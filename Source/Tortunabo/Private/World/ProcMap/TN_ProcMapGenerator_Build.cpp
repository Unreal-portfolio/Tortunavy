// ─────────────────────────────────────────────────────────────────────────────
// ATN_ProcMapGenerator — construcción de geometría: terreno, agua, límites y
// estructuras (tableros colosales, techos de cueva, isletas, labios, pasarelas).
// ─────────────────────────────────────────────────────────────────────────────

#include "World/ProcMap/TN_ProcMapGenerator.h"
#include "Core/TN_ProjectMaterials.h"
#include "World/ProcMap/TN_ProcMapTerrain.h"
#include "World/ProcMap/TN_ProcWaterActors.h"
#include "World/ProcMap/TN_ProcMapActorUtils.h"
#include "Core/TN_Log.h"
#include "ProceduralMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Async/ParallelFor.h"
#include "TN_ProcMapMeshKit.h"
#include "TN_ProcMapFormationMeshes.h"
#include "TN_ProcMapCaveMeshes.h"
#include "TN_ProcMapCaveDecor.h"
#include "TN_ProcMapFinishMeshes.h"
#include "TN_ProcMapPropMeshes.h"
#include "TN_ProcMapRockMeshes.h"
#include "TN_ProcMapAmbientFX.h"
#include "TN_ProcMapTrailColors.h"
#include "World/ProcMap/TN_ProcFauna.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Art/TN_Art.h"
#include "../../Art/TN_ArtPieces.h"

using namespace TNProcMesh;

namespace
{
	// TNLuminance, TNContrastPath y TNTrailColor están en TN_ProcMapTrailColors.h (los usa también el tutorial).

	// ── Piezas de arte de las estructuras (Docs/Arte_Assets.md, tabla en Private/Art/TN_ArtSlots_ProcMap.inl) ──
	// Todas en el espacio del mapa (el de StructureMesh y DecorMesh). Las que cambian de tamaño llevan escala por copia
	// respecto a un tamaño de referencia (el de la tabla).

	/** Pivote en At con +X hacia Dir (en planta) y escala Scale. */
	FTransform TNProcArtPivot(const FVector& At, const FVector2D& Dir, const FVector& Scale = FVector::OneVector)
	{
		return TNArt::PiecePivot(At, FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)), Scale);
	}

	/** Pivote de lo que va a lo largo del camino: en From, +X hacia To (en planta); escala X 1 = RefLength de From a To. */
	FTransform TNProcArtSpanPivot(const FVector& From, const FVector& To, double RefLength)
	{
		const FVector2D D(To.X - From.X, To.Y - From.Y);
		return TNProcArtPivot(From, D, FVector(FMath::Max(1.0, D.Size()) / RefLength, 1.0, 1.0));
	}

	/** Torre hueca: centro del suelo de la puerta, +X hacia la puerta; escala 1 = radio 1100 y 3000 del suelo a la cima. */
	FTransform TNHollowTowerArtPivot(const TNProcMap::FFeature& F)
	{
		const FVector2D C(F.Location.X, F.Location.Y);
		const double FloorZ = F.Target.Z;
		return TNProcArtPivot(FVector(C, FloorZ), FVector2D(F.Target.X, F.Target.Y) - C,
			FVector(F.Radius / 1100.0, F.Radius / 1100.0, FMath::Max(100.0, F.Height - FloorZ) / 3000.0));
	}

	/** Torre de sillería: centro de la cima, +X hacia donde va el camino; escala 1 = radio 1100 (baja hasta el terreno). */
	FTransform TNSolidTowerArtPivot(const TNProcMap::FFeature& F)
	{
		return TNProcArtPivot(FVector(F.Location.X, F.Location.Y, F.Height), F.Dir, FVector(F.Radius / 1100.0, F.Radius / 1100.0, 1.0));
	}

	/** Tablero de un puente colosal según su estilo. */
	FName TNBridgeDeckArt(TNProcMap::EBridgeStyle Style)
	{
		switch (Style)
		{
			case TNProcMap::EBridgeStyle::Stone:   return TN_ART("ProcMap.Bridge.StoneDeck");
			case TNProcMap::EBridgeStyle::Trestle: return TN_ART("ProcMap.Bridge.TrestleDeck");
			case TNProcMap::EBridgeStyle::Iron:    return TN_ART("ProcMap.Bridge.IronDeck");
			case TNProcMap::EBridgeStyle::Rope:
			default:                               return TN_ART("ProcMap.Bridge.RopeDeck");
		}
	}

	/** Poste de un hueco de salto, según el bioma (como lo construye BuildStructures). */
	FName TNGapPostArt(ETNProcBiome Biome)
	{
		switch (Biome)
		{
			case ETNProcBiome::Jungle:
			case ETNProcBiome::Mangrove: return TN_ART("ProcMap.Gap.TrunkPost");
			case ETNProcBiome::Beach:
			case ETNProcBiome::Human:    return TN_ART("ProcMap.Gap.Piling");
			case ETNProcBiome::Volcanic: return TN_ART("ProcMap.Gap.BasaltPost");
			default:                     return TN_ART("ProcMap.Gap.StonePost");
		}
	}

	/** Torre de escalada, según el bioma (como la construye BuildStructures). */
	FName TNClimbTowerArt(ETNProcBiome Biome)
	{
		switch (Biome)
		{
			case ETNProcBiome::Beach:
			case ETNProcBiome::Human:    return TN_ART("ProcMap.ClimbTower.Crates");
			case ETNProcBiome::Jungle:
			case ETNProcBiome::Mangrove: return TN_ART("ProcMap.ClimbTower.Stumps");
			case ETNProcBiome::Volcanic: return TN_ART("ProcMap.ClimbTower.Basalt");
			case ETNProcBiome::Rocky:    return TN_ART("ProcMap.ClimbTower.Slabs");
			default:                     return TN_ART("ProcMap.ClimbTower.Sandstone");
		}
	}

	/** Obstáculo de objetos del camino. */
	FName TNProcPathPropArt(TNProcMap::EPathProp Kind)
	{
		using K = TNProcMap::EPathProp;
		switch (Kind)
		{
			case K::CrateStack:     return TN_ART("ProcMap.PathProp.CrateStack");
			case K::BarrelGroup:    return TN_ART("ProcMap.PathProp.BarrelGroup");
			case K::Barricade:      return TN_ART("ProcMap.PathProp.Barricade");
			case K::HayBales:       return TN_ART("ProcMap.PathProp.HayBales");
			case K::Sandcastle:     return TN_ART("ProcMap.PathProp.Sandcastle");
			case K::Rowboat:        return TN_ART("ProcMap.PathProp.Rowboat");
			case K::BeachSet:       return TN_ART("ProcMap.PathProp.BeachSet");
			case K::Totem:          return TN_ART("ProcMap.PathProp.Totem");
			case K::RuinColumn:     return TN_ART("ProcMap.PathProp.RuinColumn");
			case K::GiantMushrooms: return TN_ART("ProcMap.PathProp.GiantMushrooms");
			case K::SkullRock:      return TN_ART("ProcMap.PathProp.SkullRock");
			case K::PotteryJars:    return TN_ART("ProcMap.PathProp.PotteryJars");
			case K::CrystalSpikes:  return TN_ART("ProcMap.PathProp.CrystalSpikes");
			case K::Cairn:          return TN_ART("ProcMap.PathProp.Cairn");
			case K::MineCart:       return TN_ART("ProcMap.PathProp.MineCart");
			case K::CrabTraps:      return TN_ART("ProcMap.PathProp.CrabTraps");
			case K::MarketStall:    return TN_ART("ProcMap.PathProp.MarketStall");
			case K::ConeLine:       return TN_ART("ProcMap.PathProp.ConeLine");
			default:                return NAME_None;
		}
	}

	/**
	 * Escala 1 de cada obstáculo: semihuella (el mayor de su radio y medio largo) y alto, los del medio de su rango
	 * (TNProcMap::FeatureDetail::PathPropSize).
	 */
	FVector2D TNProcPathPropArtRef(TNProcMap::EPathProp Kind)
	{
		using K = TNProcMap::EPathProp;
		switch (Kind)
		{
			case K::CrateStack:     return FVector2D(140.0, 165.0);
			case K::BarrelGroup:    return FVector2D(125.0, 105.0);
			case K::Barricade:      return FVector2D(165.0, 125.0);
			case K::HayBales:       return FVector2D(150.0, 115.0);
			case K::Sandcastle:     return FVector2D(140.0, 130.0);
			case K::Rowboat:        return FVector2D(210.0, 80.0);
			case K::BeachSet:       return FVector2D(180.0, 260.0);
			case K::Totem:          return FVector2D(70.0, 350.0);
			case K::RuinColumn:     return FVector2D(180.0, 270.0);
			case K::GiantMushrooms: return FVector2D(180.0, 230.0);
			case K::SkullRock:      return FVector2D(165.0, 150.0);
			case K::PotteryJars:    return FVector2D(110.0, 100.0);
			case K::CrystalSpikes:  return FVector2D(140.0, 230.0);
			case K::Cairn:          return FVector2D(90.0, 210.0);
			case K::MineCart:       return FVector2D(140.0, 150.0);
			case K::CrabTraps:      return FVector2D(120.0, 110.0);
			case K::MarketStall:    return FVector2D(200.0, 270.0);
			case K::ConeLine:       return FVector2D(190.0, 70.0);
			default:                return FVector2D(120.0, 120.0);
		}
	}

	/** Peñasco según su estilo. */
	FName TNProcBoulderArt(TNRockMesh::EBoulderStyle Style)
	{
		using S = TNRockMesh::EBoulderStyle;
		switch (Style)
		{
			case S::Slab:    return TN_ART("ProcMap.Rock.SlabBoulder");
			case S::Split:   return TN_ART("ProcMap.Rock.SplitBoulder");
			case S::Stacked: return TN_ART("ProcMap.Rock.StackedBoulder");
			case S::Strata:  return TN_ART("ProcMap.Rock.StrataBoulder");
			case S::Basalt:  return TN_ART("ProcMap.Rock.BasaltBoulder");
			case S::Mossy:   return TN_ART("ProcMap.Rock.MossyBoulder");
			case S::Crystal: return TN_ART("ProcMap.Rock.CrystalBoulder");
			case S::Coral:   return TN_ART("ProcMap.Rock.CoralBoulder");
			case S::Round:
			default:         return TN_ART("ProcMap.Rock.RoundBoulder");
		}
	}

	/** Aguja o mogote según su estilo. */
	FName TNProcSpireArt(TNRockMesh::ESpireStyle Style)
	{
		using S = TNRockMesh::ESpireStyle;
		switch (Style)
		{
			case S::Leaning:    return TN_ART("ProcMap.Rock.LeaningSpire");
			case S::Twin:       return TN_ART("ProcMap.Rock.TwinSpire");
			case S::Hoodoo:     return TN_ART("ProcMap.Rock.HoodooSpire");
			case S::Karst:      return TN_ART("ProcMap.Rock.KarstSpire");
			case S::Organ:      return TN_ART("ProcMap.Rock.OrganSpire");
			case S::Mogote:     return TN_ART("ProcMap.Rock.Mogote");
			case S::Tor:        return TN_ART("ProcMap.Rock.Tor");
			case S::StrataMesa: return TN_ART("ProcMap.Rock.StrataMesa");
			case S::LavaDome:   return TN_ART("ProcMap.Rock.LavaDome");
			case S::Spire:
			default:            return TN_ART("ProcMap.Rock.Spire");
		}
	}

	/** Escala 1 de una aguja o un mogote: radio y alto (agujas 240 x 950; mogotes, tor, mesas y domos 540 x 410). */
	FVector2D TNProcSpireArtRef(TNRockMesh::ESpireStyle Style)
	{
		using S = TNRockMesh::ESpireStyle;
		const bool bLow = Style == S::Mogote || Style == S::Tor || Style == S::StrataMesa || Style == S::LavaDome;
		return bLow ? FVector2D(540.0, 410.0) : FVector2D(240.0, 950.0);
	}

	/** Formación temática. */
	FName TNProcFormationArt(TNProcMap::EFormation Kind)
	{
		using K = TNProcMap::EFormation;
		switch (Kind)
		{
			case K::StoneArch:      return TN_ART("ProcMap.Formation.StoneArch");
			case K::WhaleRibs:      return TN_ART("ProcMap.Formation.WhaleRibs");
			case K::RootArch:       return TN_ART("ProcMap.Formation.RootArch");
			case K::TempleGate:     return TN_ART("ProcMap.Formation.TempleGate");
			case K::FallenTrunk:    return TN_ART("ProcMap.Formation.FallenTrunk");
			case K::RuinedAqueduct: return TN_ART("ProcMap.Formation.RuinedAqueduct");
			case K::Shipwreck:      return TN_ART("ProcMap.Formation.Shipwreck");
			case K::StoneHead:      return TN_ART("ProcMap.Formation.StoneHead");
			case K::BasaltColumns:  return TN_ART("ProcMap.Formation.BasaltColumns");
			case K::Fumarole:       return TN_ART("ProcMap.Formation.Fumarole");
			case K::Hoodoo:         return TN_ART("ProcMap.Formation.Hoodoo");
			case K::BalancedRock:   return TN_ART("ProcMap.Formation.BalancedRock");
			case K::Wagon:          return TN_ART("ProcMap.Formation.Wagon");
			case K::Cannon:         return TN_ART("ProcMap.Formation.Cannon");
			case K::Sandbags:       return TN_ART("ProcMap.Formation.Sandbags");
			case K::Bunker:         return TN_ART("ProcMap.Formation.Bunker");
			case K::WatchTower:     return TN_ART("ProcMap.Formation.WatchTower");
			case K::TankWreck:      return TN_ART("ProcMap.Formation.TankWreck");
			case K::GiantShell:     return TN_ART("ProcMap.Formation.GiantShell");
			case K::Anchor:         return TN_ART("ProcMap.Formation.Anchor");
			case K::StoneCircle:    return TN_ART("ProcMap.Formation.StoneCircle");
			case K::Obelisk:        return TN_ART("ProcMap.Formation.Obelisk");
			case K::FossilSkull:    return TN_ART("ProcMap.Formation.FossilSkull");
			case K::ObsidianSpires: return TN_ART("ProcMap.Formation.ObsidianSpires");
			case K::ColossalTurtle: return TN_ART("ProcMap.Formation.ColossalTurtle");
			case K::WaterTower:     return TN_ART("ProcMap.Formation.WaterTower");
			case K::Pyramid:        return TN_ART("ProcMap.Formation.Pyramid");
			case K::Lighthouse:     return TN_ART("ProcMap.Formation.Lighthouse");
			case K::Mesa:           return TN_ART("ProcMap.Formation.Mesa");
			case K::SeaStack:       return TN_ART("ProcMap.Formation.SeaStack");
			case K::CastleRuin:     return TN_ART("ProcMap.Formation.CastleRuin");
			case K::Windmill:       return TN_ART("ProcMap.Formation.Windmill");
			case K::StiltHut:       return TN_ART("ProcMap.Formation.StiltHut");
			default:                return NAME_None;
		}
	}

	/**
	 * Escala de una formación respecto a su tamaño de referencia (escala 1, la del medio de su rango en
	 * TNProcMap::FormationDetail): en los arcos, fondo (X), ancho entre pies (Y) y alto (Z); en el resto, radio (X e Y) y alto.
	 */
	FVector TNProcFormationArtScale(const TNProcMap::FFeature& F, TNProcMap::EFormation Kind)
	{
		using K = TNProcMap::EFormation;
		if (TNProcMap::IsArchFormation(Kind))
		{
			const double RefLength = Kind == K::WhaleRibs ? 1100.0 : 350.0;
			return FVector(F.Length / RefLength, F.Width / 1700.0, F.Height / 1100.0);
		}
		FVector2D Ref(300.0, 300.0);
		switch (Kind)
		{
			case K::Shipwreck:      Ref = FVector2D(800.0, 450.0); break;
			case K::StoneHead:      Ref = FVector2D(300.0, 520.0); break;
			case K::BasaltColumns:  Ref = FVector2D(420.0, 520.0); break;
			case K::Fumarole:       Ref = FVector2D(320.0, 220.0); break;
			case K::Hoodoo:         Ref = FVector2D(200.0, 850.0); break;
			case K::BalancedRock:   Ref = FVector2D(250.0, 650.0); break;
			case K::Wagon:          Ref = FVector2D(300.0, 300.0); break;
			case K::Cannon:         Ref = FVector2D(260.0, 200.0); break;
			case K::Sandbags:       Ref = FVector2D(380.0, 110.0); break;
			case K::Bunker:         Ref = FVector2D(400.0, 260.0); break;
			case K::WatchTower:     Ref = FVector2D(250.0, 850.0); break;
			case K::TankWreck:      Ref = FVector2D(380.0, 280.0); break;
			case K::GiantShell:     Ref = FVector2D(360.0, 650.0); break;
			case K::Anchor:         Ref = FVector2D(300.0, 600.0); break;
			case K::StoneCircle:    Ref = FVector2D(680.0, 380.0); break;
			case K::Obelisk:        Ref = FVector2D(180.0, 1150.0); break;
			case K::FossilSkull:    Ref = FVector2D(400.0, 320.0); break;
			case K::ObsidianSpires: Ref = FVector2D(390.0, 750.0); break;
			case K::ColossalTurtle: Ref = FVector2D(420.0, 460.0); break;
			case K::WaterTower:     Ref = FVector2D(300.0, 1000.0); break;
			case K::Pyramid:        Ref = FVector2D(2000.0, 1650.0); break;
			case K::Lighthouse:     Ref = FVector2D(380.0, 3000.0); break;
			case K::Mesa:           Ref = FVector2D(3750.0, 2650.0); break;
			case K::SeaStack:       Ref = FVector2D(650.0, 2500.0); break;
			case K::CastleRuin:     Ref = FVector2D(1850.0, 1300.0); break;
			case K::Windmill:       Ref = FVector2D(420.0, 1600.0); break;
			case K::StiltHut:       Ref = FVector2D(420.0, 650.0); break;
			default:                break;
		}
		return FVector(F.Radius / Ref.X, F.Radius / Ref.X, F.Height / Ref.Y);
	}

	/** Polilínea de un tramo de camino con cota y media anchura, recorrible por distancia en planta. */
	struct FTNPlankLine
	{
		TArray<FVector> P;
		TArray<double> HalfW;
		TArray<double> Acc;

		void Add(const FVector& Pt, double Hw)
		{
			Acc.Add(P.Num() == 0 ? 0.0 : Acc.Last() + FVector::Dist2D(P.Last(), Pt));
			P.Add(Pt);
			HalfW.Add(Hw);
		}

		double Length() const { return Acc.Num() > 0 ? Acc.Last() : 0.0; }

		void At(double S, FVector& OutP, FVector& OutDir, double& OutHw) const
		{
			int32 i = 0;
			while (i + 2 < Acc.Num() && Acc[i + 1] < S) { ++i; }
			const int32 j = FMath::Min(i + 1, P.Num() - 1);
			const double Span = FMath::Max(1e-3, Acc[j] - Acc[i]);
			const double T = FMath::Clamp((S - Acc[i]) / Span, 0.0, 1.0);
			OutP = FMath::Lerp(P[i], P[j], T);
			OutDir = (P[j] - P[i]).GetSafeNormal2D();
			if (OutDir.IsNearlyZero()) { OutDir = FVector(1.0, 0.0, 0.0); }
			OutHw = FMath::Lerp(HalfW[i], HalfW[j], T);
		}
	};

	/** Tablones atravesados con junta entre S0 y S1, con dos largueros debajo. */
	void TNProcAddPlanks(FTNProcMeshBuffers& Wood, const FTNPlankLine& Line, double S0, double S1, const FLinearColor& Base, uint32 Seed)
	{
		constexpr double Pitch = 34.0;
		constexpr double Board = 29.0;
		constexpr double Thick = 6.0;
		int32 Index = 0;
		for (double S = S0 + Board * 0.5; S < S1; S += Pitch, ++Index)
		{
			FVector C, Dir;
			double Hw = 0.0;
			Line.At(S, C, Dir, Hw);
			// Cada tablón algo distinto: largo, tono y un leve giro.
			const double Jitter = (TNProcTone(Index, Seed + 7u) - 1.0f) * 0.12;
			const FVector D = (Dir + FVector(-Dir.Y, Dir.X, 0.0) * Jitter).GetSafeNormal2D();
			const double Len = Hw * (0.96 + 0.08 * (TNProcTone(Index, Seed + 3u) - 0.82f) / 0.36f);
			Wood.AddBox(C - FVector(0.0, 0.0, Thick), D, FVector(Board * 0.5, Len, Thick), Base * TNProcTone(Index, Seed));
		}
		// Largueros: vigas a lo largo, bajo los tablones.
		constexpr double Step = 300.0;
		for (double S = S0; S < S1; S += Step)
		{
			FVector A, B, DirA, DirB;
			double HwA = 0.0, HwB = 0.0;
			Line.At(S, A, DirA, HwA);
			Line.At(FMath::Min(S + Step, S1), B, DirB, HwB);
			for (const double Side : { -0.7, 0.7 })
			{
				const FVector NA = FVector(-DirA.Y, DirA.X, 0.0) * (Side * HwA);
				const FVector NB = FVector(-DirB.Y, DirB.X, 0.0) * (Side * HwB);
				Wood.AddBeam(A + NA - FVector(0.0, 0.0, Thick * 2.0 + 8.0), B + NB - FVector(0.0, 0.0, Thick * 2.0 + 8.0), 9.0, Base * 0.6f);
			}
		}
	}

	/** Estilo de los puentes colosales (el del layout). */
	using ETNBridgeStyle = TNProcMap::EBridgeStyle;

	/** Tablero de losas de piedra con pretiles de 1 m y albardilla (viaducto). */
	void TNProcAddStoneDeck(FTNProcMeshBuffers& M, const FTNPlankLine& Line, double S0, double S1, const FLinearColor& Stone, uint32 Seed)
	{
		constexpr double Step = 160.0;
		int32 k = 0;
		for (double S = S0; S < S1; S += Step, ++k)
		{
			const double Se = FMath::Min(S + Step, S1);
			FVector A, B, DA, DB;
			double HA = 0.0, HB = 0.0;
			Line.At(S, A, DA, HA);
			Line.At(Se, B, DB, HB);
			const FVector D = (B - A).GetSafeNormal2D().IsNearlyZero() ? DA : (B - A).GetSafeNormal2D();
			const FVector N(-D.Y, D.X, 0.0);
			const FVector C = (A + B) * 0.5;
			const double Len = FVector::Dist2D(A, B) * 0.5 + 1.0;
			const double Hw = 0.5 * (HA + HB);
			M.AddBox(C - FVector(0.0, 0.0, 16.0), D, FVector(Len, Hw + 32.0, 16.0), Stone * TNProcTone(k, Seed));
			for (const double Side : { -1.0, 1.0 })
			{
				const FVector P = C + N * (Side * (Hw + 16.0));
				M.AddBox(P + FVector(0.0, 0.0, 48.0), D, FVector(Len, 16.0, 48.0), Stone * 0.9f * TNProcTone(k + 50, Seed));
				M.AddBox(P + FVector(0.0, 0.0, 100.0), D, FVector(Len + 1.0, 21.0, 5.0), Stone * 1.1f);
			}
		}
	}

	/**
	 * Arco rebajado de sillería bajo el tablero entre dos apoyos: intradós curvo, rosca más oscura y
	 * tímpanos hasta el tablero por las dos caras. La flecha no baja del suelo.
	 */
	template <typename FGround>
	void TNProcAddDeckArch(FTNProcMeshBuffers& M, const FTNPlankLine& Line, double Sa, double Sb, double TopZ, const FLinearColor& Stone, FGround&& Ground)
	{
		if (Sb - Sa < 400.0) { return; }
		const int32 N = FMath::Clamp(FMath::RoundToInt32((Sb - Sa) / 250.0), 6, 48);
		// Holgura en el tramo central (junto a los apoyos el suelo sube a la torre o a la pila).
		double MinClear = 1e9;
		for (int32 i = 0; i <= N; ++i)
		{
			const double U = static_cast<double>(i) / N;
			if (U < 0.2 || U > 0.8) { continue; }
			FVector P, D;
			double Hw = 0.0;
			Line.At(FMath::Lerp(Sa, Sb, U), P, D, Hw);
			MinClear = FMath::Min(MinClear, TopZ - Ground(FVector2D(P.X, P.Y)));
		}
		const double Rise = FMath::Clamp(FMath::Min((Sb - Sa) * 0.45, MinClear * 0.8), 150.0, 3200.0);
		const double Deck = TopZ - 32.0;
		const double Ring = FMath::Min(120.0, Rise * 0.4);
		for (int32 i = 0; i < N; ++i)
		{
			const double U0 = static_cast<double>(i) / N, U1 = static_cast<double>(i + 1) / N;
			FVector P0, P1, D0, D1;
			double H0 = 0.0, H1 = 0.0;
			Line.At(FMath::Lerp(Sa, Sb, U0), P0, D0, H0);
			Line.At(FMath::Lerp(Sa, Sb, U1), P1, D1, H1);
			// Intradós: arranca en los apoyos a Rise bajo el tablero y sube hasta él en la clave.
			const double Z0 = Deck - 30.0 - Rise * FMath::Square(2.0 * U0 - 1.0);
			const double Z1 = Deck - 30.0 - Rise * FMath::Square(2.0 * U1 - 1.0);
			const FVector N0(-D0.Y, D0.X, 0.0), N1(-D1.Y, D1.X, 0.0);
			const double W0 = H0 + 30.0, W1 = H1 + 30.0;
			const FVector A0 = FVector(P0.X, P0.Y, Z0) - N0 * W0, B0 = FVector(P0.X, P0.Y, Z0) + N0 * W0;
			const FVector A1 = FVector(P1.X, P1.Y, Z1) - N1 * W1, B1 = FVector(P1.X, P1.Y, Z1) + N1 * W1;
			M.AddQuad(A0, B0, B1, A1, FVector(0.0, 0.0, -1.0), Stone * 0.8f);
			for (const double Side : { -1.0, 1.0 })
			{
				const FVector E0 = FVector(P0.X, P0.Y, 0.0) + N0 * (Side * W0);
				const FVector E1 = FVector(P1.X, P1.Y, 0.0) + N1 * (Side * W1);
				const FVector Out = N0 * Side;
				const double R0 = FMath::Min(Z0 + Ring, Deck), R1 = FMath::Min(Z1 + Ring, Deck);
				M.AddQuad(E0 + FVector(0.0, 0.0, Z0), E1 + FVector(0.0, 0.0, Z1), E1 + FVector(0.0, 0.0, R1), E0 + FVector(0.0, 0.0, R0), Out, Stone * 0.78f);
				if (R0 < Deck || R1 < Deck)
				{
					M.AddQuad(E0 + FVector(0.0, 0.0, R0), E1 + FVector(0.0, 0.0, R1), E1 + FVector(0.0, 0.0, Deck), E0 + FVector(0.0, 0.0, Deck), Out, Stone * 0.95f);
				}
			}
		}
	}

	/**
	 * Plaza redonda de un puente colosal (TNProcMap::EFeature::BridgePlaza): suelo de losas (o chapa), pretil
	 * de 1 m con albardilla (o barandilla de hierro) abierto donde entra y sale el tablero, ménsula bajo el
	 * borde (o puntales de hierro) hasta la pila, en el centro una fuente con la tortuga (o un farol alto),
	 * bancos mirando al paisaje, farolas, una atalaya de bloques de 3 m con dos escalones de 1 m por su cara
	 * de -Dir (arriba aparece la recompensa) y la almohadilla de la medusa al pie de su otra cara.
	 *
	 * Piezas de arte en Log: la plaza (suelo, pretil, ménsula y fuente o farol), cada farola, cada banco y la atalaya.
	 */
	void TNProcAddBridgePlaza(FTNProcMeshBuffers& Solid, FTNProcMeshBuffers& Glow, FTNProcMeshBuffers& Water, const TNProcMap::FFeature& F, double DeckHw, bool bIron,
		const FLinearColor& Stone, const FLinearColor& Iron, TNArt::FPieceLog* Log)
	{
		using namespace TNProcMap;
		const FVector C = F.Location;
		const FVector D(F.Dir.X, F.Dir.Y, 0.0);
		const FVector N(-F.Dir.Y, F.Dir.X, 0.0);
		const double R = F.Radius;
		const double Top = C.Z;
		const FLinearColor Floor = bIron ? Iron * 1.6f : Stone * 1.06f;
		const FLinearColor Edge = bIron ? Iron : Stone * 0.9f;
		constexpr int32 Seg = 32;
		auto Dir = [&](double A) { return D * FMath::Cos(A) + N * FMath::Sin(A); };
		// La plaza: centro a la cota del tablero, +X a lo largo del tablero; escala 1 = radio 1000.
		const int32 PlazaPiece = Log ? Log->Begin(bIron ? TN_ART("ProcMap.Bridge.IronPlaza") : TN_ART("ProcMap.Bridge.StonePlaza"),
			TNProcArtPivot(C, F.Dir, FVector(R / 1000.0, R / 1000.0, 1.0)), { &Solid, &Glow, &Water }) : INDEX_NONE;

		// Suelo: disco con un anillo más oscuro y el centro más claro (sobre el tablero, que sigue debajo).
		for (int32 Ring = 0; Ring < 3; ++Ring)
		{
			const double Ro = Ring == 0 ? R : (Ring == 1 ? R * 0.7 : R * 0.35);
			TArray<FVector2D> Poly;
			for (int32 k = 0; k < Seg; ++k) { const FVector P = C + Dir(TwoPi * k / Seg) * Ro; Poly.Add(FVector2D(P.X, P.Y)); }
			const FLinearColor Col = Ring == 1 ? Floor * 0.86f : (Ring == 2 ? Floor * 1.1f : Floor);
			Solid.AddPrism(Poly, Top + 2.0 + Ring * 0.6, Ring == 0 ? Top - 40.0 : Top + 1.5, Col, Ring == 0);
		}
		// Pretil o barandilla por el borde, abierto en las dos entradas del tablero.
		const double Open = FMath::Asin(FMath::Clamp((DeckHw + 60.0) / R, 0.0, 0.95));
		for (int32 k = 0; k < Seg; ++k)
		{
			const double A0 = TwoPi * k / Seg, A1 = TwoPi * (k + 1) / Seg, Am = 0.5 * (A0 + A1);
			const double Off = FMath::Min(FMath::Abs(FMath::Atan2(FMath::Sin(Am), FMath::Cos(Am))), FMath::Abs(FMath::Atan2(FMath::Sin(Am - PI), FMath::Cos(Am - PI))));
			if (Off < Open) { continue; }
			const FVector P0 = C + Dir(A0) * (R - 16.0), P1 = C + Dir(A1) * (R - 16.0);
			const FVector Mid = (P0 + P1) * 0.5;
			const FVector Along = (P1 - P0).GetSafeNormal2D();
			const double Half = FVector::Dist2D(P0, P1) * 0.5 + 2.0;
			if (bIron)
			{
				Solid.AddBeam(P0 + FVector(0.0, 0.0, 0.0), P0 + FVector(0.0, 0.0, 108.0), 4.0, Iron);
				Solid.AddBeam(P0 + FVector(0.0, 0.0, 105.0), P1 + FVector(0.0, 0.0, 105.0), 3.5, Iron);
				Solid.AddBeam(P0 + FVector(0.0, 0.0, 55.0), P1 + FVector(0.0, 0.0, 55.0), 2.5, Iron);
			}
			else
			{
				Solid.AddBox(FVector(Mid.X, Mid.Y, Top + 48.0), Along, FVector(Half, 16.0, 48.0), Edge * TNProcTone(k, F.Aux2));
				Solid.AddBox(FVector(Mid.X, Mid.Y, Top + 100.0), Along, FVector(Half + 1.0, 21.0, 5.0), Stone * 1.1f);
			}
		}
		// Debajo: ménsula de piedra que se estrecha hasta la pila, o puntales de hierro.
		if (bIron)
		{
			for (int32 k = 0; k < 8; ++k)
			{
				const FVector Rim = C + Dir(TwoPi * (k + 0.5) / 8.0) * (R * 0.9) - FVector(0.0, 0.0, 20.0);
				Solid.AddBeam(Rim, C + Dir(TwoPi * (k + 0.5) / 8.0) * 250.0 - FVector(0.0, 0.0, 420.0), 9.0, Iron * 1.2f);
			}
		}
		else
		{
			TNProcAddLathe(Solid, C - FVector(0.0, 0.0, 420.0), { 0.0, 180.0, 300.0, 380.0 }, { FMath::Min(R * 0.5, 480.0), FMath::Min(R * 0.6, 560.0), R * 0.85, R + 6.0 }, 0.0, 0u, Stone * 0.82f, Seg, 0.0);
		}
		// Centro: fuente con la tortuga (piedra) o farol alto (hierro).
		if (bIron)
		{
			TNProcAddCylinder(Solid, C, C + FVector(0.0, 0.0, 40.0), 60.0, 50.0, 8, Iron);
			TNProcAddCylinder(Solid, C + FVector(0.0, 0.0, 40.0), C + FVector(0.0, 0.0, 560.0), 14.0, 9.0, 8, Iron * 1.3f);
			for (const double S : { -1.0, 1.0 })
			{
				const FVector Arm = C + N * (S * 110.0) + FVector(0.0, 0.0, 520.0);
				Solid.AddBeam(C + FVector(0.0, 0.0, 540.0), Arm, 4.0, Iron);
				Glow.AddBox(Arm - FVector(0.0, 0.0, 30.0), D, FVector(16.0, 16.0, 22.0), FLinearColor(1.f, 0.78f, 0.38f));
			}
		}
		else
		{
			constexpr double BasinR = 190.0;
			TNProcAddCylinder(Solid, C, C + FVector(0.0, 0.0, 30.0), BasinR - 20.0, BasinR - 20.0, 16, Stone * 0.7f);
			for (int32 k = 0; k < 16; ++k)
			{
				const FVector P0 = C + Dir(TwoPi * k / 16.0) * (BasinR - 10.0), P1 = C + Dir(TwoPi * (k + 1) / 16.0) * (BasinR - 10.0);
				Solid.AddBox(FVector((P0.X + P1.X) * 0.5, (P0.Y + P1.Y) * 0.5, Top + 28.0), (P1 - P0).GetSafeNormal2D(), FVector(FVector::Dist2D(P0, P1) * 0.5 + 2.0, 14.0, 28.0), Stone * 1.1f);
			}
			TArray<FVector2D> Pool;
			for (int32 k = 0; k < 16; ++k) { const FVector P = C + Dir(TwoPi * k / 16.0) * (BasinR - 22.0); Pool.Add(FVector2D(P.X, P.Y)); }
			const int32 Base = Water.Verts.Num();
			Water.AddPrism(Pool, Top + 46.0, Top + 46.0, FLinearColor(0.16f, 0.5f, 0.76f, 0.85f), false);
			for (int32 v = Base; v < Water.Verts.Num(); ++v) { Water.Colors[v] = FLinearColor(0.16f, 0.5f, 0.76f, 0.85f); }
			FTNProcMeshBuffers Statue, StatueGlow;
			TNFormMesh::TNTurtleStatue(Statue, StatueGlow, static_cast<uint32>(F.Aux2), Stone * 1.05f, Stone * 0.8f, FLinearColor(0.35f, 0.9f, 1.f), 0, false, false);
			const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(F.Dir.Y, F.Dir.X));
			TNPropMesh::TNPropAppend(Solid, Statue, C + FVector(0.0, 0.0, 26.0), Yaw, 0.5);
			TNPropMesh::TNPropAppend(Glow, StatueGlow, C + FVector(0.0, 0.0, 26.0), Yaw, 0.5);
		}
		if (Log) { Log->End(PlazaPiece); }
		// Farolas en diagonal y dos bancos mirando al paisaje, en el lado contrario a la atalaya.
		const double TowerSide = (F.Aux2 & 1) ? 1.0 : -1.0;
		for (int32 k = 0; k < 4; ++k)
		{
			const double A = PI * 0.25 + HALF_PI * k;
			const FVector P = C + Dir(A) * (R * 0.8);
			// Pieza de arte: pie de la farola (con su luz).
			TNArt::FPieceScope LampPiece(Log, TN_ART("ProcMap.Bridge.PlazaLamp"), TNArt::PiecePivot(P), { &Solid, &Glow });
			FTNProcMeshBuffers Lamp;
			TNPropMesh::TNPropLampPost(Lamp, 0);
			TNPropMesh::TNPropAppend(Solid, Lamp, P, 0.0);
			Glow.AddBox(P + FVector(0.0, 0.0, 348.0), FVector(1.0, 0.0, 0.0), FVector(14.0, 14.0, 19.0), FLinearColor(1.f, 0.78f, 0.38f));
		}
		for (const double A : { -TowerSide * HALF_PI * 0.72, -TowerSide * HALF_PI * 1.28 })
		{
			const FVector P = C + Dir(A) * (R * 0.66);
			FTNProcMeshBuffers Bench;
			TNPropMesh::TNPropBench(Bench, 0);
			// El banco mira hacia fuera (su respaldo, +Y local, hacia el centro).
			const FVector Out = Dir(A);
			// Pieza de arte: centro del banco en el suelo, con su giro (respaldo hacia +Y).
			TNArt::FPieceScope BenchPiece(Log, TN_ART("ProcMap.Bridge.PlazaBench"), TNArt::PiecePivot(P, FMath::RadiansToDegrees(FMath::Atan2(Out.Y, Out.X)) + 90.0),
				{ &Solid });
			TNPropMesh::TNPropAppend(Solid, Bench, P, FMath::RadiansToDegrees(FMath::Atan2(Out.Y, Out.X)) + 90.0);
		}
		// Atalaya: tres bloques apilados de 1 m y dos escalones de 1 m por la cara de -Dir.
		const FVector2D Tw2 = PlazaDims::TowerAt(F);
		const FVector Tw(Tw2, Top);
		constexpr double Th = PlazaDims::TowerHalf;
		// Pieza de arte (atalaya, escalones, banderín y almohadilla de la medusa): centro de su base, +X a lo largo del
		// tablero (los escalones bajan hacia -X).
		TNArt::FPieceScope LookoutPiece(Log, TN_ART("ProcMap.Bridge.PlazaLookout"), TNProcArtPivot(Tw, F.Dir), { &Solid });
		const FLinearColor Block = bIron ? FLinearColor(0.46f, 0.3f, 0.16f) : Stone * 0.95f;
		const FLinearColor Trim = bIron ? Iron : Stone * 0.78f;
		for (int32 b = 0; b < 3; ++b)
		{
			Solid.AddBox(Tw + FVector(0.0, 0.0, 50.0 + 100.0 * b), D, FVector(Th - 2.0 * b, Th - 2.0 * b, 50.0), Block * TNProcTone(b, F.Aux2));
			Solid.AddBox(Tw + FVector(0.0, 0.0, 100.0 * b + 97.0), D, FVector(Th + 4.0 - 2.0 * b, Th + 4.0 - 2.0 * b, 4.0), Trim);
		}
		for (int32 s = 0; s < 2; ++s)
		{
			const double H = 100.0 * (s + 1);
			const FVector P = Tw - D * (Th + PlazaDims::StepDepth * (1.5 - s));
			Solid.AddBox(P + FVector(0.0, 0.0, H * 0.5), D, FVector(PlazaDims::StepDepth * 0.5, Th, H * 0.5), Block * 0.92f);
		}
		// Banderín en lo alto.
		Solid.AddBeam(Tw + FVector(Th - 20.0, Th - 20.0, PlazaDims::TowerH), Tw + FVector(Th - 20.0, Th - 20.0, PlazaDims::TowerH + 220.0), 3.0, Iron);
		Solid.AddBox(Tw + FVector(Th - 20.0, Th - 20.0, PlazaDims::TowerH + 190.0) + D * 30.0, D, FVector(30.0, 1.5, 18.0), FLinearColor(0.85f, 0.12f, 0.1f));
		// Almohadilla de la medusa.
		const FVector2D Jp = PlazaDims::BouncerAt(F);
		TArray<FVector2D> Pad;
		for (int32 k = 0; k < 12; ++k) { Pad.Add(Jp + FVector2D(FMath::Cos(TwoPi * k / 12.0), FMath::Sin(TwoPi * k / 12.0)) * 110.0); }
		Solid.AddPrism(Pad, Top + 4.0, Top + 2.0, FLinearColor(0.62f, 0.3f, 0.66f), false);
	}

	/** Caballete de madera bajo el tablero: dos pies en talud, dos rectos, riostras y cruces hasta el suelo. */
	void TNProcAddTrestleBent(FTNProcMeshBuffers& M, const FVector& Deck, const FVector& Dir, double Hw, double GroundZ, const FLinearColor& Timber)
	{
		const FVector N(-Dir.Y, Dir.X, 0.0);
		const double Top = Deck.Z - 26.0;
		const double H = Top - GroundZ;
		if (H < 200.0) { return; }
		auto Leg = [&](double Off0, double Off1)
		{
			const FVector T = FVector(Deck.X, Deck.Y, Top) + N * Off0;
			const FVector B = FVector(Deck.X, Deck.Y, GroundZ - 40.0) + N * Off1;
			M.AddBeam(T, B, 11.0, Timber);
		};
		for (const double Side : { -1.0, 1.0 })
		{
			Leg(Side * Hw * 0.95, Side * (Hw * 0.95 + H * 0.14));
			Leg(Side * Hw * 0.35, Side * Hw * 0.35);
		}
		M.AddBeam(FVector(Deck.X, Deck.Y, Top) - N * (Hw + 20.0), FVector(Deck.X, Deck.Y, Top) + N * (Hw + 20.0), 13.0, Timber * 0.8f);
		double PrevZ = Top;
		for (double Zc = Top - 320.0; Zc > GroundZ + 60.0; Zc -= 320.0)
		{
			const double T = (Top - Zc) / FMath::Max(1.0, H);
			const double Half = Hw * 0.95 + H * 0.14 * T;
			const double PrevT = (Top - PrevZ) / FMath::Max(1.0, H);
			const double PrevHalf = Hw * 0.95 + H * 0.14 * PrevT;
			const FVector C(Deck.X, Deck.Y, Zc), Cp(Deck.X, Deck.Y, PrevZ);
			M.AddBeam(C - N * Half, C + N * Half, 7.0, Timber * 0.9f);
			M.AddBeam(Cp - N * PrevHalf, C + N * Half, 5.0, Timber * 0.85f);
			M.AddBeam(Cp + N * PrevHalf, C - N * Half, 5.0, Timber * 0.85f);
			PrevZ = Zc;
		}
	}

	/** Barandilla rígida: postes cada PostEvery y dos pasamanos (madera o hierro). */
	void TNProcAddRigidRails(FTNProcMeshBuffers& M, const FTNPlankLine& Line, double S0, double S1, double PostEvery, double RailH, double Half, const FLinearColor& Color)
	{
		const int32 NumPosts = FMath::Max(1, FMath::RoundToInt32((S1 - S0) / PostEvery));
		for (const double Side : { -1.0, 1.0 })
		{
			FVector PrevTop = FVector::ZeroVector, PrevMid = FVector::ZeroVector;
			for (int32 k = 0; k <= NumPosts; ++k)
			{
				const double S = S0 + (S1 - S0) * k / NumPosts;
				FVector C, Dir;
				double Hw = 0.0;
				Line.At(S, C, Dir, Hw);
				const FVector Base = C + FVector(-Dir.Y, Dir.X, 0.0) * (Side * (Hw - 10.0));
				M.AddBox(Base + FVector(0.0, 0.0, RailH * 0.5), Dir, FVector(Half, Half, RailH * 0.5 + 6.0), Color);
				const FVector Top = Base + FVector(0.0, 0.0, RailH), Mid = Base + FVector(0.0, 0.0, RailH * 0.5);
				if (k > 0)
				{
					M.AddBeam(PrevTop, Top, Half * 0.7, Color);
					M.AddBeam(PrevMid, Mid, Half * 0.5, Color);
				}
				PrevTop = Top;
				PrevMid = Mid;
			}
		}
	}

	/** Cadena de eslabones alternos entre A y B (cables del puente de hierro). */
	void TNProcAddChain(FTNProcMeshBuffers& M, const FVector& A, const FVector& B, const FLinearColor& Color)
	{
		const FVector D = B - A;
		const double Len = D.Size();
		if (Len < 1.0) { return; }
		const FVector X = D / Len;
		const FVector Y0 = FVector::CrossProduct(FVector::UpVector, X).GetSafeNormal();
		const FVector Y = Y0.IsNearlyZero() ? FVector(0.0, 1.0, 0.0) : Y0;
		const FVector Z = FVector::CrossProduct(X, Y);
		const int32 Links = FMath::Max(1, FMath::RoundToInt32(Len / 34.0));
		for (int32 k = 0; k < Links; ++k)
		{
			const FVector C = A + D * ((k + 0.5) / Links);
			const FVector Wd = (k % 2) ? Y : Z;
			for (const double Sg : { -1.0, 1.0 })
			{
				M.AddBeam(C - X * 17.0 + Wd * (Sg * 7.0), C + X * 17.0 + Wd * (Sg * 7.0), 2.2, Color);
			}
		}
	}

	/** Postes a ambos bordes cada PostEvery y cuerda de barandilla con comba entre ellos. */
	void TNProcAddRopeRails(FTNProcMeshBuffers& Wood, const FTNPlankLine& Line, double S0, double S1, double PostEvery, double PostDown,
		double RailH, const FLinearColor& PostColor, const FLinearColor& RopeColor)
	{
		const int32 NumPosts = FMath::Max(1, FMath::RoundToInt((S1 - S0) / PostEvery));
		for (const double Side : { -1.0, 1.0 })
		{
			FVector PrevTop = FVector::ZeroVector;
			for (int32 k = 0; k <= NumPosts; ++k)
			{
				const double S = S0 + (S1 - S0) * k / NumPosts;
				FVector C, Dir;
				double Hw = 0.0;
				Line.At(S, C, Dir, Hw);
				const FVector N(-Dir.Y, Dir.X, 0.0);
				const FVector Base = C + N * (Side * (Hw - 12.0));
				Wood.AddBox(Base + FVector(0.0, 0.0, (RailH + 10.0 - PostDown) * 0.5), Dir, FVector(7.0, 7.0, (RailH + 10.0 + PostDown) * 0.5), PostColor);
				const FVector Top = Base + FVector(0.0, 0.0, RailH);
				if (k > 0)
				{
					// Cuerda en tres tramos con comba.
					FVector Last = PrevTop;
					for (int32 t = 1; t <= 3; ++t)
					{
						const double U = t / 3.0;
						const FVector Pt = FMath::Lerp(PrevTop, Top, U) - FVector(0.0, 0.0, 22.0 * 4.0 * U * (1.0 - U));
						Wood.AddBeam(Last, Pt, 2.5, RopeColor);
						Last = Pt;
					}
				}
				PrevTop = Top;
			}
		}
	}

	/** Eje de una muralla: su adarve (el tramo alto del cruce), recorrible por distancia en planta (el de la lógica pura). */
	using FTNWallAxis = TNProcMap::FWallAxis;

	/**
	 * Muralla de un cruce entre S0 y S1 de su eje: caras en talud desde 3 m bajo el suelo hasta el
	 * pretil, en hiladas de sillares de tonos alternos; el adarve (S0W-S1W) entre dos parapetos con
	 * almenas; y, si la cruza el tramo bajo, su puerta: jambas a plomo, arco de medio punto con las
	 * dovelas marcadas y bóveda de cañón bajo el adarve. GroundAt(FVector2D) da la cota del terreno.
	 *
	 * Breaches (los TNProcMap::EFeature::WallBreach del cruce): mordiscos solo en lo alto (TNProcMap::WallBreachDims). Cada
	 * banda (parapeto izquierdo, adarve o, en una cornisa, la franja que queda y la hundida, y parapeto derecho) va a la
	 * cota de lo que queda en ella, con su pared donde dos bandas vecinas no coinciden y la cara del corte donde la cota
	 * cambia a lo largo del eje; las caras exteriores suben hasta lo que queda. Todo va con colisión: los huecos de la malla
	 * lo son también de la colisión. Sin mordiscos sale la muralla de siempre.
	 */
	template <typename FGround>
	void TNProcAddWall(FTNProcMeshBuffers& Out, const FTNWallAxis& Axis, double S0, double S1, double S0W, double S1W, double TopZ,
		const TNProcMap::FFeature* Gate, const TArray<const TNProcMap::FFeature*>& Breaches, const FGround& GroundAt, const FLinearColor& Stone,
		uint32 Seed)
	{
		using namespace TNProcMap;
		constexpr double Course = 180.0;
		const double ParTop = TopZ + WallDims::ParapetH;
		// Lo que queda de cada parapeto (Side 0 izquierdo, 1 derecho) y del adarve: lo más bajo de todos los mordiscos.
		auto ParapetTop = [&](int32 Side, double Sq)
		{
			double Z = ParTop;
			for (const FFeature* B : Breaches) { Z = FMath::Min(Z, WallBreachDims::ParapetTopAt(*B, Side, Sq, TopZ)); }
			return Z;
		};
		auto WalkTop = [&](double Sq, double X, double Hw)
		{
			double Z = TopZ;
			for (const FFeature* B : Breaches) { Z = FMath::Min(Z, WallBreachDims::WalkTopAt(*B, Sq, X, Hw, TopZ)); }
			return Z;
		};
		// Distancias por el eje en las que cambia alguna cota: ahí va siempre una columna.
		TArray<double> Cuts;
		for (const FFeature* B : Breaches) { WallBreachDims::Breakpoints(*B, Cuts); }
		Cuts.Sort();

		struct FCol
		{
			double S = 0.0;
			FVector2D P = FVector2D::ZeroVector, T = FVector2D::ZeroVector, N = FVector2D::ZeroVector;
			double Hw = 0.0;
			/** Pie de cada cara (0 izquierda, 1 derecha): enterrado 3 m, o el intradós bajo el arco. */
			double Bottom[2] = { 0.0, 0.0 };
		};
		double Sg = 0.0, R = 0.0, Zs = 0.0, Floor = 0.0;
		bool bGate = Gate != nullptr;
		if (bGate)
		{
			Sg = Axis.Project(FVector2D(Gate->Location.X, Gate->Location.Y));
			R = Gate->Radius;
			Zs = TopZ - WallDims::Crown - R;
			Floor = Gate->Location.Z;
			bGate = Sg - R > S0 + 100.0 && Sg + R < S1 - 100.0 && Zs > Floor + 300.0;
		}
		auto Intrados = [&](double Sq) { const double U = Sq - Sg; return Zs + FMath::Sqrt(FMath::Max(0.0, R * R - U * U)); };
		auto MakeCol = [&](double Sq, bool bArch)
		{
			FCol C;
			C.S = Sq;
			Axis.At(Sq, C.P, C.T, C.N, C.Hw);
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const double Sig = Side == 0 ? 1.0 : -1.0;
				if (bArch) { C.Bottom[Side] = Intrados(Sq); continue; }
				// Pie de la cara donde el talud corta el suelo (dos pasadas), enterrado 3 m.
				double G = GroundAt(C.P + C.N * (Sig * (C.Hw + WallDims::Parapet + 300.0)));
				G = GroundAt(C.P + C.N * (Sig * WallDims::HalfAt(C.Hw, TopZ - G)));
				C.Bottom[Side] = FMath::Min(G, TopZ - 200.0) - 300.0;
			}
			return C;
		};
		auto FacePt = [&](const FCol& C, double Sig, double Z)
		{
			const double Lat = Z >= TopZ ? C.Hw + WallDims::Parapet : WallDims::HalfAt(C.Hw, TopZ - Z);
			const FVector2D Q = C.P + C.N * (Sig * Lat);
			return FVector(Q.X, Q.Y, Z);
		};
		auto Range = [&](double A, double B, double Step, bool bArch)
		{
			// Columnas cada Step y, fuera del arco, también en cada corte de un mordisco (la regular que caiga a menos de 3 cm de
			// un corte se omite: así el corte queda exacto en las caras y en el adarve).
			TArray<double> Ss;
			const int32 Num = FMath::Max(1, FMath::CeilToInt((B - A) / Step));
			for (int32 k = 0; k <= Num; ++k)
			{
				const double Sq = A + (B - A) * k / Num;
				bool bNearCut = false;
				if (!bArch && k > 0 && k < Num) { for (const double Cs : Cuts) { bNearCut |= FMath::Abs(Cs - Sq) < 3.0; } }
				if (!bNearCut) { Ss.Add(Sq); }
			}
			if (!bArch && Cuts.Num() > 0)
			{
				for (const double Cs : Cuts) { if (Cs > A + 3.0 && Cs < B - 3.0) { Ss.Add(Cs); } }
				Ss.Sort();
			}
			TArray<FCol> Cols;
			for (int32 k = 0; k < Ss.Num(); ++k)
			{
				if (k > 0 && Ss[k] - Ss[k - 1] < 0.5) { continue; }
				Cols.Add(MakeCol(Ss[k], bArch));
			}
			return Cols;
		};
		TArray<TArray<FCol>> Parts;
		TArray<bool> IsArch;
		if (bGate)
		{
			Parts.Add(Range(S0, Sg - R, 250.0, false)); IsArch.Add(false);
			Parts.Add(Range(Sg - R, Sg + R, 50.0, true)); IsArch.Add(true);
			Parts.Add(Range(Sg + R, S1, 250.0, false)); IsArch.Add(false);
		}
		else
		{
			Parts.Add(Range(S0, S1, 250.0, false)); IsArch.Add(false);
		}

		// Caras en hiladas; bajo el arco, la primera hilada es la de las dovelas (más clara).
		for (int32 PartIdx = 0; PartIdx < Parts.Num(); ++PartIdx)
		{
			const TArray<FCol>& Cols = Parts[PartIdx];
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const double Sig = Side == 0 ? 1.0 : -1.0;
				for (int32 k = 0; k + 1 < Cols.Num(); ++k)
				{
					const FCol& A = Cols[k];
					const FCol& B = Cols[k + 1];
					const double Low = FMath::Max(A.Bottom[Side], B.Bottom[Side]);
					// Hasta el parapeto o, en un mordisco, hasta lo que queda de él o del muro.
					const double Cap = IsArch[PartIdx] ? ParTop : ParapetTop(Side, 0.5 * (A.S + B.S));
					TArray<double> Lines;
					for (int32 n = FMath::FloorToInt((TopZ - Low) / Course); n >= 0; --n)
					{
						const double Zl = TopZ - n * Course;
						if (Zl > Low + 20.0 && Zl < Cap - 10.0) { Lines.Add(Zl); }
					}
					Lines.Add(Cap);
					const FVector2D Nm = (A.N + B.N).GetSafeNormal() * Sig;
					const FVector Hint(Nm.X, Nm.Y, WallDims::Batter);
					double ZA = A.Bottom[Side], ZB = B.Bottom[Side];
					for (int32 r = 0; r < Lines.Num(); ++r)
					{
						const double Zt = Lines[r];
						const int32 CourseIdx = FMath::RoundToInt((TopZ - Zt) / Course);
						FLinearColor Col = Stone * TNProcTone(CourseIdx * 977 + (PartIdx * 1000 + k) / 2 * 131 + Side * 7, Seed);
						if (IsArch[PartIdx] && r == 0) { Col = Stone * ((k % 2) == 0 ? 1.18f : 1.08f); }
						Out.AddQuad(FacePt(A, Sig, ZA), FacePt(B, Sig, ZB), FacePt(B, Sig, Zt), FacePt(A, Sig, Zt), Hint, Col);
						ZA = ZB = Zt;
					}
				}
			}
		}

		// Puerta: jambas a plomo desde el suelo del paso (enterradas) hasta el arranque del arco, y
		// bóveda de cañón por el intradós.
		if (bGate)
		{
			for (int32 J = 0; J < 2; ++J)
			{
				const FCol& C = J == 0 ? Parts[0].Last() : Parts[2][0];
				const FVector Hint(C.T.X * (J == 0 ? 1.0 : -1.0), C.T.Y * (J == 0 ? 1.0 : -1.0), 0.0);
				double Z0 = Floor - 300.0;
				while (Z0 < Zs - 1.0)
				{
					const double Z1 = FMath::Min(Zs, (FMath::FloorToDouble((Z0 - TopZ) / Course) + 1.0) * Course + TopZ);
					const double Zt = Z1 <= Z0 + 1.0 ? Zs : Z1;
					Out.AddQuad(FacePt(C, 1.0, Z0), FacePt(C, -1.0, Z0), FacePt(C, -1.0, Zt), FacePt(C, 1.0, Zt), Hint,
						Stone * TNProcTone(FMath::RoundToInt(Z0 / Course) * 31 + J, Seed ^ 0x5A5Au) * 0.95f);
					Z0 = Zt;
				}
			}
			const TArray<FCol>& Arch = Parts[1];
			for (int32 k = 0; k + 1 < Arch.Num(); ++k)
			{
				const FCol& A = Arch[k];
				const FCol& B = Arch[k + 1];
				const double Sm = 0.5 * (A.S + B.S);
				const FVector2D Tm = (A.T + B.T).GetSafeNormal();
				const FVector Hint(Tm.X * (Sg - Sm), Tm.Y * (Sg - Sm), Zs - Intrados(Sm));
				Out.AddQuad(FacePt(A, 1.0, A.Bottom[0]), FacePt(A, -1.0, A.Bottom[1]), FacePt(B, -1.0, B.Bottom[1]), FacePt(B, 1.0, B.Bottom[0]), Hint,
					Stone * ((k % 2) == 0 ? 0.92f : 0.86f));
			}
		}

		// Remates de los extremos (quedan dentro de las torres).
		for (int32 E = 0; E < 2; ++E)
		{
			const FCol& C = E == 0 ? Parts[0][0] : Parts.Last().Last();
			const FVector Hint(C.T.X * (E == 0 ? -1.0 : 1.0), C.T.Y * (E == 0 ? -1.0 : 1.0), 0.0);
			const double Zb = FMath::Min(C.Bottom[0], C.Bottom[1]);
			Out.AddQuad(FacePt(C, 1.0, Zb), FacePt(C, -1.0, Zb), FacePt(C, -1.0, ParTop), FacePt(C, 1.0, ParTop), Hint, Stone * 0.9f);
		}

		// Adarve enlosado, caras interiores y cima de los parapetos, y almenas cada 2,6 m. Por bandas: parapeto izquierdo,
		// adarve (o, en una cornisa, la franja que queda y la hundida) y parapeto derecho, cada una a la cota de lo que queda.
		const TArray<FCol> Top = Range(S0W, S1W, 250.0, false);
		auto WallPt = [](const FCol& Col, double Lateral, double Height) { const FVector2D Q = Col.P + Col.N * Lateral; return FVector(Q.X, Q.Y, Height); };
		// Borde exterior de una banda de parapeto a la cota Height: el parapeto encima del adarve y la cara en talud debajo.
		auto OuterLat = [&](const FCol& Col, double Height) { return Height >= TopZ ? Col.Hw + WallDims::Parapet : WallDims::HalfAt(Col.Hw, TopZ - Height); };
		// Piedra rota: más oscura que la labrada, para que los mordiscos se lean de lejos.
		auto BrokenTone = [&](int32 Index, float Shade) { return Stone * Shade * TNProcTone(Index, Seed ^ 0xB4EAu); };
		// Bordes de las franjas del adarve, X = Sig · (Hw - Off) en cada columna: los dos del adarve y el de una cornisa.
		struct FLatEdge { double Sig = 1.0; double Off = 0.0; };
		auto EdgeX = [](const FLatEdge& Edge, const FCol& Col) { return Edge.Sig * (Col.Hw - Edge.Off); };
		auto GatherEdges = [&](double SqPrev, double SqNext, TArray<FLatEdge, TInlineAllocator<4>>& Edges)
		{
			Edges.Reset();
			Edges.Add(FLatEdge{ -1.0, 0.0 });
			for (const FFeature* Br : Breaches)
			{
				const EWallBreach Kind = WallBreachDims::KindOf(*Br);
				const bool bIn = (SqPrev >= Br->Target.X && SqPrev <= Br->Target.Y) || (SqNext >= Br->Target.X && SqNext <= Br->Target.Y);
				if (Kind != EWallBreach::Gap && bIn) { Edges.Add(FLatEdge{ Kind == EWallBreach::LedgeLeft ? 1.0 : -1.0, Br->Radius }); }
			}
			Edges.Add(FLatEdge{ 1.0, 0.0 });
			// De derecha a izquierda (con un semiancho cualquiera mayor que la cornisa).
			Edges.Sort([](const FLatEdge& E0, const FLatEdge& E1) { return E0.Sig * (1000.0 - E0.Off) < E1.Sig * (1000.0 - E1.Off); });
		};
		for (int32 k = 0; k + 1 < Top.Num(); ++k)
		{
			const FCol& A = Top[k];
			const FCol& B = Top[k + 1];
			const double Sm = 0.5 * (A.S + B.S);
			const double HwM = 0.5 * (A.Hw + B.Hw);
			const FVector2D Nm = (A.N + B.N).GetSafeNormal();
			TArray<FLatEdge, TInlineAllocator<4>> Edges;
			GatherEdges(Sm, Sm, Edges);
			// Franjas del adarve (una sola sin mordisco) y, en una cornisa, la pared entre la que queda y la hundida.
			TArray<double, TInlineAllocator<4>> WalkZ;
			for (int32 j = 0; j + 1 < Edges.Num(); ++j)
			{
				const double Xm = 0.5 * (Edges[j].Sig * (HwM - Edges[j].Off) + Edges[j + 1].Sig * (HwM - Edges[j + 1].Off));
				const double Zj = WalkTop(Sm, Xm, HwM);
				WalkZ.Add(Zj);
				Out.AddQuad(WallPt(A, EdgeX(Edges[j + 1], A), Zj), WallPt(A, EdgeX(Edges[j], A), Zj), WallPt(B, EdgeX(Edges[j], B), Zj),
					WallPt(B, EdgeX(Edges[j + 1], B), Zj), FVector::UpVector,
					Zj >= TopZ - 0.5 ? Stone * 1.1f * TNProcTone(k, Seed ^ 0xADA7u) : BrokenTone(k * 4 + j, 0.72f));
			}
			for (int32 j = 1; j + 1 < Edges.Num(); ++j)
			{
				const double Lo = FMath::Min(WalkZ[j - 1], WalkZ[j]);
				const double Hi = FMath::Max(WalkZ[j - 1], WalkZ[j]);
				if (Hi - Lo < 0.5) { continue; }
				const FVector2D Face = Nm * (WalkZ[j - 1] < WalkZ[j] ? -1.0 : 1.0);
				Out.AddQuad(WallPt(A, EdgeX(Edges[j], A), Lo), WallPt(B, EdgeX(Edges[j], B), Lo), WallPt(B, EdgeX(Edges[j], B), Hi), WallPt(A, EdgeX(Edges[j], A), Hi),
					FVector(Face.X, Face.Y, 0.0), BrokenTone(k * 4 + j + 2, 0.8f));
			}
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const double Sig = Side == 0 ? 1.0 : -1.0;
				const double Zp = ParapetTop(Side, Sm);
				// Franja del adarve pegada a este parapeto: la última (izquierda) o la primera (derecha).
				const double Zw = WalkZ[Side == 0 ? WalkZ.Num() - 1 : 0];
				const FVector2D Inward = Nm * -Sig;
				if (FMath::Abs(Zp - Zw) >= 0.5)
				{
					// Cara interior del parapeto (o de su resto) sobre el adarve; hacia fuera si el adarve quedase más alto.
					const double Lo = FMath::Min(Zp, Zw);
					const double Hi = FMath::Max(Zp, Zw);
					const FVector2D Face = Zp > Zw ? Inward : -Inward;
					Out.AddQuad(WallPt(A, Sig * A.Hw, Lo), WallPt(B, Sig * B.Hw, Lo), WallPt(B, Sig * B.Hw, Hi), WallPt(A, Sig * A.Hw, Hi), FVector(Face.X, Face.Y, 0.0),
						Zp >= ParTop - 0.5 && Zw >= TopZ - 0.5 ? Stone * 0.97f : BrokenTone(k * 4 + Side, 0.85f));
				}
				// Cima del parapeto, de su resto, su asiento a ras del adarve o el fondo del mordisco, hasta la cara exterior.
				Out.AddQuad(WallPt(A, Sig * A.Hw, Zp), WallPt(B, Sig * B.Hw, Zp), WallPt(B, Sig * OuterLat(B, Zp), Zp), WallPt(A, Sig * OuterLat(A, Zp), Zp),
					FVector::UpVector, Zp >= ParTop - 0.5 ? Stone * 1.05f : BrokenTone(k * 4 + Side, Zp >= TopZ - 0.5 ? 0.9f : 0.72f));
			}
		}
		// Caras del corte donde la cota cambia a lo largo del eje (bordes del mordisco, sus escalones y los extremos del
		// parapeto roto), mirando hacia donde queda más bajo.
		for (int32 k = 1; k + 1 < Top.Num() && Breaches.Num() > 0; ++k)
		{
			const FCol& C = Top[k];
			const double SqPrev = 0.5 * (Top[k - 1].S + C.S);
			const double SqNext = 0.5 * (C.S + Top[k + 1].S);
			const FVector Fwd(C.T.X, C.T.Y, 0.0);
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const double Sig = Side == 0 ? 1.0 : -1.0;
				const double Zp = ParapetTop(Side, SqPrev);
				const double Zn = ParapetTop(Side, SqNext);
				if (FMath::Abs(Zp - Zn) < 0.5) { continue; }
				const double Lo = FMath::Min(Zp, Zn);
				const double Hi = FMath::Max(Zp, Zn);
				const FVector Face = Zp > Zn ? Fwd : -Fwd;
				if (Lo < TopZ - 0.5)
				{
					// Bajo el adarve la banda llega hasta la cara en talud.
					const double Zu = FMath::Min(Hi, TopZ);
					Out.AddQuad(WallPt(C, Sig * C.Hw, Lo), WallPt(C, Sig * OuterLat(C, Lo), Lo), WallPt(C, Sig * OuterLat(C, Zu), Zu), WallPt(C, Sig * C.Hw, Zu), Face,
						BrokenTone(k * 4 + Side, 0.8f));
				}
				if (Hi > TopZ + 0.5)
				{
					const double Zd = FMath::Max(Lo, TopZ);
					const double Xo = Sig * (C.Hw + WallDims::Parapet);
					Out.AddQuad(WallPt(C, Sig * C.Hw, Zd), WallPt(C, Xo, Zd), WallPt(C, Xo, Hi), WallPt(C, Sig * C.Hw, Hi), Face, BrokenTone(k * 4 + Side, 0.88f));
				}
			}
			TArray<FLatEdge, TInlineAllocator<4>> Edges;
			GatherEdges(SqPrev, SqNext, Edges);
			for (int32 j = 0; j + 1 < Edges.Num(); ++j)
			{
				const double X0 = EdgeX(Edges[j], C);
				const double X1 = EdgeX(Edges[j + 1], C);
				const double Zp = WalkTop(SqPrev, 0.5 * (X0 + X1), C.Hw);
				const double Zn = WalkTop(SqNext, 0.5 * (X0 + X1), C.Hw);
				if (FMath::Abs(Zp - Zn) < 0.5) { continue; }
				const double Lo = FMath::Min(Zp, Zn);
				const double Hi = FMath::Max(Zp, Zn);
				Out.AddQuad(WallPt(C, X0, Lo), WallPt(C, X1, Lo), WallPt(C, X1, Hi), WallPt(C, X0, Hi), Zp > Zn ? Fwd : -Fwd, BrokenTone(k * 4 + j + 2, 0.8f));
			}
		}
		for (double Sm = S0W + 130.0; Sm < S1W - 100.0; Sm += 260.0)
		{
			const FCol C = MakeCol(Sm, true);
			for (int32 Side = 0; Side < 2; ++Side)
			{
				// Ni sobre un parapeto roto ni sobre su resto.
				if (FMath::Min3(ParapetTop(Side, Sm - 66.0), ParapetTop(Side, Sm), ParapetTop(Side, Sm + 66.0)) < ParTop - 0.5) { continue; }
				const double Sig = Side == 0 ? 1.0 : -1.0;
				const FVector2D Q = C.P + C.N * (Sig * (C.Hw + WallDims::Parapet * 0.5));
				Out.AddBox(FVector(Q.X, Q.Y, ParTop + WallDims::MerlonH * 0.5), FVector(C.T.X, C.T.Y, 0.0),
					FVector(65.0, WallDims::Parapet * 0.5, WallDims::MerlonH * 0.5), Stone * TNProcTone(FMath::RoundToInt(Sm) + Side, Seed ^ 0x3E7u));
			}
		}

		// Escombros de cada mordisco: sillares caídos en su fondo y en el escalón de cada borde (dentro de la zona de muerte)
		// y, antes de cada grupo de mordiscos, a veces un trozo de almena caído sobre el adarve junto a un parapeto (54 cm:
		// se salta o se rodea; en una cornisa, del lado del parapeto roto para no estorbar la subida a ella).
		for (const FFeature* Br : Breaches)
		{
			const double BrA = Br->Target.X;
			const double BrB = Br->Target.Y;
			const EWallBreach Kind = WallBreachDims::KindOf(*Br);
			const uint32 H = WallBreachDims::HashOf(*Br, 40);
			double LenA = 0.0, DepthA = 0.0, LenB = 0.0, DepthB = 0.0;
			WallBreachDims::EndStep(*Br, 0, LenA, DepthA);
			WallBreachDims::EndStep(*Br, 1, LenB, DepthB);
			const double DeepA = BrA + LenA;
			const double DeepB = BrB - LenB;
			const int32 Pieces = DeepB - DeepA > 60.0 ? 2 + static_cast<int32>(H & 1u) : 0;
			for (int32 n = 0; n < Pieces; ++n)
			{
				const uint32 Hn = HashCell(H, n, 1);
				const FCol Col = MakeCol(FMath::Lerp(DeepA + 25.0, DeepB - 25.0, static_cast<double>(Hn & 0xFF) / 255.0), true);
				const double U = static_cast<double>((Hn >> 8) & 0xFF) / 255.0;
				double X = (U * 2.0 - 1.0) * (Col.Hw - 40.0);
				if (Kind == EWallBreach::LedgeLeft) { X = FMath::Min(X, Col.Hw - Br->Radius - 45.0); }
				if (Kind == EWallBreach::LedgeRight) { X = FMath::Max(X, -Col.Hw + Br->Radius + 45.0); }
				const FVector Half(22.0 + 20.0 * static_cast<double>((Hn >> 16) & 0xFF) / 255.0, 18.0 + 16.0 * static_cast<double>((Hn >> 24) & 0xFF) / 255.0,
					14.0 + 12.0 * U);
				const double Yaw = static_cast<double>((Hn >> 4) & 0xFF) / 255.0 * Pi;
				Out.AddBox(WallPt(Col, X, TopZ - Br->Height + Half.Z), FVector(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.0), Half, BrokenTone(n + 50, 0.85f));
			}
			for (int32 End = 0; End < 2; ++End)
			{
				const double StepLen = End == 0 ? LenA : LenB;
				const double StepZ = TopZ - (End == 0 ? DepthA : DepthB);
				if (StepLen < 40.0) { continue; }
				const FCol Col = MakeCol(End == 0 ? BrA + StepLen * 0.5 : BrB - StepLen * 0.5, true);
				for (int32 Side = 0; Side < 2; ++Side)
				{
					if (!WallBreachDims::CutsSide(*Br, Side)) { continue; }
					const double Sig = Side == 0 ? 1.0 : -1.0;
					const FVector Half(FMath::Min(StepLen * 0.4, 34.0), 26.0, 17.0);
					Out.AddBox(WallPt(Col, Sig * (OuterLat(Col, StepZ) - 30.0), StepZ + Half.Z), FVector(Col.T.X, Col.T.Y, 0.0), Half, BrokenTone(End * 2 + Side + 60, 0.95f));
				}
			}
			bool bGroupStart = true;
			for (const FFeature* Other : Breaches)
			{
				if (Other != Br && Other->Target.Y <= BrA && BrA - Other->Target.Y < 900.0) { bGroupStart = false; }
			}
			const double Sf = BrA - WallBreachDims::ParapetBreakMax - 170.0 - 200.0 * static_cast<double>((H >> 16) & 0xFF) / 255.0;
			if (bGroupStart && (H >> 8) % 5u < 3u && Sf > S0W + 300.0)
			{
				const int32 Side = Kind == EWallBreach::Gap ? static_cast<int32>((H >> 12) & 1u) : (Kind == EWallBreach::LedgeLeft ? 1 : 0);
				const double Sig = Side == 0 ? 1.0 : -1.0;
				const FCol Col = MakeCol(Sf, true);
				const double Tilt = FMath::DegreesToRadians(-15.0 + 30.0 * static_cast<double>((H >> 24) & 0xFF) / 255.0);
				const FVector2D Ax = Col.T * FMath::Cos(Tilt) + Col.N * FMath::Sin(Tilt);
				Out.AddBox(WallPt(Col, Sig * (Col.Hw - 65.0), TopZ + 27.0), FVector(Ax.X, Ax.Y, 0.0), FVector(60.0, 40.0, 27.0), BrokenTone(70 + Side, 1.0f));
			}
		}
	}

	/**
	 * Estandarte en el lado cerrado del pretil de una torre más alejado de sus aberturas (Open, por lado).
	 */
	inline void TNProcAddTowerBanner(FTNProcMeshBuffers& Cloth, const TArray<bool>& Open, const FVector2D& C, double Ri, double Ro, double ParTop, const FLinearColor& Banner)
	{
		using namespace TNProcMap;
		const int32 Sides = Open.Num();
		int32 Best = INDEX_NONE;
		int32 BestRun = -1;
		for (int32 k = 0; k < Sides; ++k)
		{
			if (Open[k]) { continue; }
			int32 Run = 0;
			while (Run < Sides && !Open[(k + Run) % Sides] && !Open[(k - Run + Sides) % Sides]) { ++Run; }
			if (Run > BestRun) { BestRun = Run; Best = k; }
		}
		if (Best == INDEX_NONE) { return; }
		const double Am = TwoPi * (Best + 0.5) / Sides;
		const FVector2D Q = C + FVector2D(FMath::Cos(Am), FMath::Sin(Am)) * (0.5 * (Ri + Ro));
		const FVector Base(Q.X, Q.Y, ParTop);
		const FVector TopP = Base + FVector(0.0, 0.0, 750.0);
		Cloth.AddBeam(Base, TopP, 7.0, FLinearColor(0.35f, 0.24f, 0.14f));
		const FVector2D Fd(-FMath::Sin(Am), FMath::Cos(Am));
		const FVector Fx(Fd.X * 260.0, Fd.Y * 260.0, 0.0);
		const FVector Fl0 = TopP - FVector(0.0, 0.0, 20.0);
		const FVector Fl1 = TopP - FVector(0.0, 0.0, 170.0);
		const FVector Wave(0.0, 0.0, -30.0);
		const FVector Nf(Fd.Y, -Fd.X, 0.0);
		Cloth.AddQuad(Fl0, Fl0 + Fx + Wave, Fl1 + Fx + Wave, Fl1, Nf, Banner);
		Cloth.AddQuad(Fl0, Fl1, Fl1 + Fx + Wave, Fl0 + Fx + Wave, -Nf, Banner * 0.8f);
	}

	/**
	 * Torre de muralla: forro de sillería en talud (TowerDims::Sides lados) sobre el pilar del terreno,
	 * cima enlosada TowerDims::PaveLift por encima del pilar, pretil de 1,9 m con almenas que cubre el
	 * de roca y se abre al adarve y al tobogán (ahí el forro baja a plomo a TowerDims::FlushOut del
	 * pilar y el enlosado llega hasta él), y un estandarte en lo alto. Los lados abiertos son los de
	 * TowerOpenSides, los mismos que deja el terreno.
	 */
	template <typename FGround>
	void TNProcAddWallTower(FTNProcMeshBuffers& Out, FTNProcMeshBuffers& Cloth, const TNProcMap::FLayout& Layout, const TNProcMap::FFeature& F,
		const FGround& GroundAt, const FLinearColor& Stone, const FLinearColor& Banner, uint32 Seed)
	{
		using namespace TNProcMap;
		constexpr int32 Sides = TowerDims::Sides;
		constexpr double Course = 180.0;
		const FVector2D C(F.Location.X, F.Location.Y);
		const double TopZ = F.Height;
		const double PaveZ = TopZ + TowerDims::PaveLift;
		const double R = F.Radius;
		const double Rf = R + TowerDims::FlushOut;
		const double Ri = R - 320.0;
		const double Ro = R + 220.0;
		const double ParTop = TopZ + 190.0;
		auto Dir = [](double A) { return FVector2D(FMath::Cos(A), FMath::Sin(A)); };
		const uint32 OpenMask = TowerOpenSides(Layout, F);
		TArray<bool> Open;
		for (int32 k = 0; k < Sides; ++k) { Open.Add(((OpenMask >> k) & 1u) != 0u); }
		double Ground = TopZ;
		for (int32 k = 0; k < Sides; ++k) { Ground = FMath::Min(Ground, GroundAt(C + Dir(TwoPi * k / Sides) * (Ro + 400.0))); }
		const double Zb = Ground - 300.0;
		auto OuterAt = [&](double A, double Z, bool bFlush)
		{
			const double Rad = bFlush ? Rf : Ro + WallDims::Batter * FMath::Max(0.0, TopZ - Z);
			const FVector2D Q = C + Dir(A) * Rad;
			return FVector(Q.X, Q.Y, Z);
		};
		auto RingAt = [&](double A, double Rad, double Z) { const FVector2D Q = C + Dir(A) * Rad; return FVector(Q.X, Q.Y, Z); };
		for (int32 k = 0; k < Sides; ++k)
		{
			const double A0 = TwoPi * k / Sides;
			const double A1 = TwoPi * (k + 1) / Sides;
			const double Am = 0.5 * (A0 + A1);
			const FVector Hint(FMath::Cos(Am), FMath::Sin(Am), WallDims::Batter);
			const bool bOpen = Open[k];
			const double Zt = bOpen ? PaveZ : ParTop;
			// Enlosado de la cima: hasta el pretil en los lados cerrados y hasta el forro a plomo en los abiertos.
			Out.AddTri(FVector(C.X, C.Y, PaveZ), RingAt(A0, bOpen ? Rf : Ri, PaveZ), RingAt(A1, bOpen ? Rf : Ri, PaveZ), FVector::UpVector,
				Stone * 1.06f * TNProcTone(k, Seed ^ 0x51ABu));
			double Z0 = Zb;
			while (Z0 < Zt - 1.0)
			{
				const double Z1 = FMath::Min(Zt, (FMath::FloorToDouble((Z0 - TopZ) / Course) + 1.0) * Course + TopZ);
				const double Z2 = Z1 <= Z0 + 1.0 ? Zt : Z1;
				Out.AddQuad(OuterAt(A0, Z0, bOpen), OuterAt(A1, Z0, bOpen), OuterAt(A1, Z2, bOpen), OuterAt(A0, Z2, bOpen), Hint,
					Stone * TNProcTone(FMath::RoundToInt((TopZ - Z2) / Course) * 97 + k / 2, Seed));
				Z0 = Z2;
			}
			if (bOpen) { continue; }
			// Pretil: cara interior, cima y almena en medio del lado; y cierre junto a las aberturas.
			Out.AddQuad(RingAt(A1, Ri, TopZ), RingAt(A0, Ri, TopZ), RingAt(A0, Ri, ParTop), RingAt(A1, Ri, ParTop), FVector(-Hint.X, -Hint.Y, 0.0), Stone * 0.95f);
			Out.AddQuad(RingAt(A0, Ri, ParTop), RingAt(A1, Ri, ParTop), OuterAt(A1, ParTop, false), OuterAt(A0, ParTop, false), FVector::UpVector, Stone * 1.05f);
			const FVector2D Tg(-FMath::Sin(Am), FMath::Cos(Am));
			if ((k % 2) == 0)
			{
				// Almenas en el borde exterior, una sí y otra no.
				const FVector2D Mc = C + Dir(Am) * (Ro - 60.0);
				Out.AddBox(FVector(Mc.X, Mc.Y, ParTop + WallDims::MerlonH * 0.5), FVector(Tg.X, Tg.Y, 0.0),
					FVector(0.5 * Ro * (A1 - A0), 60.0, WallDims::MerlonH * 0.5), Stone * TNProcTone(k, Seed ^ 0x77u));
			}
			for (int32 E = 0; E < 2; ++E)
			{
				const int32 Nb = (k + (E == 0 ? Sides - 1 : 1)) % Sides;
				if (!Open[Nb]) { continue; }
				const double Ae = E == 0 ? A0 : A1;
				const FVector2D Te = Tg * (E == 0 ? -1.0 : 1.0);
				// Junto a una abertura: cierre del pretil y, debajo, del forro hasta el de a plomo.
				Out.AddQuad(RingAt(Ae, Ri, PaveZ), OuterAt(Ae, PaveZ, false), OuterAt(Ae, ParTop, false), RingAt(Ae, Ri, ParTop), FVector(Te.X, Te.Y, 0.0), Stone * 0.9f);
				Out.AddQuad(RingAt(Ae, Rf, Zb), OuterAt(Ae, Zb, false), OuterAt(Ae, PaveZ, false), RingAt(Ae, Rf, PaveZ), FVector(Te.X, Te.Y, 0.0), Stone * 0.9f);
			}
		}
		TNProcAddTowerBanner(Cloth, Open, C, Ri, Ro, ParTop, Banner);
	}

	/**
	 * Torre hueca (la de entrada de un cruce; TNProcMap::TowerDims): sillería en talud por fuera y a plomo por dentro,
	 * sobre el núcleo del terreno; forjado a la cota de la cima con un hueco en el centro por el que sube el géiser;
	 * pretil de 1,9 m con almenas, abierto al puente o al adarve (ahí el muro se enlosa a la cota de la cima);
	 * saeteras, antorchas dentro (Glow) y estandarte (Cloth). Out lleva colisión.
	 *
	 * Puerta hacia el camino que llega: un túnel recto de 2·DoorHalf de ancho que atraviesa todo el grueso del muro,
	 * con paredes paralelas, bóveda de medio punto y suelo enlosado; por fuera, una portada plana al pie del talud
	 * (cierra los cuatro lados que abre la puerta) con impostas, dovelas y clave en relieve, el rastrillo levantado y
	 * dos antorchas; por dentro, un marco plano con el mismo arco. El terreno deja libre esa franja (TNProcMap
	 * TerrainBuilder, rama de IsHollowTower).
	 */
	template <typename FGround>
	void TNProcAddHollowTower(FTNProcMeshBuffers& Out, FTNProcMeshBuffers& Glow, FTNProcMeshBuffers& Cloth, const TNProcMap::FLayout& Layout,
		const TNProcMap::FFeature& F, const FGround& GroundAt, const FLinearColor& Stone, const FLinearColor& Banner, uint32 Seed)
	{
		using namespace TNProcMap;
		constexpr int32 Sides = TowerDims::Sides;
		constexpr double Course = 180.0;
		const FVector2D C(F.Location.X, F.Location.Y);
		const double TopZ = F.Height;
		const double PaveZ = TopZ + TowerDims::PaveLift;
		const double FloorZ = F.Target.Z;
		const double R = F.Radius;
		const double Ri = R - TowerDims::Wall;
		const double Ro = R + TowerDims::Skirt;
		const double ParTop = TopZ + 190.0;
		const double Slab = 60.0;
		auto Dir = [](double A) { return FVector2D(FMath::Cos(A), FMath::Sin(A)); };
		const FVector2D DoorDir = (FVector2D(F.Target.X, F.Target.Y) - C).GetSafeNormal();
		// La puerta abre los cuatro lados alrededor del vértice más cercano a su dirección (DoorDir ya apunta a él): por
		// dentro, dos lados no llegan al ancho del túnel.
		const int32 DoorK = ((FMath::RoundToInt(FMath::Atan2(DoorDir.Y, DoorDir.X) / TwoPi * Sides) % Sides) + Sides) % Sides;
		auto IsDoor = [&](int32 k) { const int32 d = ((k - DoorK) % Sides + Sides) % Sides; return d <= 1 || d >= Sides - 2; };
		const double GateHalf = TowerDims::DoorHalf;
		const double Spring = FloorZ + TowerDims::DoorTop - GateHalf - 70.0;
		const double GateTop = Spring + GateHalf + Course;
		const uint32 OpenMask = TowerOpenSides(Layout, F);
		TArray<bool> Open;
		for (int32 k = 0; k < Sides; ++k) { Open.Add(((OpenMask >> k) & 1u) != 0u); }
		double Ground = FloorZ;
		for (int32 k = 0; k < Sides; ++k) { Ground = FMath::Min(Ground, GroundAt(C + Dir(TwoPi * k / Sides) * (Ro + 400.0))); }
		const double Zb = Ground - 300.0;
		auto OuterAt = [&](double A, double Z) { const FVector2D Q = C + Dir(A) * (Ro + WallDims::Batter * FMath::Max(0.0, TopZ - Z)); return FVector(Q.X, Q.Y, Z); };
		auto RingAt = [&](double A, double Rad, double Z) { const FVector2D Q = C + Dir(A) * Rad; return FVector(Q.X, Q.Y, Z); };
		// Hiladas de sillería de Z0 a Z1 alineadas con la cima.
		auto Courses = [&](double Z0, double Z1, TFunctionRef<void(double, double, int32)> Band)
		{
			while (Z0 < Z1 - 1.0)
			{
				const double Zn = FMath::Min(Z1, (FMath::FloorToDouble((Z0 - TopZ) / Course) + 1.0) * Course + TopZ);
				const double Zt = Zn <= Z0 + 1.0 ? Z1 : Zn;
				Band(Z0, Zt, FMath::RoundToInt((TopZ - Zt) / Course));
				Z0 = Zt;
			}
		};
		for (int32 k = 0; k < Sides; ++k)
		{
			const double A0 = TwoPi * k / Sides;
			const double A1 = TwoPi * (k + 1) / Sides;
			const double Am = 0.5 * (A0 + A1);
			const FVector Hint(FMath::Cos(Am), FMath::Sin(Am), WallDims::Batter);
			const bool bOpen = Open[k];
			const bool bDoor = IsDoor(k);
			// Cara exterior en hiladas (sobre la puerta, desde lo alto de la portada) y cara interior a plomo hasta el forjado.
			Courses(bDoor ? GateTop : Zb, bOpen ? PaveZ : ParTop, [&](double Z0, double Z1, int32 Idx)
			{
				Out.AddQuad(OuterAt(A0, Z0), OuterAt(A1, Z0), OuterAt(A1, Z1), OuterAt(A0, Z1), Hint, Stone * TNProcTone(Idx * 97 + k / 2, Seed));
			});
			Courses(bDoor ? GateTop : FloorZ - 60.0, TopZ - Slab, [&](double Z0, double Z1, int32 Idx)
			{
				Out.AddQuad(RingAt(A1, Ri, Z0), RingAt(A0, Ri, Z0), RingAt(A0, Ri, Z1), RingAt(A1, Ri, Z1), FVector(-Hint.X, -Hint.Y, 0.0),
					Stone * 0.82f * TNProcTone(Idx * 53 + k / 2, Seed ^ 0x1D1Du));
			});
			// Forjado: losa enlosada a la cota de la cima (más PaveLift), su cara de abajo y el canto del hueco.
			const double H0 = TowerDims::HoleR;
			Out.AddQuad(RingAt(A0, H0, PaveZ), RingAt(A1, H0, PaveZ), RingAt(A1, Ri, PaveZ), RingAt(A0, Ri, PaveZ), FVector::UpVector, Stone * 1.08f * TNProcTone(k, Seed ^ 0x51ABu));
			Out.AddQuad(RingAt(A1, H0, TopZ - Slab), RingAt(A0, H0, TopZ - Slab), RingAt(A0, Ri, TopZ - Slab), RingAt(A1, Ri, TopZ - Slab), -FVector::UpVector, Stone * 0.7f);
			Out.AddQuad(RingAt(A1, H0, TopZ - Slab), RingAt(A1, H0, PaveZ), RingAt(A0, H0, PaveZ), RingAt(A0, H0, TopZ - Slab), FVector(-Hint.X, -Hint.Y, 0.0), Stone * 0.9f);
			if (bOpen)
			{
				// Hacia el puente o el adarve: el muro enlosado a la misma cota, por encima del núcleo del terreno y
				// del tablero o el adarve que entran en la torre.
				Out.AddQuad(RingAt(A0, Ri, PaveZ), RingAt(A1, Ri, PaveZ), OuterAt(A1, PaveZ), OuterAt(A0, PaveZ), FVector::UpVector, Stone * 1.05f);
				continue;
			}
			// Pretil: cara interior, cima y almena en medio del lado (una sí y otra no).
			Out.AddQuad(RingAt(A1, Ri, TopZ), RingAt(A0, Ri, TopZ), RingAt(A0, Ri, ParTop), RingAt(A1, Ri, ParTop), FVector(-Hint.X, -Hint.Y, 0.0), Stone * 0.95f);
			Out.AddQuad(RingAt(A0, Ri, ParTop), RingAt(A1, Ri, ParTop), OuterAt(A1, ParTop), OuterAt(A0, ParTop), FVector::UpVector, Stone * 1.05f);
			const FVector2D Tg(-FMath::Sin(Am), FMath::Cos(Am));
			if ((k % 2) == 0)
			{
				const FVector2D Mc = C + Dir(Am) * (Ro - 60.0);
				Out.AddBox(FVector(Mc.X, Mc.Y, ParTop + WallDims::MerlonH * 0.5), FVector(Tg.X, Tg.Y, 0.0),
					FVector(0.5 * Ro * (A1 - A0), 60.0, WallDims::MerlonH * 0.5), Stone * TNProcTone(k, Seed ^ 0x77u));
			}
			for (int32 E = 0; E < 2; ++E)
			{
				const int32 Nb = (k + (E == 0 ? Sides - 1 : 1)) % Sides;
				if (!Open[Nb]) { continue; }
				const double Ae = E == 0 ? A0 : A1;
				const FVector2D Te = Tg * (E == 0 ? -1.0 : 1.0);
				Out.AddQuad(RingAt(Ae, Ri, PaveZ), OuterAt(Ae, PaveZ), OuterAt(Ae, ParTop), RingAt(Ae, Ri, ParTop), FVector(Te.X, Te.Y, 0.0), Stone * 0.9f);
			}
			// Saeteras: rendijas oscuras en la cara exterior, a varias alturas, una de cada cuatro caras.
			if ((k % 4) == 1 && !bDoor)
			{
				for (double Zs = FloorZ + 900.0; Zs < TopZ - 400.0; Zs += 1100.0)
				{
					const FVector P = OuterAt(Am, Zs) + FVector(Hint.X, Hint.Y, 0.0) * 4.0;
					Out.AddBox(P, FVector(Tg.X, Tg.Y, 0.0), FVector(18.0, 6.0, 110.0), FLinearColor(0.03f, 0.03f, 0.035f));
				}
			}
		}
		// ── Puerta ──
		// Marco local: X hacia fuera por DoorDir, Y a lo ancho (Across) y Z arriba; el túnel va de Xin (marco de dentro,
		// junto a la cuerda de los lados abiertos por dentro) a Xout (portada, delante del pie del talud).
		{
			const FVector2D Across(-DoorDir.Y, DoorDir.X);
			auto G = [&](double X, double Y, double Z) { const FVector2D Q = C + DoorDir * X + Across * Y; return FVector(Q.X, Q.Y, Z); };
			const FVector Fw(DoorDir.X, DoorDir.Y, 0.0);
			const FVector Side(Across.X, Across.Y, 0.0);
			const double OpenA = TwoPi * 2.0 / Sides;
			const double CosO = FMath::Cos(OpenA), SinO = FMath::Sin(OpenA);
			auto OuterR = [&](double Z) { return Ro + WallDims::Batter * FMath::Max(0.0, TopZ - Z); };
			const double Xin = Ri * CosO - 12.0;
			const double Win = Ri * SinO + 15.0;
			const double Xout = OuterR(FloorZ) + 30.0;
			const double Wout = OuterR(Zb) * SinO + 25.0;
			const FLinearColor Face = Stone * 1.06f;
			constexpr int32 ArchSeg = 12;
			auto ArchY = [&](int32 i) { return GateHalf * FMath::Cos(PI * i / ArchSeg); };
			auto ArchZ = [&](int32 i) { return Spring + GateHalf * FMath::Sin(PI * i / ArchSeg); };

			// Portadas (fuera, mirando a DoorDir; dentro, al centro): paños a los lados del túnel y, sobre el arco, tiras
			// verticales hasta arriba.
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				const bool bOut = Pass == 0;
				const double X = bOut ? Xout : Xin;
				const double W = bOut ? Wout : Win;
				const double Z0 = bOut ? Zb : FloorZ - 60.0;
				const FVector N = bOut ? Fw : -Fw;
				for (const double Sg : { -1.0, 1.0 })
				{
					Courses(Z0, GateTop, [&](double Za, double Zc, int32 Idx)
					{
						Out.AddQuad(G(X, Sg * GateHalf, Za), G(X, Sg * W, Za), G(X, Sg * W, Zc), G(X, Sg * GateHalf, Zc), N,
							Face * TNProcTone(Idx * 29 + (Sg > 0.0 ? 1 : 0) + Pass * 7, Seed ^ 0x6A7Eu));
					});
				}
				for (int32 i = 0; i < ArchSeg; ++i)
				{
					Out.AddQuad(G(X, ArchY(i), ArchZ(i)), G(X, ArchY(i + 1), ArchZ(i + 1)), G(X, ArchY(i + 1), GateTop), G(X, ArchY(i), GateTop), N, Face * 0.96f);
				}
			}

			// Remates de la portada contra el talud: arriba, hasta el pie de las hiladas de los lados abiertos; a los
			// lados, hasta la arista de los vértices extremos.
			{
				const double Rg = OuterR(GateTop);
				TArray<FVector2D> Poly;
				for (int32 v = -2; v <= 2; ++v)
				{
					const double A = OpenA * 0.5 * v;
					Poly.Add(FVector2D(Rg * FMath::Cos(A), Rg * FMath::Sin(A)));
				}
				for (int32 v = 0; v + 1 < Poly.Num(); ++v)
				{
					const double Ya = v == 0 ? -Wout : Poly[v].Y;
					const double Yb = v + 2 == Poly.Num() ? Wout : Poly[v + 1].Y;
					Out.AddQuad(G(Xout, Ya, GateTop), G(Xout, Yb, GateTop), G(Poly[v + 1].X, Poly[v + 1].Y, GateTop), G(Poly[v].X, Poly[v].Y, GateTop), FVector::UpVector, Stone);
				}
				for (const double Sg : { -1.0, 1.0 })
				{
					Courses(Zb, GateTop, [&](double Za, double Zc, int32 Idx)
					{
						const double Ra = OuterR(Za), Rc = OuterR(Zc);
						Out.AddQuad(G(Xout, Sg * Wout, Za), G(Ra * CosO, Sg * Ra * SinO, Za), G(Rc * CosO, Sg * Rc * SinO, Zc), G(Xout, Sg * Wout, Zc), Side * Sg,
							Stone * 0.9f * TNProcTone(Idx * 17 + (Sg > 0.0 ? 3 : 0), Seed ^ 0x3C3Cu));
					});
				}
				// Por dentro, la ceja entre el marco y la cuerda de los lados abiertos.
				for (int32 v = -2; v < 2; ++v)
				{
					const double A0 = OpenA * 0.5 * v, A1 = OpenA * 0.5 * (v + 1);
					Out.AddQuad(G(Xin, Win * FMath::Clamp(v / 2.0, -1.0, 1.0), GateTop), G(Ri * FMath::Cos(A0), Ri * FMath::Sin(A0), GateTop),
						G(Ri * FMath::Cos(A1), Ri * FMath::Sin(A1), GateTop), G(Xin, Win * FMath::Clamp((v + 1) / 2.0, -1.0, 1.0), GateTop), -FVector::UpVector, Stone * 0.8f);
				}
			}

			// Túnel: paredes paralelas, bóveda de medio punto y suelo enlosado.
			for (const double Sg : { -1.0, 1.0 })
			{
				Courses(FloorZ - 60.0, Spring, [&](double Za, double Zc, int32 Idx)
				{
					Out.AddQuad(G(Xin, Sg * GateHalf, Za), G(Xout, Sg * GateHalf, Za), G(Xout, Sg * GateHalf, Zc), G(Xin, Sg * GateHalf, Zc), -Side * Sg,
						Stone * 0.8f * TNProcTone(Idx * 41 + (Sg > 0.0 ? 5 : 0), Seed ^ 0x7E57u));
				});
			}
			for (int32 i = 0; i < ArchSeg; ++i)
			{
				const double Am = PI * (i + 0.5) / ArchSeg;
				const FVector In(-Side * FMath::Cos(Am) - FVector::UpVector * FMath::Sin(Am));
				Out.AddQuad(G(Xin, ArchY(i), ArchZ(i)), G(Xout, ArchY(i), ArchZ(i)), G(Xout, ArchY(i + 1), ArchZ(i + 1)), G(Xin, ArchY(i + 1), ArchZ(i + 1)), In,
					Stone * ((i % 2) ? 0.74f : 0.78f));
			}
			for (double X0 = Xin - 60.0; X0 < Xout + 260.0; X0 += 200.0)
			{
				const double X1 = FMath::Min(X0 + 200.0, Xout + 260.0);
				const int32 Tile = FMath::RoundToInt(X0 / 200.0);
				Out.AddQuad(G(X0, -GateHalf, FloorZ + 3.0), G(X1, -GateHalf, FloorZ + 3.0), G(X1, GateHalf, FloorZ + 3.0), G(X0, GateHalf, FloorZ + 3.0), FVector::UpVector,
					Stone * 0.95f * TNProcTone(Tile, Seed ^ 0x51A8u));
			}

			// Dovelas en relieve sobre la portada (la clave, más grande), impostas en el arranque del arco.
			constexpr int32 Vous = 11;
			for (int32 v = 0; v < Vous; ++v)
			{
				const bool bKey = v == Vous / 2;
				const double A0 = PI * v / Vous + 0.012, A1 = PI * (v + 1) / Vous - 0.012;
				const double R0 = GateHalf - 2.0, R1 = GateHalf + (bKey ? 105.0 : 78.0);
				const double Depth = bKey ? 42.0 : 28.0;
				auto P = [&](double A, double Rr, double Dx) { return G(Xout + Dx, Rr * FMath::Cos(A), Spring + Rr * FMath::Sin(A)); };
				const FLinearColor Col = Stone * (bKey ? 1.25f : ((v % 2) ? 1.12f : 1.18f));
				const FVector Ra(Side * FMath::Cos(0.5 * (A0 + A1)) + FVector::UpVector * FMath::Sin(0.5 * (A0 + A1)));
				Out.AddQuad(P(A0, R0, Depth), P(A1, R0, Depth), P(A1, R1, Depth), P(A0, R1, Depth), Fw, Col);
				Out.AddQuad(P(A0, R0, 0.0), P(A1, R0, 0.0), P(A1, R0, Depth), P(A0, R0, Depth), -Ra, Col * 0.8f);
				Out.AddQuad(P(A0, R1, 0.0), P(A1, R1, 0.0), P(A1, R1, Depth), P(A0, R1, Depth), Ra, Col * 0.9f);
				for (const double A : { A0, A1 })
				{
					const FVector Tn(-Side * FMath::Sin(A) + FVector::UpVector * FMath::Cos(A));
					Out.AddQuad(P(A, R0, 0.0), P(A, R1, 0.0), P(A, R1, Depth), P(A, R0, Depth), A == A0 ? -Tn : Tn, Col * 0.85f);
				}
			}
			for (const double Sg : { -1.0, 1.0 })
			{
				Out.AddBox(G(Xout + 16.0, Sg * (GateHalf + 45.0), Spring - 14.0), Fw, FVector(20.0, 55.0, 14.0), Stone * 1.2f);
			}

			// Rastrillo levantado: barrotes de hierro dentro del arco, con puntas abajo, y dos travesaños.
			const FLinearColor Iron(0.1f, 0.1f, 0.11f);
			const double Xp = Xout - 45.0;
			const double Bottom = Spring + 0.3 * GateHalf;
			for (double Y = -GateHalf + 28.0; Y <= GateHalf - 27.0; Y += 46.0)
			{
				const double TopY = Spring + FMath::Sqrt(FMath::Max(0.0, GateHalf * GateHalf - Y * Y)) - 4.0;
				if (TopY <= Bottom + 20.0) { continue; }
				Out.AddBeam(G(Xp, Y, Bottom), G(Xp, Y, TopY), 4.5, Iron);
				Out.AddBeam(G(Xp, Y, Bottom - 22.0), G(Xp, Y, Bottom), 2.0, Iron);
			}
			for (const double Zr : { Bottom + 30.0, Bottom + 110.0 })
			{
				const double HalfW = FMath::Sqrt(FMath::Max(0.0, GateHalf * GateHalf - FMath::Square(Zr - Spring))) - 6.0;
				if (HalfW > 20.0) { Out.AddBeam(G(Xp, -HalfW, Zr), G(Xp, HalfW, Zr), 3.5, Iron); }
			}

			// Dos antorchas en la portada, a los lados del arco.
			for (const double Sg : { -1.0, 1.0 })
			{
				const FVector Base = G(Xout + 6.0, Sg * (GateHalf + 120.0), FloorZ + 300.0);
				const FVector TipP = Base + Fw * 38.0 + FVector(0.0, 0.0, 42.0);
				Out.AddBeam(Base, TipP, 5.0, FLinearColor(0.3f, 0.2f, 0.12f));
				Glow.AddBox(TipP + FVector(0.0, 0.0, 18.0), Fw, FVector(10.0, 10.0, 18.0), FLinearColor(1.f, 0.55f, 0.15f));
			}
		}
		// Antorchas dentro, a 3,2 m del suelo: palo y llama.
		for (int32 t = 0; t < 4; ++t)
		{
			const double A = FMath::Atan2(DoorDir.Y, DoorDir.X) + PI * 0.25 + HALF_PI * t;
			const FVector2D Q = C + Dir(A) * (Ri - 35.0);
			const FVector Base(Q.X, Q.Y, FloorZ + 320.0);
			const FVector2D In = -Dir(A);
			const FVector TipP = Base + FVector(In.X * 35.0, In.Y * 35.0, 45.0);
			Out.AddBeam(Base, TipP, 5.0, FLinearColor(0.3f, 0.2f, 0.12f));
			Glow.AddBox(TipP + FVector(0.0, 0.0, 18.0), FVector(In.X, In.Y, 0.0), FVector(10.0, 10.0, 18.0), FLinearColor(1.f, 0.55f, 0.15f));
		}
		TNProcAddTowerBanner(Cloth, Open, C, Ri, Ro, ParTop, Banner);
	}

	/** Amarillo y rojo de los avisos de parkour (chevrones, bandas y carteles). */
	const FLinearColor TNProcWarnYellow(1.f, 0.78f, 0.1f);
	const FLinearColor TNProcWarnRed(0.85f, 0.12f, 0.08f);

	/**
	 * Aviso de un hueco de panzazo (TNProcMap::EGapStyle::Dive), en el labio de llegada (-Dir): tres chevrones
	 * amarillos y rojos que apuntan al hueco y, fuera del camino, un cartel con «!» mirando a quien llega. El hueco es
	 * más largo que un salto corriendo: hay que coger carrerilla, saltar y hacer el panzazo en el aire.
	 */
	void TNProcAddDiveHint(FTNProcMeshBuffers& Paint, const TNProcMap::FFeature& F, double Inner)
	{
		const FVector2D D = F.Dir;
		const FVector2D Nn(-D.Y, D.X);
		const FVector2D Cg(F.Location.X, F.Location.Y);
		const double LipZ = F.Location.Z;
		const double Spread = FMath::Clamp(F.Width * 0.5 - 250.0, 80.0, 320.0);
		for (int32 k = 0; k < 3; ++k)
		{
			// Punta hacia el hueco; los brazos se abren hacia atrás.
			const double TipAlong = -(Inner + 60.0 + 95.0 * k);
			const FVector2D TipP = Cg + D * TipAlong;
			for (const double Sg : { -1.0, 1.0 })
			{
				const FVector2D ArmEnd = Cg + D * (TipAlong - 110.0) + Nn * (Sg * Spread);
				const FVector2D Arm = ArmEnd - TipP;
				Paint.AddBox(FVector((TipP + ArmEnd) * 0.5, LipZ + 1.0), FVector(Arm, 0.0), FVector(Arm.Size() * 0.5 + 10.0, 14.0, 1.5),
					(k % 2) ? TNProcWarnRed : TNProcWarnYellow);
			}
		}
		// Cartel: poste, tablero amarillo y «!» rojo en la cara que mira a quien llega.
		const FVector2D SignP = Cg - D * (Inner + 300.0) + Nn * (F.Width * 0.5 - 130.0);
		TNProcAddCylinder(Paint, FVector(SignP, LipZ), FVector(SignP, LipZ + 205.0), 7.0, 6.0, 6, FLinearColor(0.35f, 0.25f, 0.15f));
		Paint.AddBox(FVector(SignP, LipZ + 165.0), FVector(Nn, 0.0), FVector(55.0, 5.0, 45.0), TNProcWarnYellow);
		const FVector2D Face = SignP - D * 6.5;
		Paint.AddBox(FVector(Face, LipZ + 176.0), FVector(Nn, 0.0), FVector(7.0, 1.5, 22.0), TNProcWarnRed);
		Paint.AddBox(FVector(Face, LipZ + 138.0), FVector(Nn, 0.0), FVector(7.0, 1.5, 7.0), TNProcWarnRed);
	}

	/** Banda de aviso amarilla y roja atravesada en un tablero (cima a 2 cm sobre TopZ). */
	void TNProcAddHazardBand(FTNProcMeshBuffers& Paint, const FVector& At, const FVector& Dir, double Hw, double TopZ)
	{
		const FVector Nn(-Dir.Y, Dir.X, 0.0);
		constexpr int32 Segs = 8;
		for (int32 k = 0; k < Segs; ++k)
		{
			const double Y = -Hw + Hw * 2.0 * (k + 0.5) / Segs;
			Paint.AddBox(FVector(At.X, At.Y, TopZ + 0.5) + Nn * Y, Dir, FVector(15.0, Hw / Segs, 1.5), (k % 2) ? TNProcWarnRed : TNProcWarnYellow);
		}
	}

	/**
	 * Borde astillado de un tablero roto (centro At, cota TopZ): piezas de largo desigual que asoman hacia Toward,
	 * de grueso Thick, y, sin colisión (Far), unos tablones colgando si bDangle.
	 */
	void TNProcAddBrokenEdge(FTNProcMeshBuffers& Solid, FTNProcMeshBuffers& Far, const FVector& At, const FVector& Toward, double Hw, double TopZ,
		double Thick, bool bDangle, const FLinearColor& Color, uint32 Seed)
	{
		const FVector T = Toward.GetSafeNormal2D();
		const FVector Nn(-T.Y, T.X, 0.0);
		constexpr int32 Stubs = 7;
		for (int32 k = 0; k < Stubs; ++k)
		{
			const double U = (TNProcTone(k, Seed) - 0.82f) / 0.36f;
			const double StubLen = 12.0 + 58.0 * U;
			const double Y = -Hw + Hw * 2.0 * (k + 0.5) / Stubs;
			const FVector P = FVector(At.X, At.Y, TopZ - 3.0 - Thick * 0.5) + Nn * Y + T * (StubLen * 0.5 - 4.0);
			Solid.AddBox(P, T, FVector(StubLen * 0.5, Hw / Stubs - 3.0, Thick * 0.5), Color * TNProcTone(k + 20, Seed));
		}
		if (!bDangle) { return; }
		for (int32 k = 0; k < 3; ++k)
		{
			const double U = (TNProcTone(k + 40, Seed) - 0.82f) / 0.36f;
			const double Y = Hw * (-0.7 + 0.7 * k) + 30.0 * (U - 0.5);
			const FVector A = FVector(At.X, At.Y, TopZ - 10.0) + Nn * Y + T * 6.0;
			const FVector B = A + T * (20.0 + 30.0 * U) - FVector(0.0, 0.0, 110.0 + 80.0 * U);
			Far.AddBeam(A, B, 7.0, Color * 0.8f);
		}
	}

	/**
	 * Tramo hundido de un puente colosal entre SA y SB (ahí no hay tablero): se cruza con parkour y todo lo pisable
	 * queda a menos de 50 cm bajo el tablero (las cajas de muerte empiezan 60 cm por debajo).
	 * - Kind 0: vigas de 60 cm en zigzag de lado a lado, con plataformas en los codos.
	 * - Kind 1: postes cuadrados de 1,1 m al tresbolillo, alternando la cima (-8 y -26 cm), a saltos de ~1,3 m.
	 * - Kind 2: dos cornisas de 60 cm por los bordes, cada una con un hueco de 1,8 m (una a un tercio y otra a dos
	 *   tercios) y un tablón atravesado en medio para cambiar de lado.
	 * Bordes astillados (bStone: sillares; si no, tablones que cuelgan) y bandas de aviso antes de cada borde.
	 * OutPrize (si no es nulo): lo pisable del medio del tramo, a su cota (el codo del medio de las vigas, la cima del
	 * poste del medio o el tablón atravesado), donde va una concha especial (ATN_ProcMapGenerator::SpawnShells).
	 */
	void TNProcAddBrokenSpan(FTNProcMeshBuffers& Solid, FTNProcMeshBuffers& Far, FTNProcMeshBuffers& Paint, const FTNPlankLine& Line,
		double SA, double SB, double TopZ, int32 Kind, bool bStone, const FLinearColor& Base, uint32 Seed, FVector* OutPrize = nullptr)
	{
		const double SpanLen = SB - SA;
		if (SpanLen < 400.0) { return; }
		auto Frame = [&Line](double S, FVector2D& OutP, FVector& OutDir, double& OutHw)
		{
			FVector P;
			Line.At(S, P, OutDir, OutHw);
			OutP = FVector2D(P.X, P.Y);
		};
		// Bordes y avisos.
		for (int32 e = 0; e < 2; ++e)
		{
			FVector2D P2;
			FVector Dir;
			double Hw = 0.0;
			Frame(e == 0 ? SA : SB, P2, Dir, Hw);
			const FVector Toward = e == 0 ? Dir : -Dir;
			TNProcAddBrokenEdge(Solid, Far, FVector(P2, TopZ), Toward, Hw, TopZ, bStone ? 28.0 : 10.0, !bStone, Base, Seed + 11u * static_cast<uint32>(e));
			FVector2D W2;
			FVector WDir;
			double WHw = 0.0;
			Frame(e == 0 ? SA - 90.0 : SB + 90.0, W2, WDir, WHw);
			TNProcAddHazardBand(Paint, FVector(W2, TopZ), WDir, WHw - 20.0, TopZ);
		}

		const FLinearColor Beams = bStone ? Base * 0.95f : Base * 1.05f;
		switch (Kind)
		{
			case 0:
			{
				const int32 Legs = FMath::Clamp(FMath::RoundToInt32(SpanLen / 380.0), 3, 5);
				const double Side0 = (Seed & 1u) ? 1.0 : -1.0;
				TArray<FVector> Knots;
				for (int32 k = 0; k <= Legs; ++k)
				{
					FVector2D P2;
					FVector Dir;
					double Hw = 0.0;
					Frame(FMath::Lerp(SA - 40.0, SB + 40.0, static_cast<double>(k) / Legs), P2, Dir, Hw);
					const double Lat = (k == 0 || k == Legs) ? 0.0 : Side0 * ((k % 2) ? 1.0 : -1.0) * FMath::Max(60.0, Hw - 90.0);
					Knots.Add(FVector(P2, TopZ) + FVector(-Dir.Y, Dir.X, 0.0) * Lat);
				}
				for (int32 k = 1; k <= Legs; ++k)
				{
					const FVector A = Knots[k - 1];
					const FVector B = Knots[k];
					Solid.AddBox((A + B) * 0.5 - FVector(0.0, 0.0, 14.0), B - A, FVector(FVector::Dist2D(A, B) * 0.5 + 20.0, 30.0, 10.0), Beams * TNProcTone(k, Seed));
				}
				// La plataforma del codo del medio (su cara de arriba, 4 cm bajo el tablero).
				if (OutPrize) { *OutPrize = Knots[Legs / 2] - FVector(0.0, 0.0, 4.0); }
				for (int32 k = 1; k < Legs; ++k)
				{
					const FVector K = Knots[k];
					const FVector Along = (Knots[k + 1] - Knots[k - 1]).GetSafeNormal2D();
					Solid.AddBox(K - FVector(0.0, 0.0, 16.0), Along, FVector(50.0, 50.0, 12.0), Beams * 1.1f);
					Far.AddBox(K - FVector(0.0, 0.0, 328.0), Along, FVector(12.0, 12.0, 300.0), Beams * 0.7f);
				}
				break;
			}
			case 1:
			{
				const int32 Count = FMath::Max(2, FMath::RoundToInt32((SpanLen - 350.0) / 205.0) + 1);
				const int32 Flip = static_cast<int32>(Seed & 1u);
				for (int32 k = 0; k < Count; ++k)
				{
					FVector2D P2;
					FVector Dir;
					double Hw = 0.0;
					Frame(FMath::Lerp(SA + 175.0, SB - 175.0, static_cast<double>(k) / (Count - 1)), P2, Dir, Hw);
					const double Lat = ((k + Flip) % 2 ? 1.0 : -1.0) * FMath::Min(65.0, Hw * 0.4);
					const FVector2D Pc = P2 + FVector2D(-Dir.Y, Dir.X) * Lat;
					const double Top = TopZ - ((k % 2) ? 26.0 : 8.0);
					const double Bottom = TopZ - 700.0;
					// La cima del poste del medio.
					if (OutPrize && k == Count / 2) { *OutPrize = FVector(Pc, Top); }
					Solid.AddBox(FVector(Pc, Top - 12.0), Dir, FVector(55.0, 55.0, 12.0), Beams * 1.1f * TNProcTone(k, Seed));
					Solid.AddBox(FVector(Pc, 0.5 * (Top - 24.0 + Bottom)), Dir, FVector(44.0, 44.0, 0.5 * (Top - 24.0 - Bottom)), Beams * 0.8f);
				}
				break;
			}
			default:
			{
				const double Flip = (Seed & 1u) ? -1.0 : 1.0;
				for (const double Side : { -1.0, 1.0 })
				{
					const double GapS = SA + SpanLen * (Side * Flip < 0.0 ? 1.0 / 3.0 : 2.0 / 3.0);
					const double Runs[2][2] = { { SA - 30.0, GapS - 90.0 }, { GapS + 90.0, SB + 30.0 } };
					int32 Piece = 0;
					for (const auto& Run : Runs)
					{
						for (double S = Run[0]; S < Run[1] - 1.0; S += 150.0, ++Piece)
						{
							const double Se = FMath::Min(S + 150.0, Run[1]);
							FVector2D P0, P1;
							FVector D0, D1;
							double H0 = 0.0, H1 = 0.0;
							Frame(S, P0, D0, H0);
							Frame(Se, P1, D1, H1);
							const FVector2D Mid = (P0 + P1) * 0.5 + FVector2D(-D0.Y, D0.X) * (Side * (0.5 * (H0 + H1) - 45.0));
							Solid.AddBox(FVector(Mid, TopZ - 16.0), D0, FVector(FVector2D::Distance(P0, P1) * 0.5 + 1.0, 30.0, 10.0), Beams * TNProcTone(Piece, Seed));
							Far.AddBox(FVector(Mid, TopZ - 60.0), D0, FVector(FVector2D::Distance(P0, P1) * 0.5 + 1.0, 18.0, 34.0), Beams * 0.7f);
						}
					}
				}
				// Tablón atravesado para cambiar de cornisa.
				FVector2D Pm;
				FVector Dm;
				double Hm = 0.0;
				Frame(SA + SpanLen * 0.5, Pm, Dm, Hm);
				Solid.AddBox(FVector(Pm, TopZ - 14.0), FVector(-Dm.Y, Dm.X, 0.0), FVector(Hm - 30.0, 25.0, 8.0), Beams * 1.2f);
				// El tablón atravesado del medio (su cara de arriba).
				if (OutPrize) { *OutPrize = FVector(Pm, TopZ - 6.0); }
				break;
			}
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Poza de las cascadas
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ProcMapGenerator::SlidePool(const TNProcMap::FFeature& F, FVector2D& OutCenter, double& OutRadius, double& OutZ, FVector2D& OutFoot, FVector2D& OutFlow) const
{
	// El mismo cálculo que hunde el terreno (TNProcMap::SlidePoolOf): agua a ras de suelo donde se aterriza.
	if (!TNProcMap::SlidePoolOf(Layout, F, OutCenter, OutRadius, OutZ, OutFoot, OutFlow))
	{
		OutCenter = OutFoot = FVector2D(F.Location.X, F.Location.Y);
		OutFlow = FVector2D(1.0, 0.0);
		OutRadius = 350.0;
		OutZ = F.Location.Z;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Materiales y colores
// ─────────────────────────────────────────────────────────────────────────────

UMaterialInterface* ATN_ProcMapGenerator::ResolveMaterial(UMaterialInterface* Preferred) const
{
	return Preferred ? Preferred : TNMaterials::VertexColor();
}

void ATN_ProcMapGenerator::ResolveBiomeColors(ETNProcBiome Biome, FLinearColor& Ground, FLinearColor& Path, FLinearColor& Rock, FLinearColor& Bed) const
{
	if (const UTN_ProcBiomeDataAsset* Asset = Settings ? Settings->FindBiome(Biome) : nullptr)
	{
		Ground = Asset->GroundColor;
		Path = Asset->PathColor;
		Rock = Asset->RockColor;
		Bed = Asset->BedColor;
		return;
	}
	TN_DefaultBiomeColors(Biome, Ground, Path, Rock, Bed);
}

// ─────────────────────────────────────────────────────────────────────────────
// Terreno
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ProcMapGenerator::BuildTerrain()
{
	using namespace TNProcMap;
	const double Spacing = Settings ? Settings->VertexSpacing : 150.0;
	const int32 TileQuads = Settings ? Settings->TileQuads : 48;
	const double Margin = Settings ? Settings->OuterMargin : 25000.0;
	const double Sea = Settings ? Settings->SeaExtent : 30000.0;

	LatticeSpacing = Spacing;
	LatticeOrigin = FVector2D(-Margin, -Margin);
	const int32 QuadsX = FMath::CeilToInt((Layout.WorldSizeX + 2.0 * Margin) / Spacing / TileQuads) * TileQuads;
	const int32 QuadsY = FMath::CeilToInt((Layout.WorldSize + Margin + Sea) / Spacing / TileQuads) * TileQuads;
	LatticeNX = QuadsX + 1;
	LatticeNY = QuadsY + 1;
	const int32 NumVerts = LatticeNX * LatticeNY;

	// ── Alturas en paralelo (lógica pura, filas disjuntas) ──────────────────
	TNProcMap::FTerrainBuilder Builder;
	Builder.Build(Layout, LatticeOrigin, Spacing, LatticeNX, LatticeNY);
	Heights.SetNumUninitialized(NumVerts);
	PathMask.SetNumUninitialized(NumVerts);
	const int32 RowsPerChunk = 16;
	const int32 NumChunks = (LatticeNY + RowsPerChunk - 1) / RowsPerChunk;
	ParallelFor(NumChunks, [&](int32 Chunk)
	{
		Builder.ComputeRows(Chunk * RowsPerChunk, FMath::Min(LatticeNY, (Chunk + 1) * RowsPerChunk), Heights, PathMask);
	});
	Builder.ExportPathEdgeDistance(PathDist);

	// ── Detalle fino (DetailSpacing) donde la malla gruesa se aparta del terreno: bordes de taludes, crestas ──
	TNProcMap::FTerrainDetailParams DetailParams;
	const double DetailSpacing = Settings ? Settings->DetailSpacing : 50.0;
	DetailParams.N = FMath::Clamp(FMath::RoundToInt(Spacing / FMath::Max(50.0, DetailSpacing)), 1, 3);
	DetailParams.NearError = FMath::Max(10.0, Settings ? static_cast<double>(Settings->DetailError) : 20.0);
	DetailParams.FarError = DetailParams.NearError * 3.0;
	TNProcMap::BuildTerrainDetail(TerrainDetail, Builder, Heights, PathDist, QuadsX, QuadsY, DetailParams,
		[](int32 Num, auto&& Body) { ParallelFor(Num, Body); });

	// ── Colores por bioma (el camino, con contraste fuerte frente a sus paredes) ──
	FLinearColor Ground[NumBiomes], PathC[NumBiomes], PathRaw[NumBiomes], Rock[NumBiomes], Bed[NumBiomes];
	for (int32 b = 0; b < NumBiomes; ++b)
	{
		ResolveBiomeColors(BiomeFromIndex(b), Ground[b], PathRaw[b], Rock[b], Bed[b]);
		PathC[b] = TNTrailColor(BiomeFromIndex(b), TNContrastPath(PathRaw[b], Ground[b], Rock[b]));
	}
	// La playa de la meta conserva su arena: desde 15 m antes de su primera muestra de orilla, el camino
	// vuelve al color del bioma.
	double ShoreStartY = 1e18;
	for (const FPathSample& S : Layout.Main)
	{
		if ((S.Flags & PathFlags::Shore) != 0) { ShoreStartY = S.P.Y; break; }
	}
	const double FinishX = Layout.EndPoint.X;

	UMaterialInterface* TerrainMat = ResolveMaterial(Settings ? Settings->TerrainMaterial.Get() : nullptr);
	const uint32 ColorSeed = Layout.Params.Seed ^ 0xC0105u;

	const int32 TilesX = QuadsX / TileQuads;
	const int32 TilesY = QuadsY / TileQuads;
	const TArray<FProcMeshTangent> NoTangents;

	// Datos de cada tesela en paralelo (con 1,5 m de resolución son millones de vértices); la
	// creación de los componentes va después, en el hilo de juego.
	struct FTileData
	{
		TArray<FVector> Verts;
		TArray<int32> Tris;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FLinearColor> Colors;
	};
	TArray<FTileData> TileData;
	TileData.SetNum(TilesX * TilesY);
	ParallelFor(TileData.Num(), [&](int32 TileIndex)
	{
		const int32 Tx = TileIndex % TilesX;
		const int32 Ty = TileIndex / TilesX;
		FTileData& T = TileData[TileIndex];
		// Malla de la tesela con su detalle fino (abanicos en las costuras, sin grietas entre teselas).
		TNProcMap::FTerrainTileMesh Mesh;
		TerrainDetail.BuildTileMesh(Tx, Ty, TileQuads, Heights, PathMask, Mesh);
		T.Verts = MoveTemp(Mesh.Verts);
		T.Tris = MoveTemp(Mesh.Tris);
		T.Normals = MoveTemp(Mesh.Normals);
		const int32 NumV = T.Verts.Num();
		T.UVs.Reserve(NumV); T.Colors.Reserve(NumV);
		{
			for (int32 v = 0; v < NumV; ++v)
			{
				const double H = T.Verts[v].Z;
				const FVector2D P(T.Verts[v].X, T.Verts[v].Y);
				const FVector& N = T.Normals[v];
				T.UVs.Add(P / 500.0);
				double W[NumBiomes];
				Layout.BiomeWeightsAt(P, W);
				FLinearColor G(0.f, 0.f, 0.f, 0.f), Pc(0.f, 0.f, 0.f, 0.f), Praw(0.f, 0.f, 0.f, 0.f), R(0.f, 0.f, 0.f, 0.f), Bd(0.f, 0.f, 0.f, 0.f);
				for (int32 b = 0; b < NumBiomes; ++b)
				{
					const float Wb = static_cast<float>(W[b]);
					G += Ground[b] * Wb; Pc += PathC[b] * Wb; Praw += PathRaw[b] * Wb; R += Rock[b] * Wb; Bd += Bed[b] * Wb;
				}
				const double Beach = TNProcMap::SmoothStep(ShoreStartY - 1500.0, ShoreStartY, P.Y) * TNProcMap::SmoothStep(16000.0, 9000.0, FMath::Abs(P.X - FinishX));
				Pc = TNProcLerpColor(Pc, Praw, static_cast<float>(Beach));
				const float Mask = Mesh.Masks[v] / 255.f;
				// Paredes en degradado por pendiente: suelo en lo llano, roca en los taludes (28-57°) y
				// roca cada vez más oscura en los tajos (57-81°), con estratos suaves por altura en lo
				// empinado para que se lea su forma.
				const float Steep = static_cast<float>(TNProcMap::SmoothStep(0.88, 0.55, N.Z));
				const float Cliff = static_cast<float>(TNProcMap::SmoothStep(0.55, 0.15, N.Z));
				FLinearColor Col = TNProcLerpColor(G, R, Steep);
				Col = Col * FMath::Lerp(1.f, 0.62f, Cliff);
				const double Strata = FMath::Sin(H / 170.0 + 1.3 * TNProcMap::Noise2(ColorSeed + 7u, P.X / 3000.0, P.Y / 3000.0));
				Col = Col * (1.f + 0.07f * Steep * static_cast<float>(FMath::Clamp(Strata * 3.0, -1.0, 1.0)));
				// Suelo del camino, con una línea oscura en el pie del talud que marca su borde.
				Col = TNProcLerpColor(Col, Pc, Mask);
				Col = Col * (1.f - 2.0f * Mask * (1.f - Mask));
				Col = TNProcLerpColor(Col, Bd, static_cast<float>(TNProcMap::SmoothStep(30.0, -120.0, H)));
				const float Var = 0.9f + 0.2f * static_cast<float>(0.5 + 0.5 * TNProcMap::Noise2(ColorSeed, P.X / 700.0, P.Y / 700.0));
				Col = Col * Var;
				Col.A = Mask;
				T.Colors.Add(Col);
			}
		}
	});

	for (FTileData& T : TileData)
	{
		UProceduralMeshComponent* Tile = NewObject<UProceduralMeshComponent>(this, NAME_None, RF_Transient);
		Tile->SetupAttachment(RootComponent);
		Tile->bUseAsyncCooking = true;
		Tile->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		// Dato de primitiva 0 = 1: M_ProcTerrain aplica el relieve por normales y la textura del camino solo en las
		// teselas, no en las formaciones pintadas que comparten el material (su alfa de vértice no es la máscara).
		Tile->SetCustomPrimitiveDataFloat(0, 1.f);
		Tile->RegisterComponent();
		Tile->CreateMeshSection_LinearColor(0, T.Verts, T.Tris, T.Normals, T.UVs, T.Colors, NoTangents, true);
		if (TerrainMat) { Tile->SetMaterial(0, TerrainMat); }
		TerrainTiles.Add(Tile);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Agua
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ProcMapGenerator::BuildWater()
{
	// Plano de agua a nivel del mar sobre todo el mallado: el terreno lo tapa donde está por encima.
	if (BasicPlane)
	{
		WaterPlane = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		WaterPlane->SetupAttachment(RootComponent);
		WaterPlane->SetStaticMesh(BasicPlane);
		WaterPlane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WaterPlane->SetCastShadow(false);
		WaterPlane->RegisterComponent();
		const double SizeX = (LatticeNX - 1) * LatticeSpacing;
		const double SizeY = (LatticeNY - 1) * LatticeSpacing;
		WaterPlane->SetRelativeLocation(FVector(LatticeOrigin.X + SizeX * 0.5, LatticeOrigin.Y + SizeY * 0.5, TNProcMap::SeaLevel + 2.0));
		WaterPlane->SetRelativeScale3D(FVector(SizeX / 100.0, SizeY / 100.0, 1.0));
		if (UMaterialInterface* WaterMat = Settings ? Settings->WaterMaterial.Get() : nullptr)
		{
			WaterPlane->SetMaterial(0, WaterMat);
		}
		else
		{
			TNProcActors::Tint(WaterPlane, FLinearColor(0.05f, 0.3f, 0.5f));
		}
	}

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	// Volumen nadable: rectángulos codiciosos sobre celdas de 4x4 quads con agua profunda.
	const int32 Step = 4;
	const int32 CW = (LatticeNX - 1) / Step;
	const int32 CH = (LatticeNY - 1) / Step;
	TArray<float> CellMin;
	CellMin.SetNumUninitialized(CW * CH);
	for (int32 cy = 0; cy < CH; ++cy)
	{
		for (int32 cx = 0; cx < CW; ++cx)
		{
			float MinH = TNumericLimits<float>::Max();
			for (int32 y = cy * Step; y <= (cy + 1) * Step; ++y)
			{
				for (int32 x = cx * Step; x <= (cx + 1) * Step; ++x)
				{
					MinH = FMath::Min(MinH, Heights[y * LatticeNX + x]);
				}
			}
			CellMin[cy * CW + cx] = MinH;
		}
	}
	const float DeepEnough = -90.f;
	TArray<uint8> Covered;
	Covered.Init(0, CW * CH);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = this;
	ATN_ProcWaterVolume* Water = World->SpawnActor<ATN_ProcWaterVolume>(ATN_ProcWaterVolume::StaticClass(), GetActorTransform(), Params);
	if (!Water)
	{
		return;
	}
	SpawnedActors.Add(Water);

	const double CellSize = Step * LatticeSpacing;
	for (int32 cy = 0; cy < CH; ++cy)
	{
		for (int32 cx = 0; cx < CW; ++cx)
		{
			const int32 Idx = cy * CW + cx;
			if (Covered[Idx] || CellMin[Idx] > DeepEnough) { continue; }
			int32 W = 1;
			float MinH = CellMin[Idx];
			while (cx + W < CW && !Covered[cy * CW + cx + W] && CellMin[cy * CW + cx + W] <= DeepEnough)
			{
				MinH = FMath::Min(MinH, CellMin[cy * CW + cx + W]);
				++W;
			}
			int32 H = 1;
			bool bGrow = true;
			while (bGrow && cy + H < CH)
			{
				for (int32 k = 0; k < W; ++k)
				{
					const int32 J = (cy + H) * CW + cx + k;
					if (Covered[J] || CellMin[J] > DeepEnough) { bGrow = false; break; }
				}
				if (bGrow)
				{
					for (int32 k = 0; k < W; ++k) { MinH = FMath::Min(MinH, CellMin[(cy + H) * CW + cx + k]); }
					++H;
				}
			}
			for (int32 yy = 0; yy < H; ++yy)
			{
				for (int32 xx = 0; xx < W; ++xx) { Covered[(cy + yy) * CW + cx + xx] = 1; }
			}
			const double Bottom = static_cast<double>(MinH) - 150.0;
			// El volumen se mide por el centro de la cápsula: techo a nivel del mar.
			const double Top = TNProcMap::SeaLevel;
			const FVector2D Min2 = LatticeOrigin + FVector2D(cx * CellSize, cy * CellSize);
			const FVector2D Size2(W * CellSize, H * CellSize);
			const FVector CenterMap(Min2.X + Size2.X * 0.5, Min2.Y + Size2.Y * 0.5, (Bottom + Top) * 0.5);
			Water->AddWaterBox(MapToWorld(CenterMap), FVector(Size2.X * 0.5, Size2.Y * 0.5, (Top - Bottom) * 0.5));
		}
	}
	UE_LOG(LogTortunabo, Log, TEXT("[ProcMap] Agua nadable: %d cajas."), Water->NumBoxes());
}

// ─────────────────────────────────────────────────────────────────────────────
// Estructuras
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ProcMapGenerator::BuildStructures()
{
	using namespace TNProcMap;
	// Painted: formaciones del camino (con colisión); PaintedFar: hitos lejanos (sin ella). Color de vértice.
	FTNProcMeshBuffers Rock, Wood, Lava, SlideWater, Foliage, Painted, PaintedFar;
	// Decoración de las cuevas: lo que brilla (color de vértice emisivo) y los haces de luz (translúcido).
	FTNProcMeshBuffers Glow, Beam;
	// Piezas que Arte puede sustituir (Docs/Arte_Assets.md): todas en el espacio del mapa, el de StructureMesh y DecorMesh.
	// Clear quita sus mallas de arte por este mismo grupo.
	TNArt::FPieceLog ArtLog(TEXT("ProcMapStructures"));
	// Luz dentro de las torres huecas: cálida junto al suelo (antorchas) y fría bajo el forjado (la del hueco).
	auto AddTowerLights = [this](const FFeature& T)
	{
		const FVector2D C(T.Location.X, T.Location.Y);
		const struct { double Z; float Lumens; float Radius; FLinearColor Color; } Defs[2] = {
			{ T.Target.Z + 420.0, 6000.f, 2600.f, FLinearColor(1.f, 0.62f, 0.3f) },
			{ T.Height - 350.0, 3500.f, 2200.f, FLinearColor(0.65f, 0.78f, 1.f) } };
		for (const auto& Def : Defs)
		{
			UPointLightComponent* Light = NewObject<UPointLightComponent>(this, NAME_None, RF_Transient);
			Light->SetupAttachment(RootComponent);
			Light->SetRelativeLocation(FVector(C, Def.Z));
			Light->SetIntensityUnits(ELightUnits::Lumens);
			Light->SetIntensity(Def.Lumens);
			Light->SetAttenuationRadius(Def.Radius);
			Light->SetLightColor(Def.Color);
			Light->SetCastShadows(false);
			Light->RegisterComponent();
			CaveLights.Add(Light);
		}
	};
	TArray<FVector> CaveFlames, CaveMotes;
	const FLinearColor RockColor(0.32f, 0.29f, 0.26f);
	const FLinearColor WoodColor(0.45f, 0.3f, 0.16f);
	const TArray<FPathSample>& M = Layout.Main;

	// ── Puentes colosales: estilo según el bioma del cruce ─────
	// Colgante: tablones sobre dos largueros, barandilla de cuerda y, en cada apoyo (borde de torre o
	// pilar de roca), mástiles de los que cuelgan los cables principales con sus péndolas. Viaducto:
	// losas y pretiles de piedra sobre arcos rebajados entre los apoyos. Caballete: tablones y barandilla
	// de madera sobre caballetes de vigas hasta el suelo. Hierro: planchas, barandilla y pórticos de
	// hierro con cadenas en lugar de cables.
	const FLinearColor RopeColor(0.52f, 0.42f, 0.27f);
	const FLinearColor IronColor(0.16f, 0.16f, 0.18f);
	for (int32 c = 0; c < Layout.Crossings.Num(); ++c)
	{
		const FCrossing& C = Layout.Crossings[c];
		if (C.Type != ETNProcCrossingType::Bridge) { continue; }
		const FRouteStep& High = Layout.Route[C.HighStep];
		FTNPlankLine Line;
		for (int32 i = High.FirstSample; i <= High.LastSample; ++i)
		{
			Line.Add(FVector(M[i].P, C.TopZ), M[i].Width * 0.5);
		}
		const double TowerR = Layout.Params.TowerRadius;
		const double S0 = TowerR - 150.0;
		const double S1 = Line.Length() - TowerR + 150.0;
		if (S1 <= S0) { continue; }
		const uint32 Seed = Layout.Params.Seed ^ (0xB21D6u + static_cast<uint32>(c));
		const ETNProcBiome CrossBiome = Layout.Modules[C.Module].Biome;
		const ETNBridgeStyle Style = TNProcMap::BridgeStyleOf(Layout, c);
		FLinearColor Gc, Pcc, RockCc, Bdc;
		ResolveBiomeColors(CrossBiome, Gc, Pcc, RockCc, Bdc);
		const bool bSandy = CrossBiome == ETNProcBiome::Desert || CrossBiome == ETNProcBiome::Beach;
		const FLinearColor StoneC = bSandy ? TNProcLerpColor(RockCc, Gc, 0.6f) * 1.12f : TNProcLerpColor(RockCc, Gc, 0.15f) * 1.3f;
		// Torres: la de entrada, hueca (puerta y géiser dentro); la de salida, forrada de sillería con almenas.
		{
			static const FLinearColor TowerBanners[3] = { FLinearColor(0.62f, 0.08f, 0.07f), FLinearColor(0.1f, 0.18f, 0.55f), FLinearColor(0.75f, 0.55f, 0.08f) };
			auto TowerGround = [this](const FVector2D& Q) { return TerrainHeightMap(Q); };
			for (const FFeature& Ft : Layout.Features)
			{
				if (Ft.Type != EFeature::Tower || Ft.Aux != c) { continue; }
				const uint32 Ts = Seed ^ static_cast<uint32>(Ft.PathIndex * 2654435761u);
				if (IsHollowTower(Ft))
				{
					TNArt::FPieceScope TowerPiece(ArtLog, TN_ART("ProcMap.Tower.Hollow"), TNHollowTowerArtPivot(Ft), { &Rock, &Glow, &Foliage });
					TNProcAddHollowTower(Rock, Glow, Foliage, Layout, Ft, TowerGround, StoneC, TowerBanners[c % 3], Ts);
					AddTowerLights(Ft);
				}
				else
				{
					TNArt::FPieceScope TowerPiece(ArtLog, TN_ART("ProcMap.Tower.Solid"), TNSolidTowerArtPivot(Ft), { &Rock, &Foliage });
					TNProcAddWallTower(Rock, Foliage, Layout, Ft, TowerGround, StoneC, TowerBanners[c % 3], Ts);
				}
			}
		}
		// Plaza del puente (piedra o hierro): el tablero y sus pretiles se cortan donde la entra el borde.
		const FFeature* Plaza = nullptr;
		for (const FFeature& Fp : Layout.Features) { if (Fp.Type == EFeature::BridgePlaza && Fp.Aux == c) { Plaza = &Fp; } }
		double CutA = S1, CutB = S1;
		if (Plaza)
		{
			const double Sc = Plaza->Length - M[High.FirstSample].S;
			const double Hw = M[Plaza->PathIndex].Width * 0.5;
			const double Rc = FMath::Sqrt(FMath::Max(0.0, FMath::Square(Plaza->Radius - 20.0) - FMath::Square(Hw + 48.0)));
			CutA = FMath::Clamp(Sc - Rc, S0, S1);
			CutB = FMath::Clamp(Sc + Rc, S0, S1);
		}
		// Tramos hundidos (puentes de más de 42 m): de uno a tres, uno por cada 22 m de tablero útil, repartidos a lo largo
		// del puente y de tipos distintos: el primero de 11-15 m y los demás de 9-13 m sin tablero, que se cruzan con parkour
		// (vigas en zigzag, postes o cornisas; ver TNProcAddBrokenSpan). Entre dos, al menos 7 m de tablero entero para
		// aterrizar y coger carrerilla. Solo donde debajo no hay nada en 9 m (ni pilas ni torres), todo el tramo queda sobre
		// cajas de muerte y no hay nada del recorrido cerca (plaza, huevos, recompensas).
		struct FTNDeckBreak { double A = 0.0; double B = 0.0; int32 Kind = 0; };
		TArray<FTNDeckBreak> Breaks;
		const uint32 BreakHash = HashCell(Layout.Params.Seed ^ 0x7B0Bu, c, 3);
		if (S1 - S0 > 4200.0)
		{
			auto OverKillBox = [&](const FVector2D& Q)
			{
				for (const FKillBox& K : Layout.KillBoxes)
				{
					if (FMath::Abs(K.Center.Z - (C.TopZ - 655.0)) > 1.0) { continue; }
					const FVector2D Rel = Q - FVector2D(K.Center.X, K.Center.Y);
					if (FMath::Abs(FVector2D::DotProduct(Rel, K.Dir)) <= K.Half.X - 50.0
						&& FMath::Abs(FVector2D::DotProduct(Rel, FVector2D(-K.Dir.Y, K.Dir.X))) <= K.Half.Y - 300.0)
					{
						return true;
					}
				}
				return false;
			};
			auto SpanClear = [&](double Ba, double Bb)
			{
				for (double S = Ba - 150.0; S <= Bb + 150.0; S += 100.0)
				{
					FVector P, Dir;
					double Hw = 0.0;
					Line.At(S, P, Dir, Hw);
					const FVector2D Q(P.X, P.Y);
					if (TerrainHeightMap(Q) > C.TopZ - 900.0 || !OverKillBox(Q)) { return false; }
					for (const FFeature& Fo : Layout.Features)
					{
						if (Fo.Type == EFeature::Deck || Fo.Type == EFeature::Tower || Fo.Type == EFeature::DeckPillar) { continue; }
						if (FMath::Abs(Fo.Location.Z - C.TopZ) > 600.0) { continue; }
						const double Reach = Hw + (Fo.Type == EFeature::BridgePlaza ? Fo.Radius + 500.0 : 500.0);
						if (FVector2D::DistSquared(Q, FVector2D(Fo.Location.X, Fo.Location.Y)) < Reach * Reach) { return false; }
					}
				}
				return true;
			};
			constexpr double BreakSep = 700.0;
			const double Lo = S0 + 600.0;
			const double Hi = S1 - 600.0;
			const int32 Want = FMath::Clamp(FMath::FloorToInt32((Hi - Lo) / 2200.0), 1, 3);
			const double Slot = (Hi - Lo) / Want;
			for (int32 k = 0; k < Want; ++k)
			{
				const uint32 Hk = HashCell(BreakHash, k, 5);
				const double BreakLen = (k == 0 ? 1100.0 : 900.0) + 400.0 * static_cast<double>((k == 0 ? BreakHash >> 8 : Hk) & 0xFF) / 255.0;
				// Centrado en su parte del tablero y, si ahí no cabe, desplazado a saltos de 3 m hasta media parte.
				const double Center = Lo + Slot * (k + 0.5);
				for (int32 Try = 0; Try <= FMath::FloorToInt32(Slot / 600.0) * 2; ++Try)
				{
					const double Ba = Center + ((Try % 2) ? -1.0 : 1.0) * 300.0 * ((Try + 1) / 2) - BreakLen * 0.5;
					if (Ba < Lo || Ba + BreakLen > Hi) { continue; }
					bool bFar = true;
					for (const FTNDeckBreak& Other : Breaks) { bFar &= Ba > Other.B + BreakSep || Ba + BreakLen < Other.A - BreakSep; }
					if (bFar && SpanClear(Ba, Ba + BreakLen))
					{
						Breaks.Add(FTNDeckBreak{ Ba, Ba + BreakLen, static_cast<int32>(((BreakHash >> 16) + static_cast<uint32>(k)) % 3u) });
						break;
					}
				}
			}
			Breaks.Sort([](const FTNDeckBreak& X0, const FTNDeckBreak& X1) { return X0.A < X1.A; });
		}
		// Reparte un tramo [Pa, Pb] del tablero en los trozos que quedan fuera de los hundidos.
		auto Pieces = [&](double Pa, double Pb, auto&& Emit)
		{
			double Cur = Pa;
			for (const FTNDeckBreak& Br : Breaks)
			{
				if (Br.B <= Cur || Br.A >= Pb) { continue; }
				if (Br.A > Cur) { Emit(Cur, Br.A); }
				Cur = FMath::Max(Cur, Br.B);
			}
			if (Pb > Cur) { Emit(Cur, Pb); }
		};
		// Pieza de arte: el tablero con sus barandillas y los tramos hundidos, del principio al final (+X hacia el final;
		// escala X 1 = 100 m).
		FVector DeckFrom, DeckTo, DeckDir;
		double DeckHw = 0.0;
		Line.At(S0, DeckFrom, DeckDir, DeckHw);
		Line.At(S1, DeckTo, DeckDir, DeckHw);
		const int32 DeckPiece = ArtLog.Begin(TNBridgeDeckArt(Style), TNProcArtSpanPivot(DeckFrom, DeckTo, 10000.0), { &Wood, &Painted, &PaintedFar });
		switch (Style)
		{
			case ETNBridgeStyle::Stone:
				Pieces(S0, CutA, [&](double Pa, double Pb) { TNProcAddStoneDeck(Painted, Line, Pa, Pb, StoneC, Seed); });
				if (CutB < S1) { Pieces(CutB, S1, [&](double Pa, double Pb) { TNProcAddStoneDeck(Painted, Line, Pa, Pb, StoneC, Seed + 1u); }); }
				break;
			case ETNBridgeStyle::Trestle:
				Pieces(S0, S1, [&](double Pa, double Pb)
				{
					TNProcAddPlanks(Wood, Line, Pa, Pb, WoodColor * 1.1f, Seed);
					TNProcAddRigidRails(Wood, Line, Pa, Pb, 250.0, 100.0, 6.0, WoodColor * 0.7f);
				});
				break;
			case ETNBridgeStyle::Iron:
				Pieces(S0, S1, [&](double Pa, double Pb) { TNProcAddPlanks(Painted, Line, Pa, Pb, IronColor * 1.6f, Seed); });
				Pieces(S0, CutA, [&](double Pa, double Pb) { TNProcAddRigidRails(Painted, Line, Pa, Pb, 200.0, 105.0, 4.0, IronColor); });
				if (CutB < S1) { Pieces(CutB, S1, [&](double Pa, double Pb) { TNProcAddRigidRails(Painted, Line, Pa, Pb, 200.0, 105.0, 4.0, IronColor); }); }
				break;
			case ETNBridgeStyle::Rope:
			default:
				Pieces(S0, S1, [&](double Pa, double Pb)
				{
					TNProcAddPlanks(Wood, Line, Pa, Pb, WoodColor, Seed);
					TNProcAddRopeRails(Wood, Line, Pa, Pb, 300.0, 0.0, 105.0, WoodColor * 0.65f, RopeColor);
				});
				break;
		}
		for (int32 k = 0; k < Breaks.Num(); ++k)
		{
			const FTNDeckBreak& Br = Breaks[k];
			const bool bWoodDeck = Style == ETNBridgeStyle::Trestle || Style == ETNBridgeStyle::Rope;
			const FLinearColor DeckC = Style == ETNBridgeStyle::Stone ? StoneC : (Style == ETNBridgeStyle::Iron ? IronColor * 1.6f : WoodColor);
			FVector Prize = FVector::ZeroVector;
			TNProcAddBrokenSpan(bWoodDeck ? Wood : Painted, PaintedFar, Painted, Line, Br.A, Br.B, C.TopZ, Br.Kind,
				Style == ETNBridgeStyle::Stone, DeckC, Seed ^ (0x5EEDu + static_cast<uint32>(k) * 977u), &Prize);
			if (!Prize.IsZero())
			{
				// Concha especial en el medio del tramo (la pone SpawnShells en el servidor); se va a por ella desde 4 m antes.
				FVector StandP, StandDir;
				double StandHw = 0.0;
				Line.At(FMath::Max(0.0, Br.A - 400.0), StandP, StandDir, StandHw);
				FBrokenSpanPrize SpanPrize;
				SpanPrize.Point = Prize;
				SpanPrize.Stand = FVector(StandP.X, StandP.Y, C.TopZ);
				SpanPrize.Facing = FVector2D(StandDir.X, StandDir.Y);
				SpanPrize.Crossing = c;
				SpanPrize.Length = Br.B - Br.A;
				BrokenSpanPrizes.Add(SpanPrize);
			}
			UE_LOG(LogTortunabo, Log, TEXT("[ProcMap] Cruce %d: tramo hundido de %.0f m (tipo %d, %d de %d)."), c, (Br.B - Br.A) / 100.0, Br.Kind, k + 1, Breaks.Num());
		}
		ArtLog.End(DeckPiece);

		if (Plaza)
		{
			TNProcAddBridgePlaza(Painted, Glow, SlideWater, *Plaza, M[Plaza->PathIndex].Width * 0.5, Style == ETNBridgeStyle::Iron, StoneC, IronColor, &ArtLog);
		}

		// Apoyos: bordes de las torres y pilares de roca de este cruce.
		TArray<double> Supports = { S0 };
		for (const FFeature& F : Layout.Features)
		{
			if (F.Type == EFeature::DeckPillar && F.Aux == c)
			{
				Supports.Add(M[F.PathIndex].S - M[High.FirstSample].S);
			}
		}
		Supports.Add(S1);
		Supports.Sort();
		if (Style == ETNBridgeStyle::Stone || Style == ETNBridgeStyle::Trestle)
		{
			// Arcos entre apoyos (desde el borde de cada pilar) o caballetes cada ~9 m, bajo el tablero.
			// Una pila o un caballete que caería sobre otro camino (el de abajo del cruce, una rama, una
			// cueva) no se pone: el arco se une al siguiente y salva el camino.
			const double PillarR = 450.0;
			auto GroundAt = [this](const FVector2D& Q) { return TerrainHeightMap(Q); };
			auto OverPath = [&](const FVector2D& Q, double R)
			{
				auto Near = [&](const TArray<FPathSample>& Arr, int32 SkipFrom, int32 SkipTo)
				{
					for (int32 i = 0; i < Arr.Num(); ++i)
					{
						if (i >= SkipFrom && i <= SkipTo) { continue; }
						const double Reach = R + Arr[i].Width * 0.5 + 300.0;
						if (FVector2D::DistSquared(Arr[i].P, Q) < Reach * Reach) { return true; }
					}
					return false;
				};
				if (Near(M, High.FirstSample - 2, High.LastSample + 2)) { return true; }
				for (const FBranch& Br : Layout.Branches) { if (Near(Br.Samples, INDEX_NONE, INDEX_NONE)) { return true; } }
				return false;
			};
			for (int32 k = 1; k < Supports.Num(); ++k)
			{
				const double A = Supports[k - 1] + (k - 1 > 0 ? PillarR : 0.0);
				const double B = Supports[k] - (k < Supports.Num() - 1 ? PillarR : 0.0);
				if (Style == ETNBridgeStyle::Stone)
				{
					// Acueducto: arcos de hasta ~36 m separados por pilas de sillería que bajan hasta el suelo.
					const int32 NArch = FMath::Max(1, FMath::CeilToInt32((B - A) / 3600.0));
					constexpr double PierHalf = 180.0;
					TArray<double> Piers;
					for (int32 a = 1; a < NArch; ++a)
					{
						const double Sp = FMath::Lerp(A, B, static_cast<double>(a) / NArch);
						FVector Pp, Dp;
						double Hwp = 0.0;
						Line.At(Sp, Pp, Dp, Hwp);
						if (!OverPath(FVector2D(Pp.X, Pp.Y), Hwp + 85.0)) { Piers.Add(Sp); }
					}
					double Prev = A;
					for (int32 a = 0; a <= Piers.Num(); ++a)
					{
						const double Next = a < Piers.Num() ? Piers[a] : B;
						const double ArchA = Prev + (a > 0 ? PierHalf : 0.0);
						const double ArchB = Next - (a < Piers.Num() ? PierHalf : 0.0);
						// Pieza de arte: cada arco, del arranque a la otra pila a la cota del tablero (+X hacia la otra pila;
						// escala X 1 = 36 m).
						FVector ArchFrom, ArchTo, ArchDir;
						double ArchHw = 0.0;
						Line.At(ArchA, ArchFrom, ArchDir, ArchHw);
						Line.At(ArchB, ArchTo, ArchDir, ArchHw);
						TNArt::FPieceScope ArchPiece(ArtLog, TN_ART("ProcMap.Bridge.StoneArch"), TNProcArtSpanPivot(ArchFrom, ArchTo, 3600.0), { &PaintedFar });
						TNProcAddDeckArch(PaintedFar, Line, ArchA, ArchB, C.TopZ, StoneC, GroundAt);
						Prev = Next;
					}
					for (const double Sp : Piers)
					{
						FVector Pp, Dp;
						double Hwp = 0.0;
						Line.At(Sp, Pp, Dp, Hwp);
						const double G0 = TerrainHeightMap(FVector2D(Pp.X, Pp.Y)) - 80.0;
						const double Top = C.TopZ - 32.0;
						if (Top - G0 < 200.0) { continue; }
						// Pieza de arte: centro de la cima de la pila (bajo el tablero), +X a lo largo del puente; escala 1 = 600 de
						// ancho y 2000 hasta el pie.
						TNArt::FPieceScope PierPiece(ArtLog, TN_ART("ProcMap.Bridge.StonePier"),
							TNProcArtPivot(FVector(Pp.X, Pp.Y, Top), FVector2D(Dp.X, Dp.Y), FVector(1.0, (Hwp + 45.0) / 300.0, (Top - G0) / 2000.0)), { &PaintedFar });
						PaintedFar.AddBox(FVector(Pp.X, Pp.Y, 0.5 * (G0 + Top)), Dp, FVector(PierHalf, Hwp + 45.0, 0.5 * (Top - G0)), StoneC * 0.9f);
						PaintedFar.AddBox(FVector(Pp.X, Pp.Y, G0 + 150.0), Dp, FVector(PierHalf + 40.0, Hwp + 85.0, 150.0), StoneC * 0.82f);
					}
					continue;
				}
				const int32 Bents = FMath::Max(1, FMath::FloorToInt32((B - A) / 900.0));
				for (int32 b = 1; b <= Bents; ++b)
				{
					const double S = FMath::Lerp(A, B, static_cast<double>(b) / (Bents + 1));
					FVector P, Dir;
					double Hw = 0.0;
					Line.At(S, P, Dir, Hw);
					const double GroundZ = TerrainHeightMap(FVector2D(P.X, P.Y));
					if (OverPath(FVector2D(P.X, P.Y), Hw + (C.TopZ - GroundZ) * 0.14 + 30.0)) { continue; }
					// Pieza de arte: centro del caballete a la cota del tablero, +X a lo largo del puente; escala 1 = 600 de ancho
					// arriba y 2000 hasta el suelo.
					TNArt::FPieceScope BentPiece(ArtLog, TN_ART("ProcMap.Bridge.TrestleBent"),
						TNProcArtPivot(P, FVector2D(Dir.X, Dir.Y), FVector(1.0, Hw / 300.0, FMath::Max(1.0, C.TopZ - GroundZ) / 2000.0)), { &PaintedFar });
					TNProcAddTrestleBent(PaintedFar, P, Dir, Hw, GroundZ, WoodColor * 0.85f);
				}
			}
			continue;
		}
		const bool bIron = Style == ETNBridgeStyle::Iron;
		constexpr double MastH = 750.0;
		constexpr double Low = 130.0;
		for (int32 k = 0; k < Supports.Num(); ++k)
		{
			FVector P0, Dir0;
			double Hw0 = 0.0;
			Line.At(Supports[k], P0, Dir0, Hw0);
			{
				// Pieza de arte: el par de mástiles de cada apoyo (con su dintel en el de hierro), centro en el tablero, +X a lo
				// largo del puente; escala Y 1 = 300 del eje a cada mástil.
				TNArt::FPieceScope MastPiece(ArtLog, bIron ? TN_ART("ProcMap.Bridge.IronPortal") : TN_ART("ProcMap.Bridge.RopeMasts"),
					TNProcArtPivot(P0, FVector2D(Dir0.X, Dir0.Y), FVector(1.0, (Hw0 + 25.0) / 300.0, 1.0)), { &Painted, &Wood });
				for (const double Side : { -1.0, 1.0 })
				{
					const FVector Base = P0 + FVector(-Dir0.Y, Dir0.X, 0.0) * (Side * (Hw0 + 25.0));
					(bIron ? Painted : Wood).AddBox(Base + FVector(0.0, 0.0, MastH * 0.5 - 60.0), Dir0, FVector(16.0, 16.0, MastH * 0.5 + 60.0), bIron ? IronColor : WoodColor * 0.55f);
				}
				if (bIron)
				{
					// Pórtico: dintel de hierro entre los dos mástiles.
					const FVector Nn(-Dir0.Y, Dir0.X, 0.0);
					Painted.AddBeam(P0 - Nn * (Hw0 + 25.0) + FVector(0.0, 0.0, MastH - 40.0), P0 + Nn * (Hw0 + 25.0) + FVector(0.0, 0.0, MastH - 40.0), 14.0, IronColor);
				}
			}
			if (k == 0) { continue; }
			// Vano entre el apoyo anterior y este: cable parabólico y péndolas cada 3 m.
			const double A = Supports[k - 1];
			const double B = Supports[k];
			const int32 NumSeg = FMath::Max(2, FMath::RoundToInt((B - A) / 300.0));
			// Pieza de arte: los dos cables (o cadenas) del vano con sus péndolas, del apoyo anterior a este a la cota del
			// tablero (+X hacia este apoyo; escala X 1 = 30 m, Y 1 = 300 del eje a cada cable).
			FVector SpanFrom, SpanDir;
			double SpanHw = 0.0;
			Line.At(A, SpanFrom, SpanDir, SpanHw);
			const FTransform SpanPivot = TNProcArtSpanPivot(SpanFrom, P0, 3000.0);
			TNArt::FPieceScope SpanPiece(ArtLog, bIron ? TN_ART("ProcMap.Bridge.IronChains") : TN_ART("ProcMap.Bridge.RopeCables"),
				FTransform(SpanPivot.GetRotation(), SpanPivot.GetLocation(), FVector(SpanPivot.GetScale3D().X, (SpanHw + 25.0) / 300.0, 1.0)), { &PaintedFar, &Wood });
			for (const double Side : { -1.0, 1.0 })
			{
				FVector Prev = FVector::ZeroVector;
				for (int32 t = 0; t <= NumSeg; ++t)
				{
					const double U = static_cast<double>(t) / NumSeg;
					FVector P, Dir;
					double Hw = 0.0;
					Line.At(FMath::Lerp(A, B, U), P, Dir, Hw);
					const FVector Edge = P + FVector(-Dir.Y, Dir.X, 0.0) * (Side * (Hw + 25.0));
					const FVector Cable = Edge + FVector(0.0, 0.0, Low + (MastH - Low) * FMath::Square(2.0 * U - 1.0));
					if (bIron)
					{
						if (t > 0) { TNProcAddChain(PaintedFar, Prev, Cable, IronColor * 1.3f); }
						if (t > 0 && t < NumSeg) { PaintedFar.AddBeam(Cable, Edge + FVector(0.0, 0.0, 10.0), 1.8, IronColor); }
					}
					else
					{
						if (t > 0) { Wood.AddBeam(Prev, Cable, 4.5, RopeColor * 0.8f); }
						if (t > 0 && t < NumSeg) { Wood.AddBeam(Cable, Edge + FVector(0.0, 0.0, 10.0), 2.0, RopeColor); }
					}
					Prev = Cable;
				}
			}
		}
	}

	// ── Murallas: muro almenado bajo el adarve, puerta altísima y torres de sillería ──
	// De arenisca en los biomas de arena y de la piedra del bioma en el resto.
	{
		static const FLinearColor Banners[3] = { FLinearColor(0.62f, 0.08f, 0.07f), FLinearColor(0.1f, 0.18f, 0.55f), FLinearColor(0.75f, 0.55f, 0.08f) };
		auto Ground = [this](const FVector2D& Q) { return TerrainHeightMap(Q); };
		for (int32 c = 0; c < Layout.Crossings.Num(); ++c)
		{
			const FCrossing& C = Layout.Crossings[c];
			if (C.Type != ETNProcCrossingType::Wall) { continue; }
			const FRouteStep& High = Layout.Route[C.HighStep];
			FTNWallAxis Axis;
			Axis.Build(M, High.FirstSample, High.LastSample);
			const FFeature* Gate = nullptr;
			for (const FFeature& F : Layout.Features) { if (F.Type == EFeature::Gate && F.Aux == c) { Gate = &F; } }
			const ETNProcBiome Biome = Layout.Modules[C.Module].Biome;
			FLinearColor G, Pc, RockC, Bd;
			ResolveBiomeColors(Biome, G, Pc, RockC, Bd);
			const bool bSand = Biome == ETNProcBiome::Desert || Biome == ETNProcBiome::Beach;
			const FLinearColor Stone = bSand ? TNProcLerpColor(RockC, G, 0.6f) * 1.12f : TNProcLerpColor(RockC, G, 0.15f) * 1.3f;
			const double TowerR = Layout.Params.TowerRadius;
			const double Len = Axis.Length();
			const uint32 Seed = Layout.Params.Seed ^ (0x3A11u + static_cast<uint32>(c) * 7919u);
			// Adarve roto: los mordiscos de esta muralla (TNProcMap::BuildWallBreaches).
			TArray<const FFeature*> Breaches;
			int32 NumLedges = 0;
			for (const FFeature& F : Layout.Features)
			{
				if (F.Type != EFeature::WallBreach || F.Aux != c) { continue; }
				Breaches.Add(&F);
				NumLedges += WallBreachDims::KindOf(F) != EWallBreach::Gap ? 1 : 0;
			}
			// El cuerpo de la muralla entra 2 m en la torre de entrada (hueca: sin tocar su sala) y 4 m en la de salida.
			{
				// Pieza de arte: la muralla entera (con su puerta y los mordiscos), del principio al final del adarve a su cota
				// (+X hacia la torre de salida; escala X 1 = 100 m).
				FVector2D WallFrom, WallTo, WallT, WallN;
				double WallHw = 0.0;
				Axis.At(TowerR, WallFrom, WallT, WallN, WallHw);
				Axis.At(Len - TowerR, WallTo, WallT, WallN, WallHw);
				TNArt::FPieceScope WallPiece(ArtLog, TN_ART("ProcMap.Wall.Rampart"), TNProcArtSpanPivot(FVector(WallFrom, C.TopZ), FVector(WallTo, C.TopZ), 10000.0), { &Rock });
				TNProcAddWall(Rock, Axis, TowerR - 200.0, Len - TowerR + 400.0, TowerR, Len - TowerR, C.TopZ, Gate, Breaches, Ground, Stone, Seed);
			}
			if (Breaches.Num() > 0)
			{
				UE_LOG(LogTortunabo, Log, TEXT("[ProcMap] Cruce %d: adarve roto con %d mordiscos (%d brechas y %d cornisas)."), c, Breaches.Num(),
					Breaches.Num() - NumLedges, NumLedges);
			}
			// Barrera invisible sobre cada parapeto entero, de su cima a 9 m más arriba: nadie se sube a él (ni lanzado por un
			// compañero) para rodear una cornisa o saltar fuera de la muralla. Se corta donde el parapeto está roto: no tapa los
			// mordiscos ni deja andar por el aire, y quien sale por un mordisco cae en sus cajas de muerte. Solo frena a las
			// tortugas (la cámara la atraviesa).
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const double Sig = Side == 0 ? 1.0 : -1.0;
				TArray<FVector2D> BrokenRuns;
				for (const FFeature* Br : Breaches)
				{
					if (!WallBreachDims::CutsSide(*Br, Side)) { continue; }
					BrokenRuns.Add(FVector2D(Br->Target.X - WallBreachDims::ParapetBreak(*Br, Side, 0), Br->Target.Y + WallBreachDims::ParapetBreak(*Br, Side, 1)));
				}
				BrokenRuns.Sort([](const FVector2D& R0, const FVector2D& R1) { return R0.X < R1.X; });
				auto EmitRun = [&](double RunFrom, double RunTo)
				{
					constexpr double BarrierW = WallDims::Parapet + 60.0;
					constexpr double BarrierH = 900.0;
					const int32 Pieces = FMath::Max(1, FMath::CeilToInt32((RunTo - RunFrom) / 600.0));
					for (int32 k = 0; k < Pieces; ++k)
					{
						FVector2D P0, T0, N0, P1, T1, N1;
						double Hw0 = 0.0, Hw1 = 0.0;
						Axis.At(FMath::Lerp(RunFrom, RunTo, static_cast<double>(k) / Pieces), P0, T0, N0, Hw0);
						Axis.At(FMath::Lerp(RunFrom, RunTo, static_cast<double>(k + 1) / Pieces), P1, T1, N1, Hw1);
						// Desde la cara interior del parapeto hasta 60 cm más allá de la exterior.
						const FVector2D In0 = P0 + N0 * (Sig * Hw0);
						const FVector2D In1 = P1 + N1 * (Sig * Hw1);
						const FVector2D Along = (In1 - In0).GetSafeNormal();
						if (Along.IsNearlyZero()) { continue; }
						const FVector2D Outward = (N0 + N1).GetSafeNormal() * Sig;
						const FVector2D Mid = (In0 + In1) * 0.5 + Outward * (BarrierW * 0.5);
						UBoxComponent* Barrier = NewObject<UBoxComponent>(this, NAME_None, RF_Transient);
						Barrier->SetupAttachment(RootComponent);
						Barrier->SetBoxExtent(FVector(FVector2D::Distance(In0, In1) * 0.5 + 10.0, BarrierW * 0.5, BarrierH * 0.5));
						Barrier->SetCollisionProfileName(TEXT("InvisibleWall"));
						Barrier->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
						Barrier->SetHiddenInGame(true);
						Barrier->RegisterComponent();
						Barrier->SetRelativeLocationAndRotation(FVector(Mid, C.TopZ + WallDims::ParapetH - 2.0 + BarrierH * 0.5),
							FRotator(0.0, FMath::RadiansToDegrees(FMath::Atan2(Along.Y, Along.X)), 0.0));
						BoundaryWalls.Add(Barrier);
					}
				};
				double RunA = TowerR;
				for (const FVector2D& Run : BrokenRuns)
				{
					if (Run.X > RunA + 1.0) { EmitRun(RunA, Run.X); }
					RunA = FMath::Max(RunA, Run.Y);
				}
				if (Len - TowerR > RunA + 1.0) { EmitRun(RunA, Len - TowerR); }
			}
			for (const FFeature& F : Layout.Features)
			{
				if (F.Type == EFeature::Tower && F.Aux == c)
				{
					const uint32 Ts = Seed ^ static_cast<uint32>(F.PathIndex * 2654435761u);
					if (IsHollowTower(F))
					{
						TNArt::FPieceScope TowerPiece(ArtLog, TN_ART("ProcMap.Tower.Hollow"), TNHollowTowerArtPivot(F), { &Rock, &Glow, &Foliage });
						TNProcAddHollowTower(Rock, Glow, Foliage, Layout, F, Ground, Stone, Banners[c % 3], Ts);
						AddTowerLights(F);
					}
					else
					{
						TNArt::FPieceScope TowerPiece(ArtLog, TN_ART("ProcMap.Tower.Solid"), TNSolidTowerArtPivot(F), { &Rock, &Foliage });
						TNProcAddWallTower(Rock, Foliage, Layout, F, Ground, Stone, Banners[c % 3], Ts);
					}
				}
			}
		}
	}

	for (const FFeature& F : Layout.Features)
	{
		switch (F.Type)
		{
			case EFeature::Islet:
			{
				// Hasta el lecho de la laguna (≈ -10 m): vistas desde el agua no quedan flotando.
				// Pieza de arte: centro de la cima, +X a lo largo del camino; escala 1 = 10 x 10 m de planta.
				TNArt::FPieceScope IsletPiece(ArtLog, TN_ART("ProcMap.Lagoon.Islet"), TNProcArtPivot(F.Location, F.Dir, FVector(F.Length / 1000.0, F.Width / 1000.0, 1.0)),
					{ &Rock });
				Rock.AddPrism(F.Polygon, F.Location.Z, -1300.0, FLinearColor(0.55f, 0.5f, 0.38f));
				break;
			}
			case EFeature::Gap:
			{
				// Labios que reducen la zanja al hueco exacto: de madera (selva, playa, pueblos), de sillería
				// (desierto, roca) o de basalto (volcán), o de roca en el río de lava de una cueva (con la lava
				// 1,5 m por debajo, de pared a pared). Según el estilo, postes que parten el hueco en saltos
				// cortos, troncos de equilibrio de labio a labio o salto largo de panzazo con aviso.
				const bool bLava = IsLavaGap(F);
				const FVector2D D = F.Dir;
				const double Outer = F.Height * 0.5 + 150.0;
				const double Inner = F.Length * 0.5;
				const double HalfLen = (Outer - Inner) * 0.5;
				const bool bStoneLips = !bLava && (F.Biome == ETNProcBiome::Desert || F.Biome == ETNProcBiome::Rocky || F.Biome == ETNProcBiome::Volcanic);
				FLinearColor Gc, Pcc, RockCc, Bdc;
				ResolveBiomeColors(F.Biome, Gc, Pcc, RockCc, Bdc);
				const FLinearColor LipStone = F.Biome == ETNProcBiome::Volcanic ? FLinearColor(0.1f, 0.09f, 0.1f) : TNProcLerpColor(RockCc, Gc, 0.35f) * 1.15f;
				for (int32 Side = -1; Side <= 1; Side += 2)
				{
					const FVector2D Center2 = FVector2D(F.Location.X, F.Location.Y) + D * (Side * (Inner + HalfLen));
					// Pieza de arte: el labio de este lado, centro de su cara de arriba, +X hacia el hueco; escala 1 = 300 de largo
					// y 1000 de ancho.
					TNArt::FPieceScope LipPiece(ArtLog, bLava ? TN_ART("ProcMap.Gap.LavaLip") : (bStoneLips ? TN_ART("ProcMap.Gap.StoneLip") : TN_ART("ProcMap.Gap.WoodLip")),
						TNProcArtPivot(FVector(Center2, F.Location.Z), D * static_cast<double>(-Side), FVector(HalfLen * 2.0 / 300.0, F.Width / 1000.0, 1.0)),
						{ &Painted, &Wood, &Rock });
					if (bStoneLips)
					{
						// Sillares: bloques con junta a lo ancho y el borde del salto más claro.
						const FVector2D Nn = LeftNormal(D);
						const double Half = F.Width * 0.5;
						const int32 Blocks = FMath::Max(1, FMath::RoundToInt32(Half * 2.0 / 180.0));
						for (int32 k = 0; k < Blocks; ++k)
						{
							const double Y = -Half + Half * 2.0 * (k + 0.5) / Blocks;
							const FVector2D Bc = Center2 + Nn * Y;
							Painted.AddBox(FVector(Bc.X, Bc.Y, F.Location.Z - 60.0), FVector(D.X, D.Y, 0.0), FVector(HalfLen, Half / Blocks - 3.0, 60.0), LipStone * TNProcTone(k, static_cast<uint32>(F.PathIndex)));
						}
						const FVector2D EdgeC = FVector2D(F.Location.X, F.Location.Y) + D * (Side * (Inner + 12.0));
						Painted.AddBox(FVector(EdgeC.X, EdgeC.Y, F.Location.Z - 2.0), FVector(D.X, D.Y, 0.0), FVector(12.0, Half, 4.0), LipStone * 1.25f);
						continue;
					}
					(bLava ? Rock : Wood).AddBox(FVector(Center2.X, Center2.Y, F.Location.Z - (bLava ? 90.0 : 60.0)), FVector(D.X, D.Y, 0.0),
						FVector(HalfLen, F.Width * 0.5 + (bLava ? GapTrenchSideOf(F) : 0.0), bLava ? 90.0 : 60.0), bLava ? RockColor : WoodColor);
				}
				// Postes: columnas desde el fondo de la zanja con la cima a ras (±30 cm), del estilo del bioma.
				TArray<FGapPost> Posts;
				GapPostsOf(F, LerpD(FMath::Min(Layout.Params.GapMax, 200.0), Layout.Params.GapMax, Saturate(Layout.Params.Difficulty01)) * 0.8, Posts);
				const double Floor = GapFloorZ(F) - 60.0;
				for (int32 k = 0; k < Posts.Num(); ++k)
				{
					const FGapPost& Po = Posts[k];
					const FVector Base(Po.P, Floor);
					const FVector TopP(Po.P, Po.TopZ);
					const uint32 Ps = static_cast<uint32>(F.PathIndex) * 131u + static_cast<uint32>(k);
					// Pieza de arte: centro de la cima del poste, +X a lo largo del hueco; escala 1 = radio 50 y 500 hasta el fondo.
					TNArt::FPieceScope PostPiece(ArtLog, TNGapPostArt(F.Biome),
						TNProcArtPivot(TopP, D, FVector(Po.Radius / 50.0, Po.Radius / 50.0, FMath::Max(1.0, Po.TopZ - Floor) / 500.0)), { &Wood, &Painted });
					switch (F.Biome)
					{
						case ETNProcBiome::Jungle:
						case ETNProcBiome::Mangrove:
							// Tronco con corteza y un sombrero de musgo.
							TNProcAddCylinder(Wood, Base, TopP, Po.Radius * 1.08, Po.Radius, 9, WoodColor * (0.8f + 0.2f * TNProcTone(k, Ps)));
							TNProcAddCylinder(Painted, TopP - FVector(0.0, 0.0, 6.0), TopP + FVector(0.0, 0.0, 4.0), Po.Radius + 6.0, Po.Radius - 4.0, 9, FLinearColor(0.2f, 0.42f, 0.14f));
							break;
						case ETNProcBiome::Beach:
						case ETNProcBiome::Human:
							// Pilote de embarcadero con dos anillos de cuerda.
							TNProcAddCylinder(Wood, Base, TopP, Po.Radius, Po.Radius * 0.95, 8, WoodColor * 0.85f);
							for (const double Zr : { 40.0, 90.0 })
							{
								TNProcAddCylinder(Painted, TopP - FVector(0.0, 0.0, Zr + 6.0), TopP - FVector(0.0, 0.0, Zr - 6.0), Po.Radius + 5.0, Po.Radius + 5.0, 8, FLinearColor(0.72f, 0.62f, 0.42f));
							}
							break;
						case ETNProcBiome::Volcanic:
							TNRockMesh::TNRockHexColumn(Painted, Base, Po.Radius * 1.1, Po.TopZ - Floor, Ps, FLinearColor(0.12f, 0.11f, 0.12f));
							break;
						default:
							// Columna de sillería con capitel.
							TNProcAddCylinder(Painted, Base, TopP - FVector(0.0, 0.0, 22.0), Po.Radius * 0.85, Po.Radius * 0.85, 8, LipStone);
							Painted.AddBox(TopP - FVector(0.0, 0.0, 11.0), FVector(D.X, D.Y, 0.0), FVector(Po.Radius, Po.Radius, 11.0), LipStone * 1.15f);
							break;
					}
				}
				// Troncos de equilibrio de labio a labio (uno o dos), con la cima 10 cm sobre el suelo. En Supervivencia, un
				// hueco del catálogo lleva en su lugar el puente que se rompe (ATN_ProcBreakableBridge, #517).
				if (GapStyleOf(F) == EGapStyle::Beam && !IsSurvivalBreakableGap(static_cast<int32>(&F - Layout.Features.GetData())))
				{
					const FVector2D Nn = LeftNormal(D);
					const int32 Logs = F.Width > 1400.0 ? 2 : 1;
					for (int32 k = 0; k < Logs; ++k)
					{
						const double Y = Logs == 1 ? 0.0 : (k == 0 ? -1.0 : 1.0) * F.Width * 0.18;
						const FVector2D A2 = FVector2D(F.Location.X, F.Location.Y) + Nn * Y - D * (Inner + 70.0);
						const FVector2D B2 = FVector2D(F.Location.X, F.Location.Y) + Nn * Y + D * (Inner + 70.0);
						constexpr double LogR = 32.0;
						// Pieza de arte: centro del tronco, +X de labio a labio; escala X 1 = 600 de largo.
						TNArt::FPieceScope LogPiece(ArtLog, TN_ART("ProcMap.Gap.BalanceLog"),
							TNProcArtPivot(FVector((A2 + B2) * 0.5, F.Location.Z + 10.0 - LogR), D, FVector(FVector2D::Distance(A2, B2) / 600.0, 1.0, 1.0)), { &Wood });
						TNProcAddLog(Wood, FVector(A2, F.Location.Z + 10.0 - LogR), FVector(B2, F.Location.Z + 10.0 - LogR), LogR, static_cast<uint32>(F.PathIndex) + k, WoodColor * 0.9f, WoodColor * 1.3f);
					}
				}
				// Salto de panzazo: chevrones y cartel en el labio de llegada.
				if (GapStyleOf(F) == EGapStyle::Dive)
				{
					// Pieza de arte (chevrones y cartel): centro del hueco a la cota del labio, +X a lo largo del hueco.
					TNArt::FPieceScope SignPiece(ArtLog, TN_ART("ProcMap.Gap.DiveSign"), TNProcArtPivot(F.Location, D), { &Painted });
					TNProcAddDiveHint(Painted, F, Inner);
				}
				if (bLava)
				{
					const FVector2D C(F.Location.X, F.Location.Y);
					const FVector2D N = LeftNormal(D);
					const double Hl = F.Height * 0.5 + 100.0;
					const double Hs = F.Width * 0.5 + GapTrenchSideOf(F) + 100.0;
					TArray<FVector2D> Poly = { C - D * Hl - N * Hs, C + D * Hl - N * Hs, C + D * Hl + N * Hs, C - D * Hl + N * Hs };
					// Pieza de arte: la superficie del río de lava, centro a su cota, +X a lo largo del hueco; escala 1 = 10 x 10 m.
					TNArt::FPieceScope RiverPiece(ArtLog, TN_ART("ProcMap.Gap.LavaRiver"),
						TNProcArtPivot(FVector(C, F.Location.Z - 150.0), D, FVector(Hl * 2.0 / 1000.0, Hs * 2.0 / 1000.0, 1.0)), { &Lava });
					Lava.AddPrism(Poly, F.Location.Z - 150.0, F.Location.Z - 160.0, FLinearColor(1.f, 0.35f, 0.05f), false);
				}
				break;
			}
			case EFeature::ClimbTower:
			{
				// Torre de escalada del estilo del bioma: bloques de 1 m (tres o cuatro) con escalones de 1 m
				// por su cara de -Dir; cajas en la playa y los pueblos, sillares en el desierto, losas de roca
				// en el rocoso, troncos en la selva y columnas de basalto en el volcán. Banderín en la cima.
				const FVector D3(F.Dir.X, F.Dir.Y, 0.0);
				const FVector N3(-F.Dir.Y, F.Dir.X, 0.0);
				const FVector Base = F.Location;
				const int32 Levels = FMath::Clamp(FMath::RoundToInt32(F.Height / 100.0), 3, 4);
				const uint32 Ts = static_cast<uint32>(F.Aux2);
				constexpr double Th = PlazaDims::TowerHalf;
				FLinearColor Gc, Pcc, RockCc, Bdc;
				ResolveBiomeColors(F.Biome, Gc, Pcc, RockCc, Bdc);
				const FLinearColor Sand = TNProcLerpColor(RockCc, Gc, 0.5f) * 1.15f;
				// Pieza de arte (torre, escalones, banderín y almohadilla de la medusa): centro de la base, +X hacia la medusa
				// (los escalones bajan hacia -X); escala 1 = cuatro bloques de 1 m (la de tres va a 0,75 de alto).
				TNArt::FPieceScope TowerPiece(ArtLog, TNClimbTowerArt(F.Biome), TNProcArtPivot(Base, F.Dir, FVector(1.0, 1.0, Levels / 4.0)), { &Painted });
				auto Block = [&](const FVector& C, const FVector& Half, int32 k)
				{
					switch (F.Biome)
					{
						case ETNProcBiome::Beach:
						case ETNProcBiome::Human:
						{
							// Caja: cuerpo, cantoneras y un aspa en cada cara.
							const FLinearColor Body = FLinearColor(0.62f, 0.44f, 0.24f) * TNProcTone(k, Ts);
							const FLinearColor Frame = Body * 0.62f;
							Painted.AddBox(C, D3, Half, Body);
							for (const double Sz : { -1.0, 1.0 })
							{
								Painted.AddBox(C + FVector(0.0, 0.0, Sz * (Half.Z - 6.0)), D3, FVector(Half.X + 2.0, Half.Y + 2.0, 6.0), Frame);
							}
							for (const double Sa : { -1.0, 1.0 })
							{
								for (const double Sb : { -1.0, 1.0 })
								{
									Painted.AddBeam(C + D3 * (Sa * Half.X) + N3 * (Sb * (Half.Y + 1.5)) - FVector(0.0, 0.0, Half.Z), C + D3 * (Sa * Half.X) + N3 * (Sb * (Half.Y + 1.5)) + FVector(0.0, 0.0, Half.Z), 4.0, Frame);
								}
								const FVector Face = C + N3 * (Sa * (Half.Y + 2.0));
								Painted.AddBeam(Face + D3 * Half.X - FVector(0.0, 0.0, Half.Z - 10.0), Face - D3 * Half.X + FVector(0.0, 0.0, Half.Z - 10.0), 3.5, Frame);
								Painted.AddBeam(Face - D3 * Half.X - FVector(0.0, 0.0, Half.Z - 10.0), Face + D3 * Half.X + FVector(0.0, 0.0, Half.Z - 10.0), 3.5, Frame);
							}
							break;
						}
						case ETNProcBiome::Jungle:
						case ETNProcBiome::Mangrove:
						{
							// Tocón ancho con corteza y musgo encima.
							TNProcAddCylinder(Painted, C - FVector(0.0, 0.0, Half.Z), C + FVector(0.0, 0.0, Half.Z - 6.0), Half.X * 1.05, Half.X, 10, FLinearColor(0.34f, 0.22f, 0.12f) * TNProcTone(k, Ts));
							TNProcAddCylinder(Painted, C + FVector(0.0, 0.0, Half.Z - 8.0), C + FVector(0.0, 0.0, Half.Z + 2.0), Half.X + 4.0, Half.X - 6.0, 10, FLinearColor(0.22f, 0.44f, 0.15f));
							break;
						}
						case ETNProcBiome::Volcanic:
							TNRockMesh::TNRockHexColumn(Painted, C - FVector(0.0, 0.0, Half.Z), FMath::Min(Half.X, Half.Y), Half.Z * 2.0, Ts + k, FLinearColor(0.13f, 0.12f, 0.13f));
							break;
						case ETNProcBiome::Rocky:
							TNRockMesh::TNRockSlab(Painted, C, Half.X, Half.Y, Half.Z, 3.0 * TNProcHashNoise(k, 1, Ts), FMath::RadiansToDegrees(FMath::Atan2(F.Dir.Y, F.Dir.X)) + 6.0 * TNProcHashNoise(k, 2, Ts), Ts + k, RockCc * 1.25f);
							break;
						default:
							Painted.AddBox(C, D3, Half, Sand * TNProcTone(k, Ts));
							Painted.AddBox(C + FVector(0.0, 0.0, Half.Z - 4.0), D3, FVector(Half.X + 5.0, Half.Y + 5.0, 4.0), Sand * 0.8f);
							break;
					}
				};
				for (int32 b = 0; b < Levels; ++b)
				{
					Block(Base + FVector(0.0, 0.0, 50.0 + 100.0 * b), FVector(Th - 3.0 * b, Th - 3.0 * b, 50.0), b);
				}
				for (int32 s = 0; s < Levels - 1; ++s)
				{
					const double H = 100.0 * (s + 1);
					const FVector P = Base - D3 * (Th + PlazaDims::StepDepth * (Levels - 1 - s - 0.5));
					// Cada escalón, un bloque de su altura (apilado de a metro).
					for (int32 b = 0; b <= s; ++b)
					{
						Block(P + FVector(0.0, 0.0, 50.0 + 100.0 * b), FVector(PlazaDims::StepDepth * 0.5 - 2.0, Th - 8.0, 50.0), 10 + s * 4 + b);
					}
				}
				const double Top = Base.Z + 100.0 * Levels;
				Painted.AddBeam(FVector(Base.X, Base.Y, Top) + D3 * (Th - 20.0) + N3 * (Th - 20.0), FVector(Base.X, Base.Y, Top + 230.0) + D3 * (Th - 20.0) + N3 * (Th - 20.0), 3.0, FLinearColor(0.2f, 0.2f, 0.22f));
				Painted.AddBox(FVector(Base.X, Base.Y, Top + 200.0) + D3 * (Th + 10.0) + N3 * (Th - 20.0), D3, FVector(30.0, 1.5, 18.0), FLinearColor(0.95f, 0.72f, 0.1f));
				// Almohadilla de la medusa.
				const FVector2D Jp = FVector2D(Base.X, Base.Y) + F.Dir * (Th + 210.0);
				TArray<FVector2D> Pad;
				for (int32 k = 0; k < 12; ++k) { Pad.Add(Jp + FVector2D(FMath::Cos(TwoPi * k / 12.0), FMath::Sin(TwoPi * k / 12.0)) * 110.0); }
				Painted.AddPrism(Pad, Base.Z + 4.0, Base.Z + 1.0, FLinearColor(0.62f, 0.3f, 0.66f), false);
				break;
			}
			case EFeature::Boardwalk:
			{
				// Pasarela de tablones sobre postes hundidos en el agua, con cuerda a los lados.
				const int32 From = FMath::Clamp(F.PathIndex, 0, M.Num() - 1);
				const int32 To = FMath::Clamp(F.Aux2, 0, M.Num() - 1);
				if (To <= From) { break; }
				FTNPlankLine Line;
				for (int32 i = From; i <= To; ++i) { Line.Add(FVector(M[i].P, M[i].Z), M[i].Width * 0.5); }
				const uint32 Seed = Layout.Params.Seed ^ (0xB0A2Du + static_cast<uint32>(From));
				// Pieza de arte: la pasarela entera, del principio al final a la cota del camino (+X hacia el final; escala X 1 =
				// 20 m).
				TNArt::FPieceScope WalkPiece(ArtLog, TN_ART("ProcMap.Lagoon.Boardwalk"), TNProcArtSpanPivot(Line.P[0], Line.P.Last(), 2000.0), { &Wood });
				TNProcAddPlanks(Wood, Line, 0.0, Line.Length(), WoodColor * 0.9f, Seed);
				TNProcAddRopeRails(Wood, Line, 0.0, Line.Length(), 260.0, 320.0, 85.0, WoodColor * 0.6f, RopeColor);
				break;
			}
			case EFeature::RiverBridge:
			{
				// Puente de madera sobre el río. Muchos están rotos: les falta el centro (7 m, no se salta) y hay que
				// rodear por el agua: se baja al río por el hueco, se va por las piedras (o nadando) hasta la
				// escalera de madera que sube pegada al puente por un lado y se vuelve al tablero por un rellano
				// (la barandilla está abierta ahí). La escalera no puede quedar enterrada por la orilla.
				const FVector2D D = F.Dir;
				const FVector2D N = LeftNormal(D);
				const FVector2D Cb(F.Location.X, F.Location.Y);
				const double DeckZ = F.Location.Z;
				const double HalfL = F.Length * 0.5;
				const double HalfW = F.Width * 0.5;
				constexpr double HoleHalf = 350.0;
				constexpr double StepRise = 30.0;
				constexpr double StepRun = 42.0;
				constexpr double StairHalf = 80.0;
				const double StairOff = HalfW + 10.0 + StairHalf;
				const int32 NumSteps = FMath::FloorToInt32((DeckZ - 35.0) / StepRise) + 1;
				const double StairRun = StepRun * (NumSteps - 1);
				const uint32 RiverHash = HashCell(Layout.Params.Seed ^ 0xB40Eu, F.PathIndex, F.BranchIndex + 3);
				int32 StairSide = 0;
				double StairTop = 0.0;
				if (F.Length > 1600.0 && (RiverHash & 3u) != 0u && DeckZ > 150.0 && TerrainHeightMap(Cb) < -100.0)
				{
					for (int32 Try = 0; Try < 2 && StairSide == 0; ++Try)
					{
						const int32 Sd = (((RiverHash >> 2) & 1u) != 0u) == (Try == 0) ? 1 : -1;
						// El primer peldaño junto al hueco (ahí llegan las piedras); el rellano, sobre el tramo de llegada.
						for (double At = FMath::Max(HoleHalf + 110.0, StairRun - 220.0); At <= FMath::Min(HalfL - 110.0, StairRun + 250.0) && StairSide == 0; At += 40.0)
						{
							bool bOk = true;
							for (int32 k = 0; k < NumSteps && bOk; ++k)
							{
								const double Top = DeckZ - StepRise * (NumSteps - 1 - k);
								const double Along = At - StepRun * (NumSteps - 1 - k);
								for (const double Lat : { StairOff - StairHalf, StairOff, StairOff + StairHalf })
								{
									if (TerrainHeightMap(Cb + D * Along + N * (Sd * Lat)) > Top - 10.0) { bOk = false; break; }
								}
							}
							if (bOk) { StairSide = Sd; StairTop = At; }
						}
					}
				}
				const bool bBrokenBridge = StairSide != 0;
				const double OpenA = StairTop - StepRun * 0.5 - 10.0;
				const double OpenB = StairTop + 110.0;
				// Pieza de arte: el puente entero (intacto, o roto con la escalera a la izquierda o a la derecha de +X, con sus
				// piedras), centro del tablero a su cota, +X a lo largo de Dir; escala 1 = 2000 de largo y 600 de ancho.
				TNArt::FPieceScope RiverPiece(ArtLog,
					!bBrokenBridge ? TN_ART("ProcMap.River.Bridge") : (StairSide > 0 ? TN_ART("ProcMap.River.BrokenBridgeLeft") : TN_ART("ProcMap.River.BrokenBridgeRight")),
					TNProcArtPivot(FVector(Cb, DeckZ), D, FVector(F.Length / 2000.0, F.Width / 600.0, 1.0)), { &Wood, &Painted, &PaintedFar, &Rock });
				// Tablero y barandillas de [Pa, Pb] (la barandilla del lado de la escalera, abierta en el rellano).
				auto DeckPiece = [&](double Pa, double Pb)
				{
					Wood.AddBox(FVector(Cb + D * ((Pa + Pb) * 0.5), DeckZ - 45.0), FVector(D, 0.0), FVector((Pb - Pa) * 0.5, HalfW, 45.0), WoodColor);
					for (int32 Sd = -1; Sd <= 1; Sd += 2)
					{
						auto RailRun = [&](double Ra, double Rb)
						{
							if (Rb - Ra < 5.0) { return; }
							const FVector2D Rail = Cb + D * ((Ra + Rb) * 0.5) + N * (Sd * (HalfW - 10.0));
							Wood.AddBox(FVector(Rail, DeckZ + 45.0), FVector(D, 0.0), FVector((Rb - Ra) * 0.5, 10.0, 45.0), WoodColor * 0.8f);
						};
						if (bBrokenBridge && Sd == StairSide) { RailRun(Pa, FMath::Clamp(OpenA, Pa, Pb)); RailRun(FMath::Clamp(OpenB, Pa, Pb), Pb); }
						else { RailRun(Pa, Pb); }
					}
				};
				if (!bBrokenBridge)
				{
					DeckPiece(-HalfL, HalfL);
					break;
				}
				DeckPiece(-HalfL, -HoleHalf);
				DeckPiece(HoleHalf, HalfL);
				const uint32 Bs = RiverHash ^ 0x51ABu;
				const FVector D3(D, 0.0);
				TNProcAddBrokenEdge(Wood, PaintedFar, FVector(Cb - D * HoleHalf, DeckZ), D3, HalfW - 20.0, DeckZ, 10.0, true, WoodColor, Bs);
				TNProcAddBrokenEdge(Wood, PaintedFar, FVector(Cb + D * HoleHalf, DeckZ), -D3, HalfW - 20.0, DeckZ, 10.0, true, WoodColor, Bs + 7u);
				TNProcAddHazardBand(Painted, FVector(Cb - D * (HoleHalf + 90.0), DeckZ), D3, HalfW - 30.0, DeckZ);
				TNProcAddHazardBand(Painted, FVector(Cb + D * (HoleHalf + 90.0), DeckZ), D3, HalfW - 30.0, DeckZ);

				// Escalera: peldaños de 30 cm de alto y 42 de huella con puntales hasta el lecho, zancas por los
				// dos lados y el rellano de arriba a ras del tablero, pegado a su borde.
				const FVector2D Side2 = N * static_cast<double>(StairSide);
				const FLinearColor StairC = WoodColor * 0.9f;
				for (int32 k = 0; k < NumSteps; ++k)
				{
					const double Top = DeckZ - StepRise * (NumSteps - 1 - k);
					const double Along = StairTop - StepRun * (NumSteps - 1 - k);
					const FVector2D Sc = Cb + D * Along + Side2 * StairOff;
					if (k == NumSteps - 1)
					{
						const double In = HalfW - 1.0;
						const double Out = StairOff + StairHalf;
						const FVector2D Lc = Cb + D * (StairTop + (110.0 - StepRun * 0.5) * 0.5) + Side2 * ((In + Out) * 0.5);
						Wood.AddBox(FVector(Lc, Top - 8.0), FVector(D, 0.0), FVector((110.0 + StepRun * 0.5) * 0.5, (Out - In) * 0.5, 8.0), StairC * 1.1f);
					}
					else
					{
						Wood.AddBox(FVector(Sc, Top - 7.0), FVector(D, 0.0), FVector(StepRun * 0.5 + 1.0, StairHalf, 7.0), StairC * TNProcTone(k, Bs));
					}
					if (k % 3 == 0 || k == NumSteps - 1)
					{
						for (const double Lat : { -StairHalf + 10.0, StairHalf - 10.0 })
						{
							const FVector2D Pp = Sc + Side2 * Lat;
							const double PostTop = Top - 14.0;
							Wood.AddBox(FVector(Pp, 0.5 * (PostTop - 360.0)), FVector(D, 0.0), FVector(8.0, 8.0, 0.5 * (PostTop + 360.0)), StairC * 0.6f);
						}
					}
				}
				for (const double Lat : { -StairHalf + 6.0, StairHalf - 6.0 })
				{
					const FVector2D Lo = Cb + D * (StairTop - StairRun - StepRun * 0.5) + Side2 * (StairOff + Lat);
					const FVector2D Hi = Cb + D * (StairTop + StepRun * 0.5) + Side2 * (StairOff + Lat);
					PaintedFar.AddBeam(FVector(Lo, DeckZ - StepRise * (NumSteps - 1) - 24.0), FVector(Hi, DeckZ - 24.0), 6.0, StairC * 0.55f);
				}

				// Piedras del río: en fila desde debajo del hueco hasta el primer peldaño (que la búsqueda deja junto
				// al hueco), cima a 35 cm sobre el agua y a saltitos de menos de medio metro.
				const double StoneAlong = FMath::Clamp(StairTop - StairRun, -HoleHalf + 110.0, HoleHalf - 110.0);
				const double LatEnd = StairOff - StairHalf - 80.0;
				const int32 NumStones = FMath::Max(2, FMath::CeilToInt32(LatEnd / 170.0) + 1);
				for (int32 k = 0; k < NumStones; ++k)
				{
					const FVector2D Q = Cb + D * StoneAlong + Side2 * (LatEnd * k / (NumStones - 1));
					if (TerrainHeightMap(Q) > 0.0) { continue; }
					TNProcAddCylinder(Rock, FVector(Q, -360.0), FVector(Q, 35.0), 72.0, 60.0, 9, RockColor * (0.95f + 0.1f * TNProcTone(k, Bs)));
				}
				UE_LOG(LogTortunabo, Log, TEXT("[ProcMap] Puente del río roto (muestra %d): escalera de %d peldaños."), F.PathIndex, NumSteps);
				break;
			}
			case EFeature::LavaPool:
			{
				TArray<FVector2D> Disc;
				for (int32 k = 0; k < 24; ++k)
				{
					const double A = TwoPi * k / 24.0;
					Disc.Add(FVector2D(F.Location.X, F.Location.Y) + DirFromAngle(A) * (F.Radius * (1.0 + 0.06 * FMath::Sin(A * 3.0))));
				}
				// Pieza de arte: centro de la superficie de lava; escala 1 = radio 500.
				TNArt::FPieceScope PoolPiece(ArtLog, TN_ART("ProcMap.Lava.Pool"), TNArt::PiecePivot(F.Location, 0.0, FVector(F.Radius / 500.0, F.Radius / 500.0, 1.0)),
					{ &Lava });
				Lava.AddPrism(Disc, F.Location.Z, F.Location.Z - 10.0, FLinearColor(1.f, 0.35f, 0.05f), false);
				break;
			}
			case EFeature::PathProp:
			{
				// Obstáculo de objetos del bioma (con colisión): en su marco local, sobre el suelo real.
				const FVector2D C(F.Location.X, F.Location.Y);
				const FVector2D Dx = F.Dir.GetSafeNormal().IsNearlyZero() ? FVector2D(1.0, 0.0) : F.Dir.GetSafeNormal();
				const FVector2D Dy(-Dx.Y, Dx.X);
				const double OriginZ = TerrainHeightMap(C);
				TNPropMesh::FTNPathPropParams Params;
				Params.Radius = F.Radius;
				Params.Height = F.Height;
				Params.Length = F.Length;
				Params.Seed = static_cast<uint32>(F.Aux2);
				Params.Crystal = TNPropMesh::TNPropCrystalColor(F.Biome);
				Params.bCharred = F.Biome == ETNProcBiome::Volcanic;
				auto Ground = [&](double X, double Y) { return TerrainHeightMap(C + Dx * X + Dy * Y) - OriginZ; };
				FTNProcMeshBuffers Local;
				TNPropMesh::TNPathPropBuild(Local, static_cast<EPathProp>(F.Aux), Params, Ground);
				{
					// Pieza de arte: centro en el suelo, +X por su eje; escala respecto a su tamaño de referencia (TNProcPathPropArtRef).
					const EPathProp Kind = static_cast<EPathProp>(F.Aux);
					const FVector2D Ref = TNProcPathPropArtRef(Kind);
					const double Foot = FMath::Max(F.Radius, F.Length * 0.5);
					TNArt::FPieceScope PropPiece(ArtLog, TNProcPathPropArt(Kind), TNProcArtPivot(FVector(C, OriginZ), Dx, FVector(Foot / Ref.X, Foot / Ref.X, F.Height / Ref.Y)),
						{ &Painted });
					TNFormMesh::TNFormAppend(Painted, Local, FVector(C, OriginZ), Dx);
				}
				break;
			}
			case EFeature::Boulder:
			{
				// Peñasco con el estilo de su bioma (redondo, losa, partido, apilado, estratos, basalto, musgo,
				// cristales o coral), con color de vértice.
				const FVector2D C(F.Location.X, F.Location.Y);
				FLinearColor G, Pc, RockC, Bd;
				ResolveBiomeColors(F.Biome, G, Pc, RockC, Bd);
				const uint32 Seed = static_cast<uint32>(F.Aux);
				FTNProcMeshBuffers Local;
				TNRockMesh::TNRockBuildBoulder(Local, TNRockMesh::TNBoulderStyleFor(F.Biome, Seed), F.Radius, F.Height, Seed, TNRockMesh::TNRockColorsFor(F.Biome, RockC, G));
				// Pieza de arte: centro de la base sobre el terreno, con su giro; escala 1 = radio 150 y 180 de alto.
				TNArt::FPieceScope RockPiece(ArtLog, TNProcBoulderArt(TNRockMesh::TNBoulderStyleFor(F.Biome, Seed)),
					TNArt::PiecePivot(FVector(C, TerrainHeightMap(C)), TNRockMesh::TNRockRand(Seed, 9, 0.0, 360.0), FVector(F.Radius / 150.0, F.Radius / 150.0, F.Height / 180.0)),
					{ &Painted });
				TNPropMesh::TNPropAppend(Painted, Local, FVector(C, TerrainHeightMap(C)), TNRockMesh::TNRockRand(Seed, 9, 0.0, 360.0));
				break;
			}
			case EFeature::RockSpire:
			{
				// Aguja (esbelta) o mogote (bajo y ancho) con el estilo de su bioma: aguja con sombrero, inclinada,
				// gemela, chimenea de hadas, pilar kárstico con vegetación u órgano de basalto; mogote, tor, mesa
				// de estratos o domo de lava.
				const FVector2D C(F.Location.X, F.Location.Y);
				FLinearColor G, Pc, RockC, Bd;
				ResolveBiomeColors(F.Biome, G, Pc, RockC, Bd);
				const uint32 Seed = static_cast<uint32>(F.Aux);
				const bool bSpire = F.Height > F.Radius * 2.0;
				FTNProcMeshBuffers Local;
				TNRockMesh::TNRockBuildSpire(Local, TNRockMesh::TNSpireStyleFor(F.Biome, bSpire, Seed), F.Radius, F.Height, Seed,
					TNRockMesh::TNRockColorsFor(F.Biome, RockC, G));
				// Pieza de arte: centro de la base sobre el terreno, con su giro; escala respecto a su referencia (TNProcSpireArtRef).
				const TNRockMesh::ESpireStyle SpireStyle = TNRockMesh::TNSpireStyleFor(F.Biome, bSpire, Seed);
				const FVector2D SpireRef = TNProcSpireArtRef(SpireStyle);
				TNArt::FPieceScope RockPiece(ArtLog, TNProcSpireArt(SpireStyle), TNArt::PiecePivot(FVector(C, TerrainHeightMap(C)), TNRockMesh::TNRockRand(Seed, 9, 0.0, 360.0),
					FVector(F.Radius / SpireRef.X, F.Radius / SpireRef.X, F.Height / SpireRef.Y)), { &Painted });
				TNPropMesh::TNPropAppend(Painted, Local, FVector(C, TerrainHeightMap(C)), TNRockMesh::TNRockRand(Seed, 9, 0.0, 360.0));
				break;
			}
			case EFeature::Log:
			{
				const FVector2D C(F.Location.X, F.Location.Y);
				const FVector2D Half = F.Dir * (F.Length * 0.5);
				const FVector A(C - Half, TerrainHeightMap(C - Half) + F.Radius * 0.85);
				const FVector B(C + Half, TerrainHeightMap(C + Half) + F.Radius * 0.85);
				const bool bCharred = F.Biome == ETNProcBiome::Volcanic;
				const FLinearColor Bark = bCharred ? FLinearColor(0.07f, 0.06f, 0.05f) : FLinearColor(0.3f, 0.2f, 0.11f);
				const FLinearColor Cut = bCharred ? FLinearColor(0.35f, 0.12f, 0.05f) : FLinearColor(0.62f, 0.48f, 0.3f);
				// Pieza de arte: centro del eje del tronco, +X a lo largo (con su inclinación); escala 1 = 400 de largo y radio 45.
				TNArt::FPieceScope LogPiece(ArtLog, TN_ART("ProcMap.Tree.FallenLog"),
					FTransform(FRotationMatrix::MakeFromX(B - A).ToQuat(), (A + B) * 0.5, FVector(FVector::Dist(A, B) / 400.0, F.Radius / 45.0, F.Radius / 45.0)), { &Wood });
				TNProcAddLog(Wood, A, B, F.Radius, static_cast<uint32>(F.Aux), Bark, Cut);
				break;
			}
			case EFeature::Formation:
			{
				if (bTerrainOnly) { break; }
				// Formación temática: se construye en su marco local (origen en el suelo del camino o, en los
				// hitos lejanos, en el terreno) y se apoya en el terreno real.
				const EFormation Kind = static_cast<EFormation>(F.Aux);
				FLinearColor G, Pc, RockC, Bd;
				ResolveBiomeColors(F.Biome, G, Pc, RockC, Bd);
				const FVector2D C(F.Location.X, F.Location.Y);
				const bool bFar = IsLandmarkFormation(Kind);
				const double OriginZ = bFar ? TerrainHeightMap(C) : F.Location.Z;
				const FVector2D Dx = F.Dir.GetSafeNormal().IsNearlyZero() ? FVector2D(1.0, 0.0) : F.Dir.GetSafeNormal();
				const FVector2D Dy(-Dx.Y, Dx.X);
				auto Ground = [&](double X, double Y) { return TerrainHeightMap(C + Dx * X + Dy * Y) - OriginZ; };
				TNFormMesh::FTNFormParams Params;
				Params.Width = F.Width;
				Params.Height = F.Height;
				Params.Length = F.Length;
				Params.Radius = F.Radius;
				Params.Seed = static_cast<uint32>(F.Aux2);
				Params.WaterZ = TNProcMap::SeaLevel - OriginZ;
				FTNProcMeshBuffers Local;
				TNFormMesh::TNFormBuild(Local, Kind, Params, TNFormMesh::TNFormColorsFor(F.Biome, RockC), Ground);
				// Pieza de arte: origen de la formación (centro en el suelo), +X por su eje; escala respecto a su referencia
				// (TNProcFormationArtScale).
				TNArt::FPieceScope FormationPiece(ArtLog, TNProcFormationArt(Kind), TNProcArtPivot(FVector(C, OriginZ), Dx, TNProcFormationArtScale(F, Kind)),
					{ bFar ? &PaintedFar : &Painted });
				TNFormMesh::TNFormAppend(bFar ? PaintedFar : Painted, Local, FVector(C, OriginZ), Dx);
				break;
			}
			case EFeature::Finish:
			{
				// Meta: neumático en arco sobre la línea (ya en el agua), boyas en toda la boca, banderines y
				// banderolas en la playa. El neumático y los mástiles llevan colisión; rótulos y telas, no.
				const FVector2D C(F.Location.X, F.Location.Y);
				const FVector2D Dx = F.Dir.GetSafeNormal().IsNearlyZero() ? FVector2D(0.0, 1.0) : F.Dir.GetSafeNormal();
				const FVector2D Dy(-Dx.Y, Dx.X);
				TNFinishMesh::FTNFinishParams Params;
				Params.Radius = F.Radius;
				Params.MouthHalf = F.Width * 0.5;
				Params.FloorZ = F.Location.Z;
				Params.Seed = static_cast<uint32>(F.Aux);
				auto Ground = [&](double X, double Y) { return TerrainHeightMap(C + Dx * X + Dy * Y) - TNProcMap::SeaLevel; };
				auto HalfWidthAt = [&](double X) { return 0.5 * FinishBeachWidthAt(Layout, F.Location.Y + X * Dx.Y); };
				FTNProcMeshBuffers Solid, Deco;
				TNFinishMesh::TNFinishBuild(Solid, Deco, Params, Ground, HalfWidthAt);
				// Pieza de arte (neumático, pasarela, carteles, banderas, boyas y banderolas): centro de la línea a nivel del mar,
				// +X hacia el mar.
				TNArt::FPieceScope FinishPiece(ArtLog, TN_ART("ProcMap.Finish.Gate"), TNProcArtPivot(FVector(C, TNProcMap::SeaLevel), Dx), { &Painted, &PaintedFar });
				TNFormMesh::TNFormAppend(Painted, Solid, FVector(C, TNProcMap::SeaLevel), Dx);
				TNFormMesh::TNFormAppend(PaintedFar, Deco, FVector(C, TNProcMap::SeaLevel), Dx);
				break;
			}
			case EFeature::GiantTree:
			{
				if (bTerrainOnly) { break; }
				const FVector2D C(F.Location.X, F.Location.Y);
				// Secuoya: tronco rojizo con base ensanchada, raíces zancudas en arco y copa cónica por capas.
				const uint32 Seed = static_cast<uint32>(F.Aux);
				const double Ground = TerrainHeightMap(C);
				const FVector Base(C, Ground - 60.0);
				const FLinearColor Bark(0.36f, 0.17f, 0.09f);
				// Pieza de arte (tronco, raíces y copa): centro del tronco a ras del suelo; escala 1 = radio 200 y 3900 de alto.
				TNArt::FPieceScope TreePiece(ArtLog, TN_ART("ProcMap.Tree.GiantSequoia"), TNArt::PiecePivot(FVector(C, Ground), 0.0,
					FVector(F.Radius / 200.0, F.Radius / 200.0, F.Height / 3900.0)), { &Wood, &Foliage });
				TArray<double> Z, R;
				for (int32 r = 0; r <= 9; ++r)
				{
					const double T = r / 9.0;
					Z.Add(F.Height * 0.9 * T);
					R.Add(F.Radius * (T < 0.08 ? FMath::Lerp(1.9, 1.0, T / 0.08) : FMath::Lerp(1.0, 0.35, (T - 0.08) / 0.92)));
				}
				TNProcAddLathe(Wood, Base, Z, R, 0.05, Seed, Bark, 12);
				const int32 NumRoots = 8 + static_cast<int32>(4.0 * (0.5 + 0.5 * TNProcHashNoise(0, 0, Seed)));
				for (int32 k = 0; k < NumRoots; ++k)
				{
					const double Ang = TwoPi * (k + 0.4 * TNProcHashNoise(k, 1, Seed)) / NumRoots;
					const FVector2D Dir(FMath::Cos(Ang), FMath::Sin(Ang));
					const double Reach = F.Radius * (2.6 + 1.2 * (0.5 + 0.5 * TNProcHashNoise(k, 2, Seed)));
					const double Top = 250.0 + 250.0 * (0.5 + 0.5 * TNProcHashNoise(k, 3, Seed));
					FVector Prev = FVector(C + Dir * (F.Radius * 0.8), Ground + Top);
					for (int32 t = 1; t <= 4; ++t)
					{
						const double U = t / 4.0;
						const FVector2D Q = C + Dir * FMath::Lerp(F.Radius * 0.8, Reach, U);
						const FVector Pt(Q, FMath::Lerp(Ground + Top, TerrainHeightMap(Q) - 40.0, U * U) + 120.0 * FMath::Sin(U * PI));
						Wood.AddBeam(Prev, Pt, F.Radius * FMath::Lerp(0.22, 0.12, U), Bark * 0.9f);
						Prev = Pt;
					}
				}
				const int32 Clumps = 6;
				for (int32 k = 0; k < Clumps; ++k)
				{
					const double T = FMath::Lerp(0.45, 0.97, static_cast<double>(k) / (Clumps - 1));
					const double CR = F.Radius * FMath::Lerp(4.2, 1.4, T) * (0.85 + 0.3 * (0.5 + 0.5 * TNProcHashNoise(k, 5, Seed)));
					const FVector Off(TNProcHashNoise(k, 6, Seed) * F.Radius * 0.8, TNProcHashNoise(k, 7, Seed) * F.Radius * 0.8, 0.0);
					TNProcAddBoulder(Foliage, Base + Off + FVector(0.0, 0.0, F.Height * T), CR, CR * 0.9, Seed + 100u + k,
						FLinearColor(0.05f, 0.2f + 0.08f * static_cast<float>(k % 2), 0.06f));
				}
				break;
			}
			case EFeature::SlideZone:
			{
				// Lámina de agua sobre la bajada (el tobogán en sí es terreno empinado): rejilla de 30 cm a lo
				// largo y ~60 cm a lo ancho. Cada vértice va 25 cm sobre el punto más alto del terreno en el
				// rectángulo de sus cuatro cuadros vecinos: así cualquier punto de la lámina (mezcla de vértices
				// que están todos por encima del terreno en ese punto) queda sobre la ladera. UV de flujo para
				// que las ondas del material corran ladera abajo, espuma blanca en los bordes y al pie y el
				// borde más transparente.
				const TArray<FPathSample>& S = F.BranchIndex == INDEX_NONE ? M : Layout.Branches[F.BranchIndex].Samples;
				const int32 From = FMath::Clamp(F.PathIndex, 0, S.Num() - 1);
				// Hasta la muestra de aterrizaje, ya dentro de la poza (la última del tobogán puede quedar metros por
				// encima del fondo y la lámina se quedaba cortada en el aire).
				const int32 To = FMath::Clamp(F.Aux + 1, 0, S.Num() - 1);
				constexpr double RowStep = 30.0;
				double MaxW = 0.0;
				for (int32 i = From; i <= To; ++i) { MaxW = FMath::Max(MaxW, S[i].Width); }
				const int32 Cols = FMath::Clamp(FMath::CeilToInt32(MaxW * 0.78 / 60.0), 8, 40);
				auto Lifted = [this](const FVector2D& Q, const FVector2D& Along, const FVector2D& Across, double HalfAlong, double HalfAcross)
				{
					double H = -1e18;
					for (int32 a = -2; a <= 2; ++a)
					{
						for (int32 b = -2; b <= 2; ++b)
						{
							H = FMath::Max(H, TerrainHeightMap(Q + Along * (HalfAlong * a * 0.5) + Across * (HalfAcross * b * 0.5)));
						}
					}
					return H + 25.0;
				};
				auto AddFlowQuad = [&SlideWater](const FVector (&P)[4], const FVector2D (&UV)[4], const FLinearColor (&Col)[4])
				{
					for (int32 t = 0; t < 2; ++t)
					{
						const int32 I0 = 0, I1 = t == 0 ? 1 : 2, I2 = t == 0 ? 2 : 3;
						const int32 Base = SlideWater.Verts.Num();
						SlideWater.AddTri(P[I0], P[I1], P[I2], FVector::UpVector, Col[I0]);
						// AddTri puede cambiar el orden para orientar la cara: UV y color por posición.
						for (int32 v = Base; v < SlideWater.Verts.Num(); ++v)
						{
							int32 Best = 0;
							for (int32 q = 1; q < 4; ++q) { if (FVector::DistSquared(SlideWater.Verts[v], P[q]) < FVector::DistSquared(SlideWater.Verts[v], P[Best])) { Best = q; } }
							SlideWater.UVs[v] = UV[Best];
							SlideWater.Colors[v] = Col[Best];
						}
					}
				};
				TArray<FVector> Prev;
				TArray<FVector2D> PrevUV;
				TArray<FLinearColor> PrevCol;
				double Travel = 0.0;
				FVector2D LastC = S[From].P;
				// Pieza de arte: la lámina entera, del labio al aterrizaje (+X hacia abajo; escala X 1 = 20 m en planta).
				const int32 SheetPiece = ArtLog.Begin(TN_ART("ProcMap.Slide.WaterSheet"), TNProcArtSpanPivot(FVector(S[From].P, S[From].Z), FVector(S[To].P, S[To].Z), 2000.0),
					{ &SlideWater });
				for (int32 i = From; i <= To; ++i)
				{
					const int32 Sub = i < To ? FMath::Max(1, FMath::CeilToInt(FVector2D::Distance(S[i].P, S[i + 1].P) / RowStep)) : 1;
					for (int32 k = 0; k < Sub; ++k)
					{
						if (i == To && k > 0) { break; }
						const double U = static_cast<double>(k) / Sub;
						const FPathSample& A = S[i];
						const FPathSample& B = S[FMath::Min(i + 1, To)];
						const FVector2D C = FMath::Lerp(A.P, B.P, U);
						const FVector2D Dir = FMath::Lerp(A.Dir, B.Dir, U).GetSafeNormal();
						const FVector2D N = LeftNormal(Dir);
						const double Hw = FMath::Lerp(A.Width, B.Width, U) * 0.39;
						Travel += FVector2D::Distance(C, LastC);
						LastC = C;
						const float Foot = static_cast<float>(i - From) / FMath::Max(1, To - From);
						TArray<FVector> Row;
						TArray<FVector2D> RowUV;
						TArray<FLinearColor> RowCol;
						for (int32 c = 0; c <= Cols; ++c)
						{
							const double X = 2.0 * c / Cols - 1.0;
							const FVector2D Q = C + N * (Hw * X);
							Row.Add(FVector(Q, Lifted(Q, Dir, N, RowStep * 1.3, 2.0 * Hw / Cols * 1.15)));
							RowUV.Add(FVector2D(static_cast<double>(c) / Cols, Travel / 300.0));
							const float Edge = static_cast<float>(FMath::Pow(FMath::Abs(X), 3.0));
							// Espuma en el labio (el primer metro y medio, por donde el agua se asoma) y al pie.
							const float Lip = 1.f - static_cast<float>(FMath::SmoothStep(40.0, 170.0, Travel));
							FLinearColor Col = TNProcLerpColor(FLinearColor(0.18f, 0.52f, 0.8f), FLinearColor(0.96f, 0.99f, 1.f),
								FMath::Min(1.f, 0.75f * Edge + 0.7f * Foot * Foot * Foot + 0.9f * Lip));
							Col.A = FMath::Lerp(0.9f, 0.45f, Edge);
							RowCol.Add(Col);
						}
						if (Prev.Num() == Row.Num())
						{
							for (int32 c = 0; c < Cols; ++c)
							{
								const FVector P4[4] = { Prev[c], Prev[c + 1], Row[c + 1], Row[c] };
								const FVector2D UV4[4] = { PrevUV[c], PrevUV[c + 1], RowUV[c + 1], RowUV[c] };
								const FLinearColor Col4[4] = { PrevCol[c], PrevCol[c + 1], RowCol[c + 1], RowCol[c] };
								AddFlowQuad(P4, UV4, Col4);
							}
						}
						Prev = MoveTemp(Row);
						PrevUV = MoveTemp(RowUV);
						PrevCol = MoveTemp(RowCol);
					}
				}
				ArtLog.End(SheetPiece);
				// Poza al pie: disco plano a la cota del agua, a ras de suelo, sobre el cuenco que hunde el terreno
				// (TNProcMap::SlidePoolOf): la orilla es donde el terreno vuelve a salir del agua, y ahí va la espuma (los
				// vértices con el suelo a menos de 20 cm del agua). Donde el terreno quede más bajo que el agua, el borde
				// baja hasta él para no quedar colgado. Las UV salen del punto donde cae el agua (V = distancia a él): el
				// material hace correr las ondas desde el impacto hacia fuera.
				{
					FVector2D PC, Impact, Flow;
					double R = 0.0, WaterZ = 0.0;
					SlidePool(F, PC, R, WaterZ, Impact, Flow);
					const FVector2D Across(-Flow.Y, Flow.X);
					auto PoolUV = [&](const FVector2D& Q) { return FVector2D(FVector2D::DotProduct(Q - Impact, Across) / 300.0, FVector2D::Distance(Q, Impact) / 300.0); };
					constexpr int32 Seg = 32;
					const double Radii[] = { 0.0, 0.16, 0.32, 0.48, 0.62, 0.74, 0.86, 1.0 };
					constexpr int32 NumR = UE_ARRAY_COUNT(Radii);
					const FLinearColor Water(0.16f, 0.5f, 0.76f, 0.88f), Foam(0.96f, 0.99f, 1.f, 0.85f);
					// Pieza de arte: centro de la poza a la cota del agua, +X hacia donde corre el agua; escala 1 = radio 350.
					TNArt::FPieceScope PoolPiece(ArtLog, TN_ART("ProcMap.Slide.Pool"), TNProcArtPivot(FVector(PC, WaterZ), Flow, FVector(R / 350.0, R / 350.0, 1.0)), { &SlideWater });
					auto PoolVertex = [&](const FVector2D& Q, double Rr, FVector& OutP, FLinearColor& OutC)
					{
						const double PoolGround = TerrainHeightMap(Q);
						const double PoolZ = Rr > 0.8 && PoolGround < WaterZ - 4.0 ? FMath::Max(PoolGround + 4.0, WaterZ - 60.0) : WaterZ;
						OutP = FVector(Q, PoolZ);
						const float Shore = 1.f - static_cast<float>(FMath::SmoothStep(4.0, 20.0, FMath::Abs(PoolGround - WaterZ)));
						OutC = TNProcLerpColor(Water, Foam, FMath::Max(Shore, static_cast<float>(FMath::SmoothStep(0.9, 1.0, Rr))));
					};
					for (int32 k = 0; k < Seg; ++k)
					{
						const double A0 = TNProcMap::TwoPi * k / Seg, A1 = TNProcMap::TwoPi * (k + 1) / Seg;
						for (int32 r = 0; r + 1 < NumR; ++r)
						{
							FVector P4[4];
							FVector2D UV4[4];
							FLinearColor C4[4];
							const double Ri2[4] = { Radii[r], Radii[r], Radii[r + 1], Radii[r + 1] };
							const double Ai[4] = { A0, A1, A1, A0 };
							for (int32 q = 0; q < 4; ++q)
							{
								const FVector2D Q = PC + FVector2D(FMath::Cos(Ai[q]), FMath::Sin(Ai[q])) * (R * Ri2[q]);
								PoolVertex(Q, Ri2[q], P4[q], C4[q]);
								UV4[q] = PoolUV(Q);
							}
							if (r == 0)
							{
								// Centro: triángulo (los dos primeros puntos coinciden).
								const int32 Base = SlideWater.Verts.Num();
								SlideWater.AddTri(P4[0], P4[2], P4[3], FVector::UpVector, C4[0]);
								for (int32 v = Base; v < SlideWater.Verts.Num(); ++v)
								{
									const FVector& Vv = SlideWater.Verts[v];
									SlideWater.UVs[v] = PoolUV(FVector2D(Vv.X, Vv.Y));
								}
								continue;
							}
							AddFlowQuad(P4, UV4, C4);
						}
					}
				}
				break;
			}
			default:
				break;
		}
	}

	// ── Algas y nenúfares: manchas verdes flotando en las pozas junto al camino ─
	if (!bTerrainOnly)
	{
		FRng AlgaeRng(static_cast<uint64>(Layout.Params.Seed) * 0xA16Eull + 3ull);
		for (int32 i = 0; i < M.Num(); i += 3)
		{
			const FPathSample& Sm = M[i];
			if (Sm.Biome != ETNProcBiome::Mangrove && Sm.Biome != ETNProcBiome::Water) { continue; }
			for (int32 n = 0; n < 2; ++n)
			{
				const double Side = AlgaeRng.Chance(0.5) ? 1.0 : -1.0;
				const FVector2D Q = Sm.P + LeftNormal(Sm.Dir) * (Side * (Sm.Width * 0.5 + AlgaeRng.Range(150.0, 1800.0))) + Sm.Dir * AlgaeRng.Range(-300.0, 300.0);
				if (TerrainHeightMap(Q) > -40.0) { continue; }
				const double Rad = AlgaeRng.Range(60.0, 260.0);
				TArray<FVector2D> Poly;
				for (int32 k = 0; k < 9; ++k)
				{
					const double A = TwoPi * k / 9.0;
					Poly.Add(Q + FVector2D(FMath::Cos(A), FMath::Sin(A)) * (Rad * AlgaeRng.Range(0.6, 1.1)));
				}
				const bool bLily = Sm.Biome == ETNProcBiome::Water && AlgaeRng.Chance(0.4);
				const FLinearColor Col = bLily ? FLinearColor(0.12f, 0.42f, 0.1f) : FLinearColor(0.16f, 0.3f, 0.06f) * static_cast<float>(AlgaeRng.Range(0.8, 1.2));
				// Pieza de arte: centro de la mancha a ras del agua; escala 1 = radio 160.
				TNArt::FPieceScope PatchPiece(ArtLog, bLily ? TN_ART("ProcMap.Lagoon.LilyPads") : TN_ART("ProcMap.Lagoon.Algae"),
					TNArt::PiecePivot(FVector(Q, TNProcMap::SeaLevel + 4.0), 0.0, FVector(Rad / 160.0, Rad / 160.0, 1.0)), { &Foliage });
				Foliage.AddPrism(Poly, TNProcMap::SeaLevel + 4.0, TNProcMap::SeaLevel - 2.0, Col, false);
			}
		}
	}

	// ── Cuevas: techo de roca sobre el túnel, interior decorado según su estilo y sus luces ──────────
	for (const FFeature& F : Layout.Features)
	{
		if (F.Type != EFeature::Cave || F.PathIndex < 0 || F.Aux >= M.Num()) { continue; }
		TArray<TNCaveMesh::FTNCaveStation> Stations;
		TArray<uint8> NoFloor;
		for (int32 i = F.PathIndex; i <= F.Aux; ++i)
		{
			TNCaveMesh::FTNCaveStation S;
			S.Floor = FVector(M[i].P, M[i].Z);
			S.Dir = M[i].Dir;
			S.HalfWidth = M[i].Width * 0.5;
			Stations.Add(S);
			// El río de lava de la cámara: sin suelo, no se decora.
			NoFloor.Add((M[i].Flags & PathFlags::Gap) != 0 ? 1 : 0);
		}
		FLinearColor G, Pc, RockC, Bd;
		ResolveBiomeColors(F.Biome, G, Pc, RockC, Bd);
		const uint32 CaveSeed = static_cast<uint32>(F.Aux2);
		const TNCaveDecor::ECaveStyle Style = TNCaveDecor::TNCaveStyleFor(F.Biome, CaveSeed);
		const bool bVolcanic = F.Biome == ETNProcBiome::Volcanic;
		TNCaveMesh::FTNCaveLook Look;
		Look.Inner = RockC * 0.55f;
		Look.Outer = RockC;
		Look.Moss = bVolcanic ? RockC * 0.8f : TNProcLerpColor(RockC, G, 0.6f);
		Look.Crystal = FLinearColor(0.45f, 0.8f, 0.95f);
		Look.bCrystals = Style == TNCaveDecor::ECaveStyle::Limestone || Style == TNCaveDecor::ECaveStyle::Crystal;
		TArray<TArray<FVector>> Inner;
		// Piezas de arte del túnel (techo y tapa de montaña): de la boca de entrada a la de salida, en el suelo (+X hacia la
		// salida; escala X 1 = 50 m en planta).
		const FTransform CavePivot = Stations.Num() > 0 ? TNProcArtSpanPivot(Stations[0].Floor, Stations.Last().Floor, 5000.0) : FTransform::Identity;
		{
			TNArt::FPieceScope RoofPiece(ArtLog, TN_ART("ProcMap.Cave.Roof"), CavePivot, { &Painted });
			TNCaveMesh::TNCaveBuildRoof(Painted, Stations, F.Height, F.Radius, CaveSeed, Look, &Inner);
		}

		// Tapa de montaña: el terreno no puede tener techo, así que por encima del túnel quedaba una ranura a lo largo
		// del camino (la montaña «troquelada»). Se cubre con una superficie que une las laderas de los dos lados a su
		// altura, con algo de relieve y los colores del bioma, solo donde la montaña queda por encima del techo de roca;
		// sus bordes se meten un poco en el terreno para que no se vea la costura.
		{
			TNArt::FPieceScope CapPiece(ArtLog, TN_ART("ProcMap.Cave.MountainCap"), CavePivot, { &Painted });
			constexpr int32 CapPts = 9;
			constexpr double Reach = 700.0;
			TArray<TArray<FVector>> CapRings;
			TArray<uint8> CapOk;
			for (int32 i = 0; i < Stations.Num(); ++i)
			{
				const TNCaveMesh::FTNCaveStation& St = Stations[i];
				const FVector2D P2(St.Floor.X, St.Floor.Y);
				const FVector2D Nrm(-St.Dir.Y, St.Dir.X);
				const double CapEdge = St.HalfWidth + Reach;
				const double HL = TerrainHeightMap(P2 + Nrm * CapEdge);
				const double HR = TerrainHeightMap(P2 - Nrm * CapEdge);
				const double RoofTop = St.Floor.Z + CaveDetail::Clearance(St.HalfWidth * 2.0, F.Height) + F.Radius;
				const bool bInside = FMath::Min(HL, HR) > RoofTop + 120.0;
				CapOk.Add(bInside ? 1 : 0);
				TArray<FVector>& CapRing = CapRings.AddDefaulted_GetRef();
				for (int32 k = 0; k < CapPts; ++k)
				{
					const double T = static_cast<double>(k) / (CapPts - 1);
					const double Across = FMath::Lerp(CapEdge, -CapEdge, T);
					// Sube hacia el centro (loma) y se hunde 40 cm en el terreno por los bordes.
					const double Bump = 180.0 * FMath::Sin(PI * T) * (0.7 + 0.3 * TNProcHashNoise(i, k, CaveSeed));
					const double Z = FMath::Max(FMath::Lerp(HL, HR, T) + Bump - (k == 0 || k == CapPts - 1 ? 40.0 : 0.0), RoofTop + 60.0);
					CapRing.Add(FVector(P2 + Nrm * Across, Z));
				}
			}
			FLinearColor CapGrass, CapPath, CapRock, CapBed;
			ResolveBiomeColors(F.Biome, CapGrass, CapPath, CapRock, CapBed);
			for (int32 i = 0; i + 1 < CapRings.Num(); ++i)
			{
				if (!CapOk[i] || !CapOk[i + 1]) { continue; }
				for (int32 k = 0; k + 1 < CapPts; ++k)
				{
					const FVector& A0 = CapRings[i][k];
					const FVector& A1 = CapRings[i][k + 1];
					const FVector& B1 = CapRings[i + 1][k + 1];
					const FVector& B0 = CapRings[i + 1][k];
					// Verde (o el suelo del bioma) en lo llano; roca en lo empinado.
					const FVector FaceN = FVector::CrossProduct(B0 - A0, A1 - A0).GetSafeNormal();
					const float Flat = static_cast<float>(FMath::Clamp((FMath::Abs(FaceN.Z) - 0.55) / 0.35, 0.0, 1.0));
					const FLinearColor CapC = TNProcLerpColor(CapRock, CapGrass, Flat) * (0.92f + 0.12f * static_cast<float>(0.5 + 0.5 * TNProcHashNoise(i, k, CaveSeed + 7u)));
					Painted.AddQuad(A0, A1, B1, B0, FVector::UpVector, CapC);
				}
			}
		}

		// Cueva dentro de un volcán: su lago de magma (el LavaPool pequeño entre sus muestras).
		TNCaveDecor::FTNCaveMagma Magma;
		bool bMagma = false;
		if (CaveDetail::IsVolcanoCave(F))
		{
			for (const FFeature& P : Layout.Features)
			{
				if (P.Type == EFeature::LavaPool && P.PathIndex >= F.PathIndex && P.PathIndex <= F.Aux && P.Radius < 400.0)
				{
					Magma.Center = P.Location;
					Magma.Radius = P.Radius;
					Magma.Side = CaveDetail::MagmaSide(F);
					bMagma = true;
				}
			}
		}
		// La decoración se añade directamente al final de las secciones (antes se hacía aparte y se copiaba tal cual, sin
		// mover nada: el resultado es el mismo) para que cada objeto quede marcado como pieza de arte en ellas.
		TNCaveDecor::FTNCaveDecorOut Decor(Painted, PaintedFar, Glow, Lava, Beam, SlideWater);
		Decor.Log = &ArtLog;
		TNCaveDecor::TNCaveBuildDecor(Decor, Stations, Inner, NoFloor, F.Height, CaveSeed, Style, Look, bMagma ? &Magma : nullptr);
		CaveFlames.Append(Decor.Flames);
		CaveMotes.Append(Decor.Motes);

		// Luces sin sombras: las del túnel, la estatua, cristales, setas, antorchas y el foco del lucernario.
		for (const TNCaveDecor::FTNCaveLight& Def : Decor.Lights)
		{
			UPointLightComponent* Light = nullptr;
			if (Def.bSpot)
			{
				USpotLightComponent* Spot = NewObject<USpotLightComponent>(this, NAME_None, RF_Transient);
				Spot->SetInnerConeAngle(14.f);
				Spot->SetOuterConeAngle(30.f);
				Spot->SetRelativeRotation(FRotator(-90.0, 0.0, 0.0));
				Light = Spot;
			}
			else
			{
				Light = NewObject<UPointLightComponent>(this, NAME_None, RF_Transient);
			}
			Light->SetupAttachment(RootComponent);
			Light->SetRelativeLocation(Def.P);
			Light->SetIntensityUnits(ELightUnits::Lumens);
			Light->SetIntensity(Def.Lumens);
			Light->SetAttenuationRadius(Def.Radius);
			Light->SetLightColor(Def.Color);
			Light->SetCastShadows(false);
			Light->RegisterComponent();
			CaveLights.Add(Light);
		}
	}

	// ── Componentes ─────────────────────────────────────────────────────────
	// Cada sección se sube sin las piezas que tienen sustituto de arte (TNArt::UploadSection; sin sustitutos, como siempre).
	UMaterialInterface* VertexMat = TNMaterials::VertexColor();

	StructureMesh = NewObject<UProceduralMeshComponent>(this, NAME_None, RF_Transient);
	StructureMesh->SetupAttachment(RootComponent);
	StructureMesh->bUseAsyncCooking = true;
	StructureMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	StructureMesh->RegisterComponent();
	if (!Rock.IsEmpty())
	{
		TNArt::UploadSection(StructureMesh, 0, Rock, true, (Settings && Settings->RockMaterial) ? Settings->RockMaterial.Get() : VertexMat, &ArtLog);
	}
	if (!Wood.IsEmpty())
	{
		TNArt::UploadSection(StructureMesh, 1, Wood, true, (Settings && Settings->WoodMaterial) ? Settings->WoodMaterial.Get() : VertexMat, &ArtLog);
	}
	// Formaciones temáticas con su color de vértice: las del camino con colisión, los hitos lejanos sin ella.
	UMaterialInterface* PaintMat = ResolveMaterial(Settings ? Settings->TerrainMaterial.Get() : nullptr);
	if (!Painted.IsEmpty())
	{
		TNArt::UploadSection(StructureMesh, 2, Painted, true, PaintMat, &ArtLog);
	}
	if (!PaintedFar.IsEmpty())
	{
		TNArt::UploadSection(StructureMesh, 3, PaintedFar, false, PaintMat, &ArtLog);
	}

	DecorMesh = NewObject<UProceduralMeshComponent>(this, NAME_None, RF_Transient);
	DecorMesh->SetupAttachment(RootComponent);
	DecorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DecorMesh->SetCastShadow(false);
	DecorMesh->RegisterComponent();
	if (!Lava.IsEmpty())
	{
		TNArt::UploadSection(DecorMesh, 0, Lava, false, (Settings && Settings->LavaMaterial) ? Settings->LavaMaterial.Get() : VertexMat, &ArtLog);
	}
	if (!SlideWater.IsEmpty())
	{
		// Agua de cascada con ondas que corren ladera abajo (UV de flujo de la lámina); si no existe el
		// material, el de los ajustes.
		UMaterialInterface* SlideMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_ProcCascade.M_ProcCascade"));
		if (!SlideMat)
		{
			SlideMat = Settings && Settings->SlideWaterMaterial ? Settings->SlideWaterMaterial.Get()
				: (Settings && Settings->WaterMaterial ? Settings->WaterMaterial.Get() : VertexMat);
		}
		TNArt::UploadSection(DecorMesh, 1, SlideWater, false, SlideMat, &ArtLog);
	}
	if (!Foliage.IsEmpty())
	{
		TNArt::UploadSection(DecorMesh, 2, Foliage, false, VertexMat, &ArtLog);
	}
	// Lo que brilla en las cuevas (setas, cristales, llamas, ojos de la estatua, cielo del lucernario):
	// emisivo del color del vértice; sin el material, el de depuración (también sin iluminar).
	if (!Glow.IsEmpty())
	{
		UMaterialInterface* GlowMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_ProcGlow.M_ProcGlow"));
		TNArt::UploadSection(DecorMesh, 3, Glow, false, GlowMat ? GlowMat : VertexMat, &ArtLog);
	}
	// Haces de luz de los lucernarios: translúcido con la opacidad en el alfa del vértice.
	if (!Beam.IsEmpty())
	{
		UMaterialInterface* BeamMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_ProcFXSoft.M_ProcFXSoft"));
		if (BeamMat)
		{
			TNArt::UploadSection(DecorMesh, 4, Beam, false, BeamMat, &ArtLog);
		}
	}
	// La malla de arte de cada pieza con sustituto, en su sitio (hija de StructureMesh: los ejes del mapa, como DecorMesh).
	TNArt::SpawnPieceArt(StructureMesh, ArtLog);

	// ── Límites invisibles del mapa (la costa norte queda abierta hasta el mar) ─
	const double World = Layout.WorldSize;
	const double WorldX = Layout.WorldSizeX;
	const double Tall = 40000.0;
	double CoastMax = 0.0;
	for (int32 k = 0; k <= 40; ++k) { CoastMax = FMath::Max(CoastMax, Layout.CoastY(WorldX * k / 40.0)); }
	struct FWallDef { FVector Center; FVector Extent; };
	const FWallDef Walls[4] = {
		{ FVector(-300.0, World * 0.5, 0.0), FVector(300.0, World, Tall) },
		{ FVector(WorldX + 300.0, World * 0.5, 0.0), FVector(300.0, World, Tall) },
		{ FVector(WorldX * 0.5, -300.0, 0.0), FVector(WorldX, 300.0, Tall) },
		{ FVector(WorldX * 0.5, CoastMax + 16000.0, 0.0), FVector(WorldX, 300.0, Tall) } };
	for (const FWallDef& Def : Walls)
	{
		UBoxComponent* Wall = NewObject<UBoxComponent>(this, NAME_None, RF_Transient);
		Wall->SetupAttachment(RootComponent);
		Wall->SetBoxExtent(Def.Extent);
		Wall->SetCollisionProfileName(TEXT("InvisibleWall"));
		Wall->SetHiddenInGame(true);
		Wall->RegisterComponent();
		Wall->SetRelativeLocation(Def.Center);
		BoundaryWalls.Add(Wall);
	}

	// ── Efectos ambientales (solo visuales, locales): brasas sobre la lava y bandadas de pájaros ──
	TNAmbientFX::RemoveOwner(this);
	if (bTerrainOnly) { return; }
	for (const FFeature& F : Layout.Features)
	{
		const bool bPool = F.Type == EFeature::LavaPool;
		if (!bPool && !IsLavaGap(F)) { continue; }
		TNAmbientFX::FEmitterDesc Embers;
		Embers.Shape = TNAmbientFX::EShape::Ember;
		Embers.bSoft = true;
		Embers.Color = FLinearColor(1.f, 0.45f, 0.08f);
		Embers.Alpha = 0.95f;
		Embers.MaxParticles = 30;
		Embers.Rate = bPool ? 10.f : 6.f;
		Embers.SpawnRadius = static_cast<float>(bPool ? F.Radius * 0.6 : F.Width * 0.35);
		Embers.Speed = 150.f;
		Embers.Spread = 0.5f;
		Embers.Gravity = 0.f;
		Embers.Buoyancy = 45.f;
		Embers.Drag = 0.3f;
		Embers.LifeMin = 2.f;
		Embers.LifeMax = 4.f;
		Embers.SizeStart = 7.f;
		Embers.SizeEnd = 3.f;
		const double LavaZ = bPool ? F.Location.Z : F.Location.Z - 450.0;
		TNAmbientFX::AddEmitter(this, Embers, MapToWorld(FVector(F.Location.X, F.Location.Y, LavaZ + 20.0)));
	}
	// Antorchas de las cuevas: brasas que suben de la llama; en los lucernarios, polvo que flota en el haz.
	for (const FVector& Tip : CaveFlames)
	{
		TNAmbientFX::FEmitterDesc Sparks;
		Sparks.Shape = TNAmbientFX::EShape::Ember;
		Sparks.bSoft = true;
		Sparks.Color = FLinearColor(1.f, 0.55f, 0.12f);
		Sparks.Alpha = 0.95f;
		Sparks.MaxParticles = 12;
		Sparks.Rate = 5.f;
		Sparks.SpawnRadius = 5.f;
		Sparks.Speed = 60.f;
		Sparks.Spread = 0.4f;
		Sparks.Gravity = 0.f;
		Sparks.Buoyancy = 40.f;
		Sparks.Drag = 0.4f;
		Sparks.LifeMin = 0.6f;
		Sparks.LifeMax = 1.3f;
		Sparks.SizeStart = 4.f;
		Sparks.SizeEnd = 1.5f;
		Sparks.WakeDistance = 6000.f;
		TNAmbientFX::AddEmitter(this, Sparks, MapToWorld(Tip));
	}
	for (const FVector& Mote : CaveMotes)
	{
		TNAmbientFX::FEmitterDesc Dust;
		Dust.Shape = TNAmbientFX::EShape::Ember;
		Dust.bSoft = true;
		Dust.Color = FLinearColor(1.f, 0.95f, 0.8f);
		Dust.Alpha = 0.5f;
		Dust.MaxParticles = 30;
		Dust.Rate = 4.f;
		Dust.SpawnRadius = 140.f;
		Dust.Speed = 12.f;
		Dust.Spread = 1.f;
		Dust.Gravity = 0.f;
		Dust.Buoyancy = 2.f;
		Dust.Drag = 0.6f;
		Dust.LifeMin = 5.f;
		Dust.LifeMax = 9.f;
		Dust.SizeStart = 2.5f;
		Dust.SizeEnd = 2.f;
		Dust.WakeDistance = 5000.f;
		TNAmbientFX::AddEmitter(this, Dust, MapToWorld(Mote));
	}
	// Confeti de la meta (cuatro colores): estalla cuando alguien cruza la línea (ATN_ProcMapGenerator::Tick).
	for (const FFeature& F : Layout.Features)
	{
		if (F.Type != EFeature::Finish) { continue; }
		const FLinearColor Colors[4] = { FLinearColor(0.9f, 0.1f, 0.1f), FLinearColor(1.f, 0.8f, 0.05f), FLinearColor(0.1f, 0.4f, 0.95f), FLinearColor(0.1f, 0.75f, 0.25f) };
		for (const FLinearColor& Col : Colors)
		{
			TNAmbientFX::FEmitterDesc Confetti;
			Confetti.Shape = TNAmbientFX::EShape::Flake;
			Confetti.Color = Col;
			Confetti.MaxParticles = 70;
			Confetti.Rate = 0.f;
			Confetti.SpawnRadius = static_cast<float>(F.Radius * 0.8);
			Confetti.Speed = 900.f;
			Confetti.SpeedJitter = 0.4f;
			Confetti.Spread = 0.9f;
			Confetti.Gravity = -320.f;
			Confetti.Drag = 1.6f;
			Confetti.LifeMin = 3.f;
			Confetti.LifeMax = 5.f;
			Confetti.SizeStart = 12.f;
			Confetti.SizeEnd = 12.f;
			Confetti.WakeDistance = 30000.f;
			TNAmbientFX::AddEmitter(this, Confetti, MapToWorld(FVector(F.Location.X, F.Location.Y, 950.0)));
		}
	}
	{
		// Gaviotas sobre la playa de la meta y la costa; guacamayos en la selva; pájaros oscuros sobre los
		// bosques y la roca; buitres lentos en el desierto.
		FRng BirdRng(static_cast<uint64>(Layout.Params.Seed) * 0xB1ull + 17ull);
		for (const FFeature& F : Layout.Features)
		{
			if (F.Type != EFeature::Finish) { continue; }
			TNAmbientFX::AddFlock(this, MapToWorld(FVector(F.Location.X, F.Location.Y - 2500.0, 2800.0)), 7, 3200.f, 0.22f, 1.2f,
				FLinearColor(0.95f, 0.95f, 0.93f), FLinearColor(0.68f, 0.7f, 0.74f), 11u);
		}
		int32 Coastal = 0, Forest = 0, Desert = 0;
		for (const FModule& Mod : Layout.Modules)
		{
			const FVector2D C = Mod.Centroid;
			const double Ground = TerrainHeightMap(C);
			const uint32 Seed = static_cast<uint32>(BirdRng.RangeInt(1, 1 << 20));
			const float Dir = BirdRng.Chance(0.5) ? 1.f : -1.f;
			switch (Mod.Biome)
			{
				case ETNProcBiome::Beach:
					if (Coastal++ < 3)
					{
						TNAmbientFX::AddFlock(this, MapToWorld(FVector(C, Ground + BirdRng.Range(2500.0, 4000.0))), BirdRng.RangeInt(4, 7),
							static_cast<float>(BirdRng.Range(2500.0, 4200.0)), 0.2f * Dir, 1.15f, FLinearColor(0.95f, 0.95f, 0.93f), FLinearColor(0.68f, 0.7f, 0.74f), Seed);
					}
					break;
				case ETNProcBiome::Jungle:
				case ETNProcBiome::Mangrove:
					if (Forest++ < 6)
					{
						const bool bMacaw = Mod.Biome == ETNProcBiome::Jungle && BirdRng.Chance(0.6);
						TNAmbientFX::AddFlock(this, MapToWorld(FVector(C, Ground + BirdRng.Range(3000.0, 5000.0))), BirdRng.RangeInt(5, 9),
							static_cast<float>(BirdRng.Range(2800.0, 4800.0)), 0.3f * Dir, bMacaw ? 0.9f : 0.7f,
							bMacaw ? FLinearColor(0.85f, 0.12f, 0.08f) : FLinearColor(0.14f, 0.13f, 0.12f),
							bMacaw ? FLinearColor(0.1f, 0.45f, 0.85f) : FLinearColor(0.24f, 0.2f, 0.16f), Seed);
					}
					break;
				case ETNProcBiome::Water:
				case ETNProcBiome::Rocky:
					if (Forest++ < 6)
					{
						TNAmbientFX::AddFlock(this, MapToWorld(FVector(C, Ground + BirdRng.Range(3500.0, 6000.0))), BirdRng.RangeInt(6, 10),
							static_cast<float>(BirdRng.Range(3000.0, 5500.0)), 0.28f * Dir, 0.65f, FLinearColor(0.14f, 0.13f, 0.12f), FLinearColor(0.24f, 0.2f, 0.16f), Seed);
					}
					break;
				case ETNProcBiome::Desert:
					if (Desert++ < 2)
					{
						TNAmbientFX::AddFlock(this, MapToWorld(FVector(C, Ground + BirdRng.Range(5000.0, 7000.0))), BirdRng.RangeInt(3, 5),
							static_cast<float>(BirdRng.Range(3500.0, 5000.0)), 0.09f * Dir, 1.7f, FLinearColor(0.2f, 0.15f, 0.1f), FLinearColor(0.3f, 0.22f, 0.14f), Seed);
					}
					break;
				default:
					break;
			}
		}
	}

	// Fauna ambiental (solo visual y local). Clear() la destruye al regenerar; espera sola a que el mapa esté listo.
	if (GetWorld() && GetWorld()->IsGameWorld() && GetNetMode() != NM_DedicatedServer)
	{
		if (ATN_ProcFauna* FaunaActor = Cast<ATN_ProcFauna>(SpawnMapActor(ATN_ProcFauna::StaticClass(), GetActorTransform(), false)))
		{
			FaunaActor->Init(this, Layout.Params.Seed);
		}
	}
}
