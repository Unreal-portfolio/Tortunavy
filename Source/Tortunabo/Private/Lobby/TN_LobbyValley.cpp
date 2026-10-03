// ─────────────────────────────────────────────────────────────────────────────
// ATN_LobbyValley — el valle de biomas alrededor del castillo del lobby: terreno de
// caras planas (sectores del reloj, sierra y cordillera lejana), formaciones y casitas,
// agua, lava y cascada, vegetación instanciada y efectos. La fauna y los pájaros van
// en TN_LobbyValley_Fauna.cpp; la forma del terreno, en TN_LobbyValleyTerrain.h.
// ─────────────────────────────────────────────────────────────────────────────

#include "Lobby/TN_LobbyValley.h"
#include "Lobby/TN_SandCastleLobby.h"
#include "Art/TN_Art.h"
#include "Core/TN_Log.h"
#include "CoreGlobals.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "World/ProcMap/TN_ProcMapTypes.h"
#include "TN_LobbyValleyTerrain.h"
#include "../Art/TN_ArtPieces.h"
#include "../World/ProcMap/TN_ProcMapAmbientFX.h"
#include "../World/ProcMap/TN_ProcMapFloraMeshes.h"
#include "../World/ProcMap/TN_ProcMapFormationMeshes.h"
#include "../World/ProcMap/TN_ProcMapPropMeshes.h"
#include "../World/ProcMap/TN_ProcMapRockMeshes.h"
#include "../World/ProcMap/TN_ProcMapRuntimeMesh.h"

namespace TNValleyBuild
{
	using namespace TNLobbyValley;
	using TNProcMesh::FTNProcMeshBuffers;

	TAutoConsoleVariable<int32> CVarLobbyValley(TEXT("TN.Lobby.Valley"), 1,
		TEXT("1 = valle de biomas alrededor del castillo del lobby (ATN_LobbyValley); 0 = escondido (vuelve a cargar el lobby)."));

	/** Etiqueta de los componentes que crea el valle (se quitan aunque se pierda su lista). */
	FName GeneratedTag()
	{
		static const FName Tag(TEXT("TNValleyGen"));
		return Tag;
	}

	/** Grupo de las mallas de arte de las formaciones, agujas y casitas (TNArt::FPieceLog del terreno). */
	FName ArtGroup()
	{
		static const FName Group(TEXT("Valley"));
		return Group;
	}

	/** Versión de la construcción: al cambiarla, los valles ya construidos en el editor se rehacen. */
	constexpr uint32 BuildVersion = 3u;

	/** Formación del mapa procedural colocada en un sector (mira al castillo). */
	struct FLandmark
	{
		double Hour;
		double Dist;
		TNProcMap::EFormation Kind;
		double Radius;
		double Height;
		int32 Sector;
	};

	const FLandmark Landmarks[] = {
		// Laguna (las 12): palafito en la orilla de la isleta grande.
		{ 11.8, 8700.0, TNProcMap::EFormation::StiltHut, 420.0, 650.0, 0 },
		// Playa (la 1): faro en el cabo, caracola gigante y un barco varado.
		{ 1.33, 11800.0, TNProcMap::EFormation::Lighthouse, 400.0, 3100.0, 1 },
		{ 0.86, 7700.0, TNProcMap::EFormation::GiantShell, 380.0, 720.0, 1 },
		{ 1.07, 9200.0, TNProcMap::EFormation::Shipwreck, 850.0, 450.0, 1 },
		// Dunas (las 2): la tortuga colosal, un obelisco y un cráneo fósil.
		{ 2.02, 10300.0, TNProcMap::EFormation::ColossalTurtle, 760.0, 830.0, 2 },
		{ 2.38, 12500.0, TNProcMap::EFormation::Obelisk, 190.0, 1400.0, 2 },
		{ 1.72, 8300.0, TNProcMap::EFormation::FossilSkull, 430.0, 340.0, 2 },
		// Cañón (las 3): chimeneas de hadas, una roca en equilibrio y una mesa al fondo.
		{ 2.78, 8400.0, TNProcMap::EFormation::Hoodoo, 230.0, 1000.0, 3 },
		{ 3.22, 9300.0, TNProcMap::EFormation::Hoodoo, 200.0, 820.0, 3 },
		{ 2.9, 11200.0, TNProcMap::EFormation::Hoodoo, 260.0, 1250.0, 3 },
		{ 3.03, 7600.0, TNProcMap::EFormation::BalancedRock, 260.0, 720.0, 3 },
		{ 3.2, 13600.0, TNProcMap::EFormation::Mesa, 2600.0, 2400.0, 3 },
		// Volcán (las 4): agujas de obsidiana, columnas de basalto y una fumarola.
		{ 4.38, 8300.0, TNProcMap::EFormation::ObsidianSpires, 450.0, 950.0, 4 },
		{ 3.95, 7500.0, TNProcMap::EFormation::BasaltColumns, 520.0, 620.0, 4 },
		{ 4.46, 10700.0, TNProcMap::EFormation::Fumarole, 320.0, 240.0, 4 },
		// Acantilados (las 5): castillo en ruinas en lo alto.
		{ 5.2, 12900.0, TNProcMap::EFormation::CastleRuin, 1700.0, 1350.0, 5 },
		// Bosque (las 7): círculo de piedras en un claro.
		{ 7.02, 8800.0, TNProcMap::EFormation::StoneCircle, 650.0, 380.0, 7 },
		// Pueblo (las 8): molino en la colina grande y depósito de agua.
		{ 8.22, 11600.0, TNProcMap::EFormation::Windmill, 420.0, 1700.0, 8 },
		{ 7.66, 8900.0, TNProcMap::EFormation::WaterTower, 300.0, 1000.0, 8 },
		// Granjas (las 9): otro molino en su loma.
		{ 9.35, 12200.0, TNProcMap::EFormation::Windmill, 420.0, 1500.0, 9 },
		// Selva (las 10): pirámide escalonada y cabeza de piedra.
		{ 10.05, 12900.0, TNProcMap::EFormation::Pyramid, 1900.0, 1600.0, 10 },
		{ 9.73, 8200.0, TNProcMap::EFormation::StoneHead, 320.0, 560.0, 10 },
		// Manglar (las 11): palafito.
		{ 11.18, 8900.0, TNProcMap::EFormation::StiltHut, 420.0, 650.0, 11 },
	};

	/** Aguja o peñasco de roca del mapa procedural (bLookout: en su cima se posa el águila). */
	struct FSpireDef
	{
		double Hour;
		double Dist;
		TNRockMesh::ESpireStyle Style;
		double Radius;
		double Height;
		int32 Sector;
		bool bLookout;
	};

	const FSpireDef Spires[] = {
		{ 4.82, 8700.0, TNRockMesh::ESpireStyle::Twin, 380.0, 1800.0, 5, false },
		{ 5.45, 9500.0, TNRockMesh::ESpireStyle::Leaning, 320.0, 1500.0, 5, false },
		{ 5.06, 10500.0, TNRockMesh::ESpireStyle::Spire, 300.0, 2000.0, 5, true },
		{ 5.82, 8900.0, TNRockMesh::ESpireStyle::Tor, 420.0, 700.0, 6, false },
		{ 6.34, 9900.0, TNRockMesh::ESpireStyle::Tor, 380.0, 620.0, 6, false },
		{ 10.36, 8900.0, TNRockMesh::ESpireStyle::Karst, 400.0, 1900.0, 10, false },
		{ 10.62, 10400.0, TNRockMesh::ESpireStyle::Karst, 350.0, 1600.0, 10, false },
		{ 9.86, 11000.0, TNRockMesh::ESpireStyle::Karst, 380.0, 2100.0, 10, false },
		{ 4.64, 12900.0, TNRockMesh::ESpireStyle::LavaDome, 700.0, 900.0, 4, false },
	};

	// ── Piezas de arte (Docs/Arte_Assets.md): formaciones, agujas, casitas y vegetación se pueden sustituir ──

	/** Pieza de arte de cada formación de Landmarks. */
	FName LandmarkSlot(TNProcMap::EFormation Kind)
	{
		using EF = TNProcMap::EFormation;
		switch (Kind)
		{
			case EF::StiltHut:       return TN_ART("Lobby.Valley.Landmark.StiltHut");
			case EF::Lighthouse:     return TN_ART("Lobby.Valley.Landmark.Lighthouse");
			case EF::GiantShell:     return TN_ART("Lobby.Valley.Landmark.GiantShell");
			case EF::Shipwreck:      return TN_ART("Lobby.Valley.Landmark.Shipwreck");
			case EF::ColossalTurtle: return TN_ART("Lobby.Valley.Landmark.ColossalTurtle");
			case EF::Obelisk:        return TN_ART("Lobby.Valley.Landmark.Obelisk");
			case EF::FossilSkull:    return TN_ART("Lobby.Valley.Landmark.FossilSkull");
			case EF::Hoodoo:         return TN_ART("Lobby.Valley.Landmark.Hoodoo");
			case EF::BalancedRock:   return TN_ART("Lobby.Valley.Landmark.BalancedRock");
			case EF::Mesa:           return TN_ART("Lobby.Valley.Landmark.Mesa");
			case EF::ObsidianSpires: return TN_ART("Lobby.Valley.Landmark.ObsidianSpires");
			case EF::BasaltColumns:  return TN_ART("Lobby.Valley.Landmark.BasaltColumns");
			case EF::Fumarole:       return TN_ART("Lobby.Valley.Landmark.Fumarole");
			case EF::CastleRuin:     return TN_ART("Lobby.Valley.Landmark.CastleRuin");
			case EF::StoneCircle:    return TN_ART("Lobby.Valley.Landmark.StoneCircle");
			case EF::Windmill:       return TN_ART("Lobby.Valley.Landmark.Windmill");
			case EF::WaterTower:     return TN_ART("Lobby.Valley.Landmark.WaterTower");
			case EF::Pyramid:        return TN_ART("Lobby.Valley.Landmark.Pyramid");
			case EF::StoneHead:      return TN_ART("Lobby.Valley.Landmark.StoneHead");
			default:                 return NAME_None;
		}
	}

	/**
	 * Pivote de Landmarks[Index]: su centro en el suelo, +X hacia el castillo. Escala 1 = la primera de su tipo en la tabla;
	 * las demás se estiran a su radio y su alto.
	 */
	FTransform LandmarkPivot(int32 Index, const FVector2D& C, double OriginZ, const FVector2D& Dx)
	{
		const FLandmark& Lm = Landmarks[Index];
		const FLandmark* Ref = &Lm;
		for (const FLandmark& Other : Landmarks)
		{
			if (Other.Kind == Lm.Kind)
			{
				Ref = &Other;
				break;
			}
		}
		const double S = Lm.Radius / Ref->Radius;
		return FTransform(FRotator(0.0, FMath::RadiansToDegrees(FMath::Atan2(Dx.Y, Dx.X)), 0.0), FVector(C, OriginZ), FVector(S, S, Lm.Height / Ref->Height));
	}

	/** Pieza de arte de cada estilo de aguja o peñasco de Spires. */
	FName SpireSlot(TNRockMesh::ESpireStyle Style)
	{
		using ESS = TNRockMesh::ESpireStyle;
		switch (Style)
		{
			case ESS::Twin:     return TN_ART("Lobby.Valley.Rock.TwinSpire");
			case ESS::Leaning:  return TN_ART("Lobby.Valley.Rock.LeaningSpire");
			case ESS::Spire:    return TN_ART("Lobby.Valley.Rock.Spire");
			case ESS::Tor:      return TN_ART("Lobby.Valley.Rock.Tor");
			case ESS::Karst:    return TN_ART("Lobby.Valley.Rock.KarstPillar");
			case ESS::LavaDome: return TN_ART("Lobby.Valley.Rock.LavaDome");
			default:            return NAME_None;
		}
	}

	/**
	 * Pivote de Spires[Index]: centro de la base (lo más bajo de su pie), ejes del valle. Escala 1 = la primera de su estilo en
	 * la tabla; las demás se estiran a su radio y su alto.
	 */
	FTransform SpirePivot(int32 Index, const FVector2D& C, double BaseZ)
	{
		const FSpireDef& Sd = Spires[Index];
		const FSpireDef* Ref = &Sd;
		for (const FSpireDef& Other : Spires)
		{
			if (Other.Style == Sd.Style)
			{
				Ref = &Other;
				break;
			}
		}
		const double S = Sd.Radius / Ref->Radius;
		return FTransform(FRotator::ZeroRotator, FVector(C, BaseZ), FVector(S, S, Sd.Height / Ref->Height));
	}

	/** Pieza de arte de cada especie de la vegetación del valle (sus variantes y biomas comparten la malla de arte). */
	FName FloraSlot(TNProcMap::EFloraShape Shape, TNProcMap::EPropKind Prop)
	{
		using ES = TNProcMap::EFloraShape;
		switch (Shape)
		{
			case ES::Willow:       return TN_ART("Lobby.Valley.Flora.Willow");
			case ES::BroadTree:    return TN_ART("Lobby.Valley.Flora.BroadTree");
			case ES::Birch:        return TN_ART("Lobby.Valley.Flora.Birch");
			case ES::Palm:         return TN_ART("Lobby.Valley.Flora.Palm");
			case ES::Reeds:        return TN_ART("Lobby.Valley.Flora.Reeds");
			case ES::Cypress:      return TN_ART("Lobby.Valley.Flora.Cypress");
			case ES::Bush:         return TN_ART("Lobby.Valley.Flora.Bush");
			case ES::Rock:         return TN_ART("Lobby.Valley.Flora.Rock");
			case ES::Casuarina:    return TN_ART("Lobby.Valley.Flora.Casuarina");
			case ES::SeaGrape:     return TN_ART("Lobby.Valley.Flora.SeaGrape");
			case ES::FanPalm:      return TN_ART("Lobby.Valley.Flora.FanPalm");
			case ES::Pandanus:     return TN_ART("Lobby.Valley.Flora.Pandanus");
			case ES::Saguaro:      return TN_ART("Lobby.Valley.Flora.Saguaro");
			case ES::JoshuaTree:   return TN_ART("Lobby.Valley.Flora.JoshuaTree");
			case ES::Barrel:       return TN_ART("Lobby.Valley.Flora.BarrelCactus");
			case ES::DryBush:      return TN_ART("Lobby.Valley.Flora.DryBush");
			case ES::DeadTree:     return TN_ART("Lobby.Valley.Flora.DeadTree");
			case ES::Acacia:       return TN_ART("Lobby.Valley.Flora.Acacia");
			case ES::CharredTree:  return TN_ART("Lobby.Valley.Flora.CharredTree");
			case ES::AshBush:      return TN_ART("Lobby.Valley.Flora.AshBush");
			case ES::Pine:         return TN_ART("Lobby.Valley.Flora.Pine");
			case ES::Fir:          return TN_ART("Lobby.Valley.Flora.Fir");
			case ES::Fern:         return TN_ART("Lobby.Valley.Flora.Fern");
			case ES::Ornamental:   return TN_ART("Lobby.Valley.Flora.Ornamental");
			case ES::Hedge:        return TN_ART("Lobby.Valley.Flora.Hedge");
			case ES::Ceiba:        return TN_ART("Lobby.Valley.Flora.Ceiba");
			case ES::TreeFern:     return TN_ART("Lobby.Valley.Flora.TreeFern");
			case ES::Bamboo:       return TN_ART("Lobby.Valley.Flora.Bamboo");
			case ES::BananaPlant:  return TN_ART("Lobby.Valley.Flora.BananaPlant");
			case ES::MangroveTree: return TN_ART("Lobby.Valley.Flora.MangroveTree");
			case ES::Prop:
				if (Prop == TNProcMap::EPropKind::HayBale) { return TN_ART("Lobby.Valley.Flora.HayBale"); }
				return NAME_None;
			default:               return NAME_None;
		}
	}

	/** Colores del mapa procedural de un bioma (suelo y roca), para las formaciones y la vegetación. */
	void BiomeColors(ETNProcBiome Biome, FLinearColor& OutGround, FLinearColor& OutRock)
	{
		FLinearColor PathC, BedC;
		TN_DefaultBiomeColors(Biome, OutGround, PathC, OutRock, BedC);
	}

	bool InKeepOut(const TArray<FVector>& KeepOut, const FVector2D& P, double Extra)
	{
		for (const FVector& K : KeepOut)
		{
			if (FVector2D::DistSquared(P, FVector2D(K.X, K.Y)) < FMath::Square(K.Z + Extra)) { return true; }
		}
		return false;
	}

	/** Sube unos buffers como sección de una malla procedural sin colisión (sin las piezas de Log que tienen sustituto de arte). */
	void UploadSection(UProceduralMeshComponent* Comp, int32 Section, const FTNProcMeshBuffers& B, UMaterialInterface* Mat, const TNArt::FPieceLog* Log = nullptr)
	{
		if (!Comp || B.IsEmpty()) { return; }
		TNArt::UploadSection(Comp, Section, B, false, Mat, Log);
	}

	/**
	 * Casita de caras planas (o granero): paredes encaladas de colores sobre un zócalo de piedra, tejado a dos aguas con
	 * alero, hastiales, puerta, ventanas y chimenea (su boca va a OutChimneys, si se pide). Se asienta en lo más alto de su
	 * planta y el zócalo baja hasta lo más bajo. Con Log, la casa entera es una pieza de arte.
	 */
	void AddCottage(FTNProcMeshBuffers& M, const FTNLobbyValleyGrid& G, const FVector2D& C, double Yaw, double Scale, uint32 HouseSeed, bool bBarn,
		TArray<FVector>* OutChimneys, TNArt::FPieceLog* Log)
	{
		const FVector2D Dx(FMath::Cos(Yaw), FMath::Sin(Yaw));
		const FVector2D Dy(-Dx.Y, Dx.X);
		const double W = (bBarn ? 700.0 : 460.0) * Scale;
		const double D = (bBarn ? 520.0 : 360.0) * Scale;
		const double WallH = (bBarn ? 380.0 : 280.0) * Scale;
		const double Rise = (bBarn ? 300.0 : 210.0) * Scale;
		const double Eave = 35.0 * Scale;
		double Low = 1.0e9;
		double High = -1.0e9;
		for (const double Sx : { -1.0, 1.0 })
		{
			for (const double Sy : { -1.0, 1.0 })
			{
				const double Hc = G.HeightAt(C + Dx * (Sx * W * 0.5) + Dy * (Sy * D * 0.5));
				Low = FMath::Min(Low, Hc);
				High = FMath::Max(High, Hc);
			}
		}
		const double FloorZ = High + 8.0;
		const double FootZ = Low - 60.0;
		// Pieza de arte: centro de la planta a la altura del suelo de la casa, +X hacia la puerta; escala 1 = la de tamaño 1.
		TNArt::FPieceScope Piece(Log, bBarn ? TN_ART("Lobby.Valley.Barn") : TN_ART("Lobby.Valley.Cottage"),
			FTransform(FRotator(0.0, FMath::RadiansToDegrees(Yaw), 0.0), FVector(C, FloorZ), FVector(Scale)), { &M });
		auto L = [&C, &Dx, &Dy](double X, double Y, double Z) { return FVector(C + Dx * X + Dy * Y, Z); };
		const FVector Ax(Dx.X, Dx.Y, 0.0);
		const FVector Ay(Dy.X, Dy.Y, 0.0);
		const FVector Up(0.0, 0.0, 1.0);
		static const uint32 WallHex[5] = { 0xFFF4E0u, 0xFBE7C6u, 0xF6D2C4u, 0xDDEBF2u, 0xF3E3A0u };
		static const uint32 RoofHex[5] = { 0xD9483Bu, 0xC75B2Au, 0x3E6FB0u, 0x4E8A4Au, 0x8A5A3Cu };
		const FLinearColor WallC = ValleyHex(bBarn ? 0xB8412Eu : WallHex[HouseSeed % 5u]);
		const FLinearColor RoofC = ValleyHex(bBarn ? 0x5B4A44u : RoofHex[(HouseSeed / 5u) % 5u]);
		const FLinearColor Stone = ValleyHex(0xA89F92u);
		const FLinearColor DoorC = ValleyHex(bBarn ? 0xFFF6E8u : 0x7A4E2Bu);
		const FLinearColor WinC = ValleyHex(0x8FCBEAu);
		const FLinearColor Chimney = ValleyHex(0xB8A89Au);

		// Zócalo y paredes.
		M.AddBox(L(0.0, 0.0, (FootZ + FloorZ + 20.0) * 0.5), Ax, FVector(W * 0.5 + 8.0, D * 0.5 + 8.0, (FloorZ + 20.0 - FootZ) * 0.5), Stone);
		M.AddBox(L(0.0, 0.0, FloorZ + WallH * 0.5), Ax, FVector(W * 0.5, D * 0.5, WallH * 0.5), WallC);
		// Tejado a dos aguas (con la cara de abajo para que no se vea a través) y cumbrera.
		const double Zr = FloorZ + WallH;
		for (const double Side : { -1.0, 1.0 })
		{
			const FVector R0 = L(-W * 0.5 - Eave, 0.0, Zr + Rise);
			const FVector R1 = L(W * 0.5 + Eave, 0.0, Zr + Rise);
			const FVector E1 = L(W * 0.5 + Eave, Side * (D * 0.5 + Eave), Zr - Eave * 0.55);
			const FVector E0 = L(-W * 0.5 - Eave, Side * (D * 0.5 + Eave), Zr - Eave * 0.55);
			M.AddQuad(R0, R1, E1, E0, Ay * Side + Up, RoofC);
			M.AddQuad(R0, R1, E1, E0, -(Ay * Side + Up), RoofC * 0.6f);
		}
		M.AddBox(L(0.0, 0.0, Zr + Rise + 6.0 * Scale), Ax, FVector(W * 0.5 + Eave + 6.0, 10.0 * Scale, 8.0 * Scale), RoofC * 0.8f);
		for (const double End : { -1.0, 1.0 })
		{
			M.AddTri(L(End * W * 0.5, -D * 0.5, Zr), L(End * W * 0.5, D * 0.5, Zr), L(End * W * 0.5, 0.0, Zr + Rise), Ax * End, WallC * 0.96f);
		}
		// Puerta en el hastial de delante (+X) y ventanas.
		const double DoorH = (bBarn ? 300.0 : 200.0) * Scale;
		M.AddBox(L(W * 0.5 + 3.0, 0.0, FloorZ + DoorH * 0.5), Ax, FVector(4.0, (bBarn ? 120.0 : 50.0) * Scale, DoorH * 0.5), DoorC);
		if (bBarn)
		{
			// Portón del pajar encima de la puerta.
			M.AddBox(L(W * 0.5 + 3.0, 0.0, Zr + Rise * 0.3), Ax, FVector(4.0, 55.0 * Scale, 50.0 * Scale), ValleyHex(0x4A2E22u));
		}
		else
		{
			for (const double Side : { -1.0, 1.0 })
			{
				for (const double X : { -0.22, 0.22 })
				{
					M.AddBox(L(W * X, Side * (D * 0.5 + 2.0), FloorZ + WallH * 0.58), Ax, FVector(42.0 * Scale, 3.0, 36.0 * Scale), WinC);
				}
				M.AddBox(L(W * 0.5 + 2.0, Side * D * 0.3, FloorZ + WallH * 0.58), Ax, FVector(3.0, 30.0 * Scale, 30.0 * Scale), WinC);
			}
			// Chimenea.
			const double Cz0 = Zr + Rise * 0.35;
			const double Cz1 = Zr + Rise + 70.0 * Scale;
			M.AddBox(L(-W * 0.22, D * 0.2, (Cz0 + Cz1) * 0.5), Ax, FVector(24.0 * Scale, 24.0 * Scale, (Cz1 - Cz0) * 0.5), Chimney);
			if (OutChimneys) { OutChimneys->Add(L(-W * 0.22, D * 0.2, Cz1 + 10.0)); }
		}
	}

	/** Formaciones del mapa procedural, agujas de roca y casitas en sus sectores (sección 1 del terreno), cada una pieza de arte. */
	void BuildLandmarks(FTNProcMeshBuffers& Painted, const FTNLobbyValleyGrid& G, uint32 InSeed, TArray<FVector>& KeepOut, TArray<FVector>& Lookouts,
		TArray<FVector>& Chimneys, TNArt::FPieceLog& Log)
	{
		for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(Landmarks)); ++i)
		{
			const FLandmark& Lm = Landmarks[i];
			const ETNProcBiome Biome = ValleySectorAt(Lm.Sector).Biome;
			FLinearColor GroundC, RockC;
			BiomeColors(Biome, GroundC, RockC);
			const FVector2D C = ValleyClockPoint(Lm.Hour, Lm.Dist);
			const double OriginZ = G.HeightAt(C);
			const FVector2D Dx = (-C).GetSafeNormal();
			const FVector2D Dy(-Dx.Y, Dx.X);
			auto Ground = [&G, C, Dx, Dy, OriginZ](double X, double Y) { return G.HeightAt(C + Dx * X + Dy * Y) - OriginZ; };
			TNFormMesh::FTNFormParams Params;
			Params.Radius = Lm.Radius;
			Params.Height = Lm.Height;
			Params.Width = Lm.Radius * 2.0;
			Params.Length = Lm.Radius * 2.0;
			Params.Seed = TNProcMap::HashCell(InSeed ^ 0x1A2Du, i, 7);
			Params.WaterZ = WaterZ - OriginZ;
			FTNProcMeshBuffers Local;
			TNFormMesh::TNFormBuild(Local, Lm.Kind, Params, TNFormMesh::TNFormColorsFor(Biome, RockC), Ground);
			{
				TNArt::FPieceScope Piece(Log, TNValleyBuild::LandmarkSlot(Lm.Kind), TNValleyBuild::LandmarkPivot(i, C, OriginZ, Dx), { &Painted });
				TNFormMesh::TNFormAppend(Painted, Local, FVector(C, OriginZ), Dx);
			}
			KeepOut.Add(FVector(C.X, C.Y, Lm.Radius * 1.35 + 250.0));
		}

		for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(Spires)); ++i)
		{
			const FSpireDef& Sd = Spires[i];
			const ETNProcBiome Biome = ValleySectorAt(Sd.Sector).Biome;
			FLinearColor GroundC, RockC;
			BiomeColors(Biome, GroundC, RockC);
			const FVector2D C = ValleyClockPoint(Sd.Hour, Sd.Dist);
			// Asentada en lo más bajo de su pie (en las terrazas, un lado queda más bajo).
			double BaseZ = G.HeightAt(C);
			for (int32 k = 0; k < 6; ++k)
			{
				const double A = TNProcMap::TwoPi * k / 6.0;
				BaseZ = FMath::Min(BaseZ, G.HeightAt(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Sd.Radius));
			}
			FTNProcMeshBuffers Local;
			TNRockMesh::TNRockBuildSpire(Local, Sd.Style, Sd.Radius, Sd.Height, TNProcMap::HashCell(InSeed ^ 0x5A1Eu, i, 3), TNRockMesh::TNRockColorsFor(Biome, RockC, GroundC));
			{
				TNArt::FPieceScope Piece(Log, TNValleyBuild::SpireSlot(Sd.Style), TNValleyBuild::SpirePivot(i, C, BaseZ), { &Painted });
				TNFormMesh::TNFormAppend(Painted, Local, FVector(C, BaseZ), FVector2D(1.0, 0.0));
			}
			KeepOut.Add(FVector(C.X, C.Y, Sd.Radius * 1.5 + 200.0));
			if (Sd.bLookout)
			{
				// Cima: el peñasco que corona la aguja.
				Lookouts.Add(FVector(C, BaseZ + Sd.Height - 95.0 + 1.05 * Sd.Radius));
			}
		}

		// Pueblo (las 8): casitas por las colinas, mirando más o menos al castillo; las dos primeras echan humo.
		TNProcMap::FRng Rng(static_cast<uint64>(InSeed) * 0x40A5Eull + 3ull);
		int32 Placed = 0;
		for (int32 Try = 0; Try < 300 && Placed < 10; ++Try)
		{
			const FVector2D C = ValleyClockPoint(Rng.Range(7.5, 8.52), Rng.Range(7300.0, 13300.0));
			const double HouseScale = Rng.Range(0.9, 1.15);
			if (InKeepOut(KeepOut, C, 380.0 * HouseScale) || G.NormalAt(C).Z < 0.9) { continue; }
			const double Yaw = FMath::Atan2(-C.Y, -C.X) + Rng.Range(-0.6, 0.6);
			AddCottage(Painted, G, C, Yaw, HouseScale, static_cast<uint32>(Rng.RangeInt(0, 1 << 20)), false, Placed < 2 ? &Chimneys : nullptr, &Log);
			KeepOut.Add(FVector(C.X, C.Y, 380.0 * HouseScale));
			++Placed;
		}
		// Cabañas en la nieve y en el bosque, y el granero rojo de las granjas.
		struct FCabin
		{
			double Hour;
			double Dist;
			bool bBarn;
			double CabinScale;
		};
		const FCabin Cabins[] = { { 6.12, 10400.0, false, 1.0 }, { 6.78, 10900.0, false, 1.0 }, { 9.02, 9300.0, true, 1.25 } };
		for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(Cabins)); ++i)
		{
			const FCabin& Cb = Cabins[i];
			const FVector2D C = ValleyClockPoint(Cb.Hour, Cb.Dist);
			AddCottage(Painted, G, C, FMath::Atan2(-C.Y, -C.X), Cb.CabinScale, TNProcMap::HashCell(InSeed ^ 0xCAB1u, i, 1), Cb.bBarn, nullptr, &Log);
			KeepOut.Add(FVector(C.X, C.Y, (Cb.bBarn ? 560.0 : 380.0) * Cb.CabinScale));
		}
	}

	/**
	 * Superficie del agua: un sector de corona de las 10:15 a la 1:45 (laguna y manglar); el terreno la tapa donde sube. El
	 * color de vértice (turquesa) solo se ve si falta el material del agua animada.
	 */
	void BuildWaterSurface(FTNProcMeshBuffers& Out)
	{
		constexpr double H0 = 10.25;
		constexpr double H1 = 13.75;
		constexpr int32 Steps = 60;
		static const double Rings[] = { 4150.0, 5000.0, 6000.0, 7200.0, 8600.0, 10200.0, 12000.0, 13800.0, 15700.0 };
		const FLinearColor Turquoise = ValleyHex(0x2FB5C8u);
		for (int32 s = 0; s < Steps; ++s)
		{
			const double Ha = FMath::Lerp(H0, H1, static_cast<double>(s) / Steps);
			const double Hb = FMath::Lerp(H0, H1, static_cast<double>(s + 1) / Steps);
			for (int32 r = 0; r + 1 < static_cast<int32>(UE_ARRAY_COUNT(Rings)); ++r)
			{
				const FVector A(ValleyClockPoint(Ha, Rings[r]), WaterZ);
				const FVector B(ValleyClockPoint(Hb, Rings[r]), WaterZ);
				const FVector C(ValleyClockPoint(Hb, Rings[r + 1]), WaterZ);
				const FVector D(ValleyClockPoint(Ha, Rings[r + 1]), WaterZ);
				Out.AddQuad(A, B, C, D, FVector::UpVector, Turquoise);
			}
		}
	}

	/** Río de lava ladera abajo desde el borde de un cráter: sigue la pendiente, se ensancha y brilla en el centro. */
	void AddLavaRiver(FTNProcMeshBuffers& Out, const FTNLobbyValleyGrid& G, const FVector2D& Center, double StartR, double EndR, FVector2D Dir,
		double W0, double W1, TArray<FVector>& KeepOut)
	{
		static const FLinearColor Core(1.f, 0.42f, 0.06f);
		static const FLinearColor Crust(0.45f, 0.07f, 0.02f);
		TArray<FVector2D> Pts;
		FVector2D P = Center + Dir * StartR;
		Pts.Add(P);
		for (int32 s = 0; s < 60; ++s)
		{
			const FVector N = G.NormalAt(P);
			const FVector2D Down(N.X, N.Y);
			if (!Down.IsNearlyZero()) { Dir = (Dir * 0.55 + Down.GetSafeNormal() * 0.45).GetSafeNormal(); }
			P += Dir * 240.0;
			Pts.Add(P);
			if (FVector2D::Distance(P, Center) > EndR) { break; }
		}
		const int32 Num = Pts.Num();
		if (Num < 2) { return; }
		static const double Across[4] = { -0.5, -0.18, 0.18, 0.5 };
		TArray<FVector> Row;
		TArray<FVector> Prev;
		for (int32 i = 0; i < Num; ++i)
		{
			const FVector2D Dn = (Pts[FMath::Min(i + 1, Num - 1)] - Pts[FMath::Max(i - 1, 0)]).GetSafeNormal();
			const FVector2D Sd(-Dn.Y, Dn.X);
			const double T = static_cast<double>(i) / (Num - 1);
			const double Width = FMath::Lerp(W0, W1, T) * (0.85 + 0.15 * FMath::Sin(i * 1.7));
			Row.Reset();
			for (int32 j = 0; j < 4; ++j)
			{
				const FVector2D Q = Pts[i] + Sd * (Across[j] * Width);
				Row.Add(FVector(Q, G.HeightAt(Q) + 30.0));
			}
			if (i > 0)
			{
				for (int32 j = 0; j < 3; ++j)
				{
					Out.AddQuad(Prev[j], Prev[j + 1], Row[j + 1], Row[j], FVector::UpVector, j == 1 ? Core : Crust);
				}
			}
			Prev = Row;
			if (i % 2 == 0) { KeepOut.Add(FVector(Pts[i].X, Pts[i].Y, Width * 0.6 + 150.0)); }
		}
	}

	/** Lava de los dos volcanes: lago en el cráter y ríos que bajan hacia el valle. Devuelve la boca de cada cráter. */
	void BuildLava(FTNProcMeshBuffers& Out, const FTNLobbyValleyGrid& G, const FValleyShape& Shape, FVector& OutCraterTop, FVector& OutSmallTop,
		TArray<FVector>& KeepOut)
	{
		static const FLinearColor Core(1.f, 0.46f, 0.07f);
		static const FLinearColor Hot(1.f, 0.72f, 0.2f);
		for (int32 c = 0; c < NumValleyCones; ++c)
		{
			const FValleyCone& Cn = ValleyCone(c);
			const FVector2D Cc = ValleyClockPoint(Cn.Hour, Cn.Dist);
			const double RimZ = ValleyConeRimZ(Cn, Shape.Floor(Cc.Size(), Cc));
			// Lago de lava a media altura del cráter (su borde toca la pared del cráter).
			const double PoolZ = RimZ - Cn.CraterDepth * 0.45;
			const double PoolR = Cn.CraterR * FMath::Sqrt(0.55) * 1.04;
			const FVector Mid(Cc, PoolZ);
			constexpr int32 Seg = 20;
			for (int32 k = 0; k < Seg; ++k)
			{
				const double A0 = TNProcMap::TwoPi * k / Seg;
				const double A1 = TNProcMap::TwoPi * (k + 1) / Seg;
				Out.AddTri(Mid, Mid + FVector(FMath::Cos(A0) * PoolR, FMath::Sin(A0) * PoolR, 0.0), Mid + FVector(FMath::Cos(A1) * PoolR, FMath::Sin(A1) * PoolR, 0.0),
					FVector::UpVector, (k % 2) ? Core : Hot);
			}
			(c == 0 ? OutCraterTop : OutSmallTop) = FVector(Cc, RimZ + 150.0);
			// Nada crece en la parte alta del cono.
			KeepOut.Add(FVector(Cc.X, Cc.Y, Cn.BaseR * (c == 0 ? 0.62 : 0.55)));
			const FVector2D ToCastle = (-Cc).GetSafeNormal();
			const int32 Rivers = c == 0 ? 3 : 1;
			for (int32 r = 0; r < Rivers; ++r)
			{
				const double Ang = (Rivers == 1 ? 0.0 : (r - 1) * 0.62) + 0.12 * c;
				const FVector2D Dir(ToCastle.X * FMath::Cos(Ang) - ToCastle.Y * FMath::Sin(Ang), ToCastle.X * FMath::Sin(Ang) + ToCastle.Y * FMath::Cos(Ang));
				AddLavaRiver(Out, G, Cc, Cn.CraterR * 1.02, Cn.BaseR * 0.9, Dir, c == 0 ? 260.0 : 150.0, c == 0 ? 520.0 : 260.0, KeepOut);
			}
		}
	}

	/** Triángulo con la cara frontal hacia Facing (en UE, la de normal (C - A) x (B - A)). */
	void AddFacingTri(FTNProcMeshBuffers& Out, int32 A, int32 B, int32 C, const FVector& Facing)
	{
		const FVector Front = FVector::CrossProduct(Out.Verts[C] - Out.Verts[A], Out.Verts[B] - Out.Verts[A]);
		Out.Tris.Add(A);
		if (FVector::DotProduct(Front, Facing) >= 0.0)
		{
			Out.Tris.Add(B);
			Out.Tris.Add(C);
		}
		else
		{
			Out.Tris.Add(C);
			Out.Tris.Add(B);
		}
	}

	/**
	 * Cascada de las 12, al fondo de la laguna: el agua pasa por el borde del cantil y cae en parábola hasta la laguna
	 * (pegada a la pared donde esta sobresale). Cinta de cuatro columnas con los bordes más transparentes, UV V en metros
	 * recorridos (M_ProcCascade las hace correr) y espuma blanca al pie.
	 */
	void BuildWaterfall(FTNProcMeshBuffers& Out, const FTNLobbyValleyGrid& G, FVector& OutFoot, TArray<FVector>& KeepOut)
	{
		const FVector2D Dir = ValleyClockPoint(0.0, 1.0);
		const FVector Side3(-Dir.Y, Dir.X, 0.0);
		constexpr double LipR = 15250.0;
		constexpr double FlowSpeed = 520.0;
		constexpr double Gravity = 980.0;
		const double LipZ = G.HeightAt(Dir * LipR) + 15.0;
		TArray<FVector> Line;
		{
			const FVector2D Back = Dir * (LipR + 300.0);
			Line.Add(FVector(Back, FMath::Max(LipZ, G.HeightAt(Back) + 15.0)));
		}
		for (int32 i = 0; i <= 48; ++i)
		{
			const double Dd = i * 55.0;
			const FVector2D P = Dir * (LipR - Dd);
			const double Tt = Dd / FlowSpeed;
			const double Z = FMath::Max(LipZ - 0.5 * Gravity * Tt * Tt, G.HeightAt(P) + 35.0);
			if (Z <= WaterZ + 4.0)
			{
				Line.Add(FVector(P, WaterZ + 3.0));
				break;
			}
			Line.Add(FVector(P, Z));
		}
		const int32 Rows = Line.Num();
		if (Rows < 3) { return; }
		OutFoot = Line.Last();
		KeepOut.Add(FVector(OutFoot.X, OutFoot.Y, 700.0));
		KeepOut.Add(FVector(Line[0].X, Line[0].Y, 600.0));
		static const double Across[4] = { -0.5, -0.2, 0.2, 0.5 };
		const FVector Facing(-Dir.X, -Dir.Y, 0.35);
		TArray<int32> RowStart;
		double Along = 0.0;
		for (int32 i = 0; i < Rows; ++i)
		{
			if (i > 0) { Along += FVector::Dist(Line[i], Line[i - 1]) / 100.0; }
			const double Tn = static_cast<double>(i) / (Rows - 1);
			const double Width = 720.0 * (1.0 + 0.3 * Tn);
			const FVector Tangent = (Line[FMath::Min(i + 1, Rows - 1)] - Line[FMath::Max(i - 1, 0)]).GetSafeNormal();
			FVector N = FVector::CrossProduct(Tangent, Side3).GetSafeNormal();
			if (FVector::DotProduct(N, Facing) < 0.0) { N = -N; }
			const float Foam = static_cast<float>(TNProcMap::SmoothStep(0.72, 1.0, Tn));
			RowStart.Add(Out.Verts.Num());
			for (int32 j = 0; j < 4; ++j)
			{
				FLinearColor Col = TNProcMesh::TNProcLerpColor(FLinearColor(0.78f, 0.9f, 1.f), FLinearColor(0.96f, 0.99f, 1.f), Foam);
				Col.A = (j == 0 || j == 3) ? 0.45f : 0.9f;
				Out.Verts.Add(Line[i] + Side3 * (Across[j] * Width) + N * 15.0);
				Out.Normals.Add(N);
				Out.UVs.Add(FVector2D(Across[j] * Width / 100.0, Along));
				Out.Colors.Add(Col);
			}
		}
		for (int32 i = 0; i + 1 < Rows; ++i)
		{
			for (int32 j = 0; j < 3; ++j)
			{
				const int32 I00 = RowStart[i] + j;
				const int32 I01 = RowStart[i] + j + 1;
				const int32 I11 = RowStart[i + 1] + j + 1;
				const int32 I10 = RowStart[i + 1] + j;
				AddFacingTri(Out, I00, I01, I11, Facing);
				AddFacingTri(Out, I00, I11, I10, Facing);
			}
		}
	}
}

bool ATN_LobbyValley::IsEnabled()
{
	return TNValleyBuild::CVarLobbyValley.GetValueOnGameThread() != 0;
}

ATN_LobbyValley::ATN_LobbyValley()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	// Se replica sin propiedades solo para que, si lo coloca el servidor (ATN_HQGameMode), llegue a los clientes: cada
	// máquina lo construye igual con la semilla.
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	SetNetUpdateFrequency(1.f);
	SetCanBeDamaged(false);
	SetActorEnableCollision(false);

	ValleyRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ValleyRoot"));
	SetRootComponent(ValleyRoot);

	// Mallas generadas en código (editor y ejecución) que no se guardan con el nivel: RF_Transient (el segundo parámetro
	// de CreateDefaultSubobject no basta).
	TerrainMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TerrainMesh"));
	TerrainMesh->SetFlags(RF_Transient);
	TerrainMesh->SetupAttachment(ValleyRoot);
	TerrainMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TerrainMesh->SetCanEverAffectNavigation(false);
	TerrainMesh->bUseAsyncCooking = true;
	TerrainMesh->SetCastShadow(true);

	WaterMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WaterMesh"));
	WaterMesh->SetFlags(RF_Transient);
	WaterMesh->SetupAttachment(ValleyRoot);
	WaterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WaterMesh->SetCanEverAffectNavigation(false);
	WaterMesh->bUseAsyncCooking = true;
	WaterMesh->SetCastShadow(false);
}

void ATN_LobbyValley::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	BuildAll();
}

void ATN_LobbyValley::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	// Al cargar el nivel (editor) o al duplicarlo para jugar, las mallas transitorias llegan vacías: se rehacen.
	if (!IsTemplate() && GetWorld()) { BuildAll(); }
}

void ATN_LobbyValley::BeginPlay()
{
	Super::BeginPlay();
	if (!IsEnabled())
	{
		SetActorHiddenInGame(true);
		SetActorTickEnabled(false);
		ApplyCastleSea(false);
		return;
	}
	BuildAll();
	ApplyCastleSea(true);
	if (GetNetMode() == NM_DedicatedServer)
	{
		SetActorTickEnabled(false);
		return;
	}
	StartLiving();
}

void ATN_LobbyValley::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopLiving();
	Super::EndPlay(EndPlayReason);
}

void ATN_LobbyValley::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bLiving) { return; }
	const float Dt = FMath::Min(DeltaSeconds, 0.1f);
	TickFauna(Dt);
	TickCastleBirds(Dt);
	TNAmbientFX::TickOwner(this, Dt);
}

void ATN_LobbyValley::BuildAll()
{
	// Nada que ver en un servidor dedicado ni al cocinar; en el editor y en cada cliente, igual con la misma semilla.
	if (IsTemplate() || !GetWorld() || IsRunningCommandlet() || IsRunningDedicatedServer()) { return; }
	// Con la versión de los catálogos de arte: al cambiar uno, el valle del editor se rehace con los sustitutos nuevos.
	const uint32 Key = HashCombine(HashCombine(HashCombine(GetTypeHash(Seed), GetTypeHash(FloraDensity)), HashCombine(GetTypeHash(MaxAnimals), TNValleyBuild::BuildVersion)),
		TNArt::GetCatalogVersion());
	// Hecho y con todo en su sitio (si el editor quitara los componentes creados, se rehacen).
	bool bAlive = bBuilt && Key == BuiltKey && TerrainMesh && TerrainMesh->GetNumSections() > 0;
	for (int32 i = 0; bAlive && i < GeneratedComps.Num(); ++i)
	{
		const UInstancedStaticMeshComponent* Comp = GeneratedComps[i];
		bAlive = IsValid(Comp) && Comp->IsRegistered();
	}
	if (bAlive) { return; }
	const bool bWasLiving = bLiving;
	if (bWasLiving) { StopLiving(); }
	ClearGenerated();

	const double T0 = FPlatformTime::Seconds();
	BuildTerrain();
	BuildWaterAndLava();
	BuildFlora();
	BuildFauna();
	bBuilt = true;
	BuiltKey = Key;

	int32 Instances = 0;
	for (const UInstancedStaticMeshComponent* Comp : GeneratedComps)
	{
		if (Comp) { Instances += Comp->GetInstanceCount(); }
	}
	int32 Tris = 0;
	for (UProceduralMeshComponent* Comp : { TerrainMesh.Get(), WaterMesh.Get() })
	{
		for (int32 s = 0; Comp && s < Comp->GetNumSections(); ++s)
		{
			if (const FProcMeshSection* Section = Comp->GetProcMeshSection(s)) { Tris += Section->ProcIndexBuffer.Num() / 3; }
		}
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Valle] %d triángulos de terreno, agua y formaciones · %d instancias en %d componentes · %d animales · %.0f ms."),
		Tris, Instances, GeneratedComps.Num(), Animals.Num(), (FPlatformTime::Seconds() - T0) * 1000.0);

	ApplyCastleSea(IsEnabled() || !(GetWorld() && GetWorld()->IsGameWorld()));
	if (bWasLiving) { StartLiving(); }
}

void ATN_LobbyValley::ClearGenerated()
{
	for (UInstancedStaticMeshComponent* Comp : GeneratedComps)
	{
		if (Comp) { Comp->DestroyComponent(); }
	}
	GeneratedComps.Reset();
	// Por si alguno se quedó fuera de la lista (una copia del actor, una recarga).
	TInlineComponentArray<UInstancedStaticMeshComponent*> Leftovers;
	GetComponents(Leftovers);
	for (UInstancedStaticMeshComponent* Comp : Leftovers)
	{
		if (Comp && Comp->ComponentHasTag(TNValleyBuild::GeneratedTag())) { Comp->DestroyComponent(); }
	}
	GeneratedMeshes.Reset();
	TNArt::ClearPieceArt(this, TNValleyBuild::ArtGroup());
	if (TerrainMesh) { TerrainMesh->ClearAllMeshSections(); }
	if (WaterMesh) { WaterMesh->ClearAllMeshSections(); }
	Animals.Reset();
	Kinds.Reset();
	KeepOut.Reset();
	Lookouts.Reset();
	Chimneys.Reset();
	Grid.Reset();
	bBuilt = false;
}

UInstancedStaticMeshComponent* ATN_LobbyValley::MakeInstanced(UStaticMesh* Mesh, bool bHierarchical, bool bShadow, int32 WpoDistance)
{
	if (!Mesh || !ValleyRoot) { return nullptr; }
	// Transitorios y fuera de toda duplicación: la copia para jugar se construye sola (PostRegisterAllComponents).
	const EObjectFlags Flags = RF_Transient | RF_DuplicateTransient;
	UInstancedStaticMeshComponent* Comp = nullptr;
	if (bHierarchical)
	{
		Comp = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, NAME_None, Flags);
	}
	else
	{
		Comp = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, Flags);
	}
	Comp->ComponentTags.Add(TNValleyBuild::GeneratedTag());
	Comp->SetStaticMesh(Mesh);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetGenerateOverlapEvents(false);
	Comp->SetCastShadow(bShadow);
	Comp->bAffectDistanceFieldLighting = false;
	if (WpoDistance > 0)
	{
		Comp->WorldPositionOffsetDisableDistance = WpoDistance;
	}
	else
	{
		Comp->bEvaluateWorldPositionOffset = false;
	}
	Comp->SetupAttachment(ValleyRoot);
	Comp->RegisterComponent();
	GeneratedComps.Add(Comp);
	return Comp;
}

bool ATN_LobbyValley::IsKeptOut(const FVector2D& P, double Extra) const
{
	return TNValleyBuild::InKeepOut(KeepOut, P, Extra);
}

void ATN_LobbyValley::ApplyCastleSea(bool bValleyShown)
{
	UWorld* World = GetWorld();
	if (!World) { return; }
	ATN_SandCastleLobby* Nearest = nullptr;
	double Best = FMath::Square(8000.0);
	for (TActorIterator<ATN_SandCastleLobby> It(World); It; ++It)
	{
		const double DistSq = FVector::DistSquared2D(It->GetActorLocation(), GetActorLocation());
		if (DistSq < Best)
		{
			Best = DistSq;
			Nearest = *It;
		}
	}
	if (Nearest) { Nearest->SetDrawSea(!(bHideCastleSea && bValleyShown)); }
}

void ATN_LobbyValley::BuildTerrain()
{
	using namespace TNValleyBuild;
	const FValleyShape ValleyShape(SeedU());
	Grid = MakeShared<FTNLobbyValleyGrid>();
	Grid->Build(ValleyShape);
	FTNProcMeshBuffers Ground;
	Grid->EmitMesh(Ground, ValleyShape);
	FTNProcMeshBuffers Painted;
	// Piezas que Arte puede sustituir (Docs/Arte_Assets.md): las formaciones, agujas y casitas; el terreno no.
	TNArt::FPieceLog Log(ArtGroup());
	BuildLandmarks(Painted, *Grid, SeedU(), KeepOut, Lookouts, Chimneys, Log);
	UMaterialInterface* Mat = ValleyTerrainMaterial();
	UploadSection(TerrainMesh, 0, Ground, Mat);
	UploadSection(TerrainMesh, 1, Painted, Mat, &Log);
	// La malla de arte de cada pieza con sustituto, en su sitio (hija del terreno: mismos ejes que los buffers).
	TNArt::SpawnPieceArt(TerrainMesh, Log);
}

void ATN_LobbyValley::BuildWaterAndLava()
{
	using namespace TNValleyBuild;
	if (!Grid.IsValid()) { return; }
	const FValleyShape ValleyShape(SeedU());
	FTNProcMeshBuffers Water;
	FTNProcMeshBuffers Lava;
	FTNProcMeshBuffers Fall;
	BuildWaterSurface(Water);
	BuildLava(Lava, *Grid, ValleyShape, CraterTop, SmallCraterTop, KeepOut);
	BuildWaterfall(Fall, *Grid, FallFoot, KeepOut);
	UploadSection(WaterMesh, 0, Water, ValleyWaterMaterial());
	UploadSection(WaterMesh, 1, Lava, ValleyGlowMaterial());
	UploadSection(WaterMesh, 2, Fall, ValleyCascadeMaterial());
}

void ATN_LobbyValley::BuildFlora()
{
	using namespace TNValleyBuild;
	if (!Grid.IsValid() || FloraDensity <= 0.f) { return; }
	const FTNLobbyValleyGrid& G = *Grid;
	const FValleyShape ValleyShape(SeedU());
	TArray<FValleyFloraPick> Picks[NumSectors];
	for (int32 s = 0; s < NumSectors; ++s) { ValleyFloraPicksFor(ValleySectorAt(s).Style, Picks[s]); }

	// Reparto en una rejilla con jitter: cada casilla, su generador (determinista). Menos en la sierra y nada más allá
	// de 245 m (la cordillera lejana se queda en su color, más simple).
	constexpr double Cell = 430.0;
	constexpr double MinR = 5200.0;
	constexpr double MaxR = 24500.0;
	const double CellArea = Cell * Cell / 1.0e6;
	const int32 NumCells = FMath::CeilToInt32(2.0 * MaxR / Cell);
	TMap<int32, TArray<FTransform>> ByMesh;
	for (int32 Gy = 0; Gy < NumCells; ++Gy)
	{
		for (int32 Gx = 0; Gx < NumCells; ++Gx)
		{
			TNProcMap::FRng Rng(static_cast<uint64>(TNProcMap::HashCell(SeedU() ^ 0xF10A5u, Gx, Gy)) * 0x9E3779B1ull + 17ull);
			const FVector2D P(-MaxR + (Gx + Rng.Unit()) * Cell, -MaxR + (Gy + Rng.Unit()) * Cell);
			const double Rad = P.Size();
			if (Rad < MinR || Rad > MaxR || IsKeptOut(P, 60.0)) { continue; }
			const FValleyMix SecMix = ValleyShape.Mix(P, Rad);
			const int32 Sec = Rng.Unit() < SecMix.WA ? SecMix.A : SecMix.B;
			const FValleySector& Sector = ValleySectorAt(Sec);
			const TArray<FValleyFloraPick>& Table = Picks[Sec];
			const double Z = G.HeightAt(P);
			const uint8 Zone = ValleyZoneOf(Z);
			if (Zone == 0) { continue; }
			const FVector Nrm = G.NormalAt(P);
			const double Slope = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Nrm.Z, -1.0, 1.0)));
			const bool bMountain = Rad > 15500.0;
			const double Falloff = bMountain ? 0.5 * (1.0 - TNProcMap::SmoothStep(21000.0, MaxR, Rad)) : (Rad < 6500.0 ? 0.35 : 1.0);
			const double TreeLine = FMath::Min(Sector.SnowZ - 700.0, 5600.0);
			constexpr int32 MaxPicks = 16;
			double Prob[MaxPicks];
			double Total = 0.0;
			const int32 NumPicks = FMath::Min(Table.Num(), MaxPicks);
			for (int32 k = 0; k < NumPicks; ++k)
			{
				const FValleyFloraPick& Pk = Table[k];
				Prob[k] = 0.0;
				if ((Pk.Zones & Zone) == 0 || Slope > Pk.SlopeMax || (bMountain && !Pk.bMountain)) { continue; }
				if (Pk.Shape != TNProcMap::EFloraShape::Rock && Z > TreeLine) { continue; }
				Prob[k] = Pk.Density * FloraDensity * CellArea * Falloff;
				Total += Prob[k];
			}
			if (Total <= 0.0) { continue; }
			const double U = Rng.Unit() * FMath::Max(1.0, Total);
			double Sum = 0.0;
			int32 Chosen = INDEX_NONE;
			for (int32 k = 0; k < NumPicks; ++k)
			{
				Sum += Prob[k];
				if (Prob[k] > 0.0 && U < Sum)
				{
					Chosen = k;
					break;
				}
			}
			if (Chosen == INDEX_NONE) { continue; }
			const FValleyFloraPick& Pick = Table[Chosen];
			const bool bTree = ValleyIsTree(Pick.Shape);
			// Lo lejano, algo más grande: se lee desde el castillo.
			const double PlantScale = FMath::Lerp(static_cast<double>(Pick.ScaleMin), static_cast<double>(Pick.ScaleMax), FMath::Pow(Rng.Unit(), 1.4))
				* (1.0 + 0.45 * TNProcMap::SmoothStep(14000.0, 24000.0, Rad));
			const int32 Variant = bTree ? (Rng.Chance(0.5) ? 0 : 2) : 1;
			// Base en lo más bajo de su huella (no flota por el lado de abajo).
			const bool bRock = Pick.Shape == TNProcMap::EFloraShape::Rock;
			const double Foot = (bRock ? 80.0 : 45.0) * PlantScale;
			double Low = Z;
			for (int32 d = 0; d < 4; ++d)
			{
				const FVector2D Off = d == 0 ? FVector2D(1.0, 0.0) : (d == 1 ? FVector2D(-1.0, 0.0) : (d == 2 ? FVector2D(0.0, 1.0) : FVector2D(0.0, -1.0)));
				Low = FMath::Min(Low, G.HeightAt(P + Off * Foot));
			}
			const double Sink = Pick.Shape == TNProcMap::EFloraShape::Prop ? 2.0 : (bRock ? 25.0 * PlantScale : 8.0 + 4.0 * PlantScale);
			const double Stretch = bTree ? Rng.Range(0.88, 1.12) : 1.0;
			// Bit bajo de la clave: las montañas lejanas (sin sombra).
			const int32 Key = (((TNProcMap::BiomeIndex(Sector.Biome) * 64 + static_cast<int32>(Pick.Shape)) * 64 + static_cast<int32>(Pick.Prop)) * 4 + Variant) * 2
				+ (bMountain ? 1 : 0);
			ByMesh.FindOrAdd(Key).Add(FTransform(FRotator(0.0, Rng.Range(0.0, 360.0), 0.0), FVector(P, Low - Sink), FVector(PlantScale, PlantScale, PlantScale * Stretch)));
		}
	}

	// Una malla por bioma, especie y variante (las del mapa procedural), instanciada. Las montañas lejanas, sin sombra
	// dinámica: mucha malla sin Nanite sobre mucho mapa de sombras virtuales para lo poco que se nota desde el castillo.
	UMaterialInterface* Mat = ValleyFoliageMaterial();
	TMap<int32, UStaticMesh*> MeshBySpecies;
	for (TPair<int32, TArray<FTransform>>& Entry : ByMesh)
	{
		const bool bFar = (Entry.Key & 1) != 0;
		const int32 Species = Entry.Key >> 1;
		const int32 Variant = Species % 4;
		const TNProcMap::EPropKind Prop = static_cast<TNProcMap::EPropKind>((Species / 4) % 64);
		const TNProcMap::EFloraShape FloraShape = static_cast<TNProcMap::EFloraShape>((Species / 4 / 64) % 64);
		const ETNProcBiome Biome = TNProcMap::BiomeFromIndex(Species / 4 / 64 / 64);
		const bool bProp = FloraShape == TNProcMap::EFloraShape::Prop;
		const TNFloraMesh::FTNFloraWind Wind = bProp ? TNFloraMesh::FTNFloraWind() : TNFloraMesh::TNFloraWindOf(FloraShape);
		UStaticMesh* Mesh = MeshBySpecies.FindRef(Species);
		if (!Mesh)
		{
			FLinearColor GroundC, RockC;
			BiomeColors(Biome, GroundC, RockC);
			FTNProcMeshBuffers Buffers;
			const uint32 MeshSeed = TNProcMap::HashCell(SeedU() ^ 0xF10Au, Species / 4, Variant);
			if (bProp)
			{
				TNPropMesh::TNPropBuild(Buffers, Prop, Variant, MeshSeed, TNPropMesh::TNPropCrystalColor(Biome), Biome == ETNProcBiome::Volcanic);
				// La paleta de los objetos se ve como sRGB: se decodifica una vez más (como en el mapa procedural).
				for (FLinearColor& Col : Buffers.Colors)
				{
					Col = FLinearColor(TNProcRuntimeMesh::SRGBToLinear(Col.R), TNProcRuntimeMesh::SRGBToLinear(Col.G), TNProcRuntimeMesh::SRGBToLinear(Col.B), Col.A);
				}
			}
			else
			{
				TNFloraMesh::TNFloraBuild(Buffers, FloraShape, TNFloraMesh::TNFloraPaletteFor(Biome, GroundC, RockC), Variant, MeshSeed);
			}
			Mesh = TNProcRuntimeMesh::MakeStaticMesh(this, Buffers, Mat, false, Wind.Stiffness, Wind.Exponent);
			if (!Mesh) { continue; }
			GeneratedMeshes.Add(Mesh);
			MeshBySpecies.Add(Species, Mesh);
		}
		const bool bShadow = !bFar && (bProp || TNFloraMesh::TNFloraLookOf(FloraShape).bShadow);
		// Viento solo cerca (lo lejano apenas se ve moverse); sin distancia de corte: el valle entero está a la vista.
		const int32 WpoDistance = Wind.Stiffness > 0.f ? FMath::Min(Wind.DisableDistance, 12000) : 0;
		if (UInstancedStaticMeshComponent* Comp = MakeInstanced(Mesh, true, bShadow, WpoDistance))
		{
			Comp->AddInstances(Entry.Value, false, false);
			TNArt::ApplyToInstances(Comp, TNValleyBuild::FloraSlot(FloraShape, Prop));
		}
	}
}

void ATN_LobbyValley::StartLiving()
{
	UWorld* World = GetWorld();
	if (bLiving || !World || !World->IsGameWorld() || GetNetMode() == NM_DedicatedServer) { return; }
	bLiving = true;
	StartEmitters();
	StartFlocks();
	StartCastleBirds();
	SetActorTickEnabled(true);
}

void ATN_LobbyValley::StopLiving()
{
	TNAmbientFX::RemoveOwner(this);
	SplashFX = INDEX_NONE;
	for (int32 t = 0; t < 2; ++t)
	{
		for (UInstancedStaticMeshComponent* Comp : { BirdFlyISM[t].Get(), BirdPerchISM[t].Get() })
		{
			if (Comp)
			{
				GeneratedComps.Remove(Comp);
				Comp->DestroyComponent();
			}
		}
		BirdFlyISM[t].Reset();
		BirdPerchISM[t].Reset();
		BirdFlyXf[t].Reset();
		BirdPerchXf[t].Reset();
	}
	CastleBirdList.Reset();
	Perches.Reset();
	bPerchesReady = false;
	bLiving = false;
}

void ATN_LobbyValley::StartEmitters()
{
	const FTransform Xf = GetActorTransform();
	// Columna de humo del volcán grande: bocanadas grises que suben despacio y se abren.
	{
		TNAmbientFX::FEmitterDesc Smoke;
		Smoke.Shape = TNAmbientFX::EShape::Puff;
		Smoke.bSoft = true;
		Smoke.bCloud = true;
		Smoke.Color = FLinearColor(0.32f, 0.3f, 0.3f);
		Smoke.Alpha = 0.55f;
		Smoke.MaxParticles = 26;
		Smoke.Rate = 1.6f;
		Smoke.SpawnRadius = 300.f;
		Smoke.Speed = 260.f;
		Smoke.SpeedJitter = 0.3f;
		Smoke.Spread = 0.25f;
		Smoke.Gravity = 0.f;
		Smoke.Buoyancy = 40.f;
		Smoke.Drag = 0.25f;
		Smoke.LifeMin = 9.f;
		Smoke.LifeMax = 14.f;
		Smoke.SizeStart = 700.f;
		Smoke.SizeEnd = 2600.f;
		Smoke.WakeDistance = 90000.f;
		TNAmbientFX::AddEmitter(this, Smoke, Xf.TransformPosition(CraterTop));
	}
	// Brasas que saltan del cráter.
	{
		TNAmbientFX::FEmitterDesc Embers;
		Embers.Shape = TNAmbientFX::EShape::Ember;
		Embers.bSoft = true;
		Embers.Color = FLinearColor(1.f, 0.45f, 0.08f);
		Embers.Alpha = 0.95f;
		Embers.MaxParticles = 30;
		Embers.Rate = 6.f;
		Embers.SpawnRadius = 500.f;
		Embers.Speed = 700.f;
		Embers.Spread = 0.45f;
		Embers.Gravity = -500.f;
		Embers.Drag = 0.1f;
		Embers.LifeMin = 1.5f;
		Embers.LifeMax = 2.8f;
		Embers.SizeStart = 70.f;
		Embers.SizeEnd = 30.f;
		Embers.WakeDistance = 90000.f;
		TNAmbientFX::AddEmitter(this, Embers, Xf.TransformPosition(CraterTop - FVector(0.0, 0.0, 120.0)));
	}
	// Humo fino del volcán pequeño.
	{
		TNAmbientFX::FEmitterDesc Wisp;
		Wisp.Shape = TNAmbientFX::EShape::Puff;
		Wisp.bSoft = true;
		Wisp.bCloud = true;
		Wisp.Color = FLinearColor(0.45f, 0.43f, 0.42f);
		Wisp.Alpha = 0.4f;
		Wisp.MaxParticles = 12;
		Wisp.Rate = 0.8f;
		Wisp.SpawnRadius = 120.f;
		Wisp.Speed = 140.f;
		Wisp.Spread = 0.3f;
		Wisp.Gravity = 0.f;
		Wisp.Buoyancy = 30.f;
		Wisp.Drag = 0.3f;
		Wisp.LifeMin = 6.f;
		Wisp.LifeMax = 9.f;
		Wisp.SizeStart = 250.f;
		Wisp.SizeEnd = 900.f;
		Wisp.WakeDistance = 90000.f;
		TNAmbientFX::AddEmitter(this, Wisp, Xf.TransformPosition(SmallCraterTop));
	}
	// Bruma al pie de la cascada.
	if (!FallFoot.IsZero())
	{
		TNAmbientFX::FEmitterDesc Mist;
		Mist.Shape = TNAmbientFX::EShape::Puff;
		Mist.bSoft = true;
		Mist.bCloud = true;
		Mist.Color = FLinearColor(0.95f, 0.97f, 1.f);
		Mist.Alpha = 0.4f;
		Mist.MaxParticles = 24;
		Mist.Rate = 5.f;
		Mist.SpawnRadius = 350.f;
		Mist.Speed = 160.f;
		Mist.Spread = 1.f;
		Mist.Gravity = 0.f;
		Mist.Buoyancy = 25.f;
		Mist.Drag = 0.8f;
		Mist.LifeMin = 2.f;
		Mist.LifeMax = 3.5f;
		Mist.SizeStart = 220.f;
		Mist.SizeEnd = 650.f;
		Mist.WakeDistance = 90000.f;
		TNAmbientFX::AddEmitter(this, Mist, Xf.TransformPosition(FallFoot + FVector(0.0, 0.0, 40.0)));
	}
	// Humo de dos chimeneas del pueblo.
	for (const FVector& Top : Chimneys)
	{
		TNAmbientFX::FEmitterDesc Chimney;
		Chimney.Shape = TNAmbientFX::EShape::Puff;
		Chimney.bSoft = true;
		Chimney.bCloud = true;
		Chimney.Color = FLinearColor(0.8f, 0.8f, 0.82f);
		Chimney.Alpha = 0.35f;
		Chimney.MaxParticles = 10;
		Chimney.Rate = 0.7f;
		Chimney.SpawnRadius = 10.f;
		Chimney.Speed = 60.f;
		Chimney.Spread = 0.3f;
		Chimney.Gravity = 0.f;
		Chimney.Buoyancy = 30.f;
		Chimney.Drag = 0.4f;
		Chimney.LifeMin = 4.f;
		Chimney.LifeMax = 6.f;
		Chimney.SizeStart = 60.f;
		Chimney.SizeEnd = 260.f;
		Chimney.WakeDistance = 90000.f;
		TNAmbientFX::AddEmitter(this, Chimney, Xf.TransformPosition(Top));
	}
	// Salpicaduras de los peces que saltan (a golpes, sin ritmo propio).
	{
		TNAmbientFX::FEmitterDesc Drops;
		Drops.Shape = TNAmbientFX::EShape::Puff;
		Drops.bSoft = true;
		Drops.bCloud = true;
		Drops.Color = FLinearColor(0.95f, 0.98f, 1.f);
		Drops.Alpha = 0.6f;
		Drops.MaxParticles = 24;
		Drops.Rate = 0.f;
		Drops.SpawnRadius = 30.f;
		Drops.Speed = 250.f;
		Drops.Spread = 0.6f;
		Drops.Gravity = -300.f;
		Drops.Drag = 0.5f;
		Drops.LifeMin = 0.5f;
		Drops.LifeMax = 0.9f;
		Drops.SizeStart = 60.f;
		Drops.SizeEnd = 160.f;
		Drops.WakeDistance = 90000.f;
		SplashFX = TNAmbientFX::AddEmitter(this, Drops, GetActorLocation());
	}
}
