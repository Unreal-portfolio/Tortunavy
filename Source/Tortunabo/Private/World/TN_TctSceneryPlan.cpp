#include "World/TN_TctSceneryPlan.h"
#include "World/ProcMap/TN_ProcMapLayout.h"
#include "World/ProcMap/TN_ProcMapMath.h"
#include "ProcMap/TN_ProcMapPropMeshes.h"
#include "Misc/Crc.h"

namespace TNTctSceneryDetail
{
	/** Casilla del decorado con colisión (uu): una pieza posible por casilla. */
	constexpr double DecorCell = 2600.0;
	/** Distancia libre entre dos piezas con colisión (uu): un paso de tortuga de sobra. */
	constexpr double DecorGap = 500.0;
	/** Casilla mala: inclinación mayor que esta (rampas, taludes) o borde de un piso. */
	constexpr float BadSlopeDeg = 28.f;
	/** Normal.Z mínima para poner una pieza con colisión (casi llano). */
	constexpr double DecorMinNormalZ = 0.97;
	/** Lo libre (uu) que tiene que quedar alrededor del sitio de un animal. */
	constexpr float FaunaMinOpen = 300.f;
	/** Tope del tamaño de «lo libre» (uu): más allá todo vale lo mismo. */
	constexpr float OpenCap = 6000.f;
	/** Lado (uu) de las manchas de bioma de la rejilla del mapa de biomas. */
	constexpr double BiomeCellSize = 2500.0;
	constexpr double NoPlant = -1.0e6;

	/** Consultas de PlaceFloraRows sobre el suelo medido (en coordenadas del mapa de plantas: relativas a Origin). */
	struct FFloraQuery
	{
		const TNTctScenery::FHeightField* Field = nullptr;
		const TArray<TNTctScenery::FKeepOut>* KeepOuts = nullptr;
		FVector2D Origin = FVector2D::ZeroVector;
		double WaterZ = 0.0;

		double Height(const FVector2D& P) const
		{
			float Z = 0.f;
			return Field->HeightAt(P + Origin, Z) ? static_cast<double>(Z) - WaterZ + TNProcMap::SeaLevel : NoPlant;
		}
		FVector Normal(const FVector2D& P) const { return Field->NormalAt(P + Origin); }
		/** Distancia al suelo malo (rampas, bordes, huecos) o a una salida o punto de objetos: la planta crece lejos de lo que es camino. */
		double Edge(const FVector2D& P) const
		{
			const FVector2D World = P + Origin;
			return FMath::Min(static_cast<double>(Field->OpenAt(World)), TNTctScenery::KeepOutDistance(*KeepOuts, World));
		}
		bool Blocked(const FVector2D& P) const { return TNTctScenery::KeepOutDistance(*KeepOuts, P + Origin) < 0.0; }
	};

	/** El elemento de decorado de cada bioma (de los de ATN_BeachDecorField, con colisión). */
	struct FDecorSet
	{
		ETNBeachElement Elements[4];
		int32 Num;
	};
}

float TNTctScenery::FHeightField::Get(int32 X, int32 Y) const
{
	return (IsValid() && X >= 0 && Y >= 0 && X < NX && Y < NY) ? Z[Y * NX + X] : NoGround;
}

void TNTctScenery::FHeightField::Build(const FVector2D& Min, const FVector2D& Max, double CellSize, TFunctionRef<bool(double, double, float&)> Sampler)
{
	Origin = Min;
	Cell = FMath::Max(10.0, CellSize);
	NX = FMath::Max(2, FMath::FloorToInt((Max.X - Min.X) / Cell) + 1);
	NY = FMath::Max(2, FMath::FloorToInt((Max.Y - Min.Y) / Cell) + 1);
	Z.SetNumUninitialized(NX * NY);
	Open.Reset();
	for (int32 Y = 0; Y < NY; ++Y)
	{
		for (int32 X = 0; X < NX; ++X)
		{
			float Height = 0.f;
			// Cotas a centímetros enteros: lo que sale de la física de cada máquina no se cuela por la última cifra.
			Z[Y * NX + X] = Sampler(Origin.X + X * Cell, Origin.Y + Y * Cell, Height) ? FMath::RoundToFloat(Height) : NoGround;
		}
	}
}

void TNTctScenery::FHeightField::ComputeOpen(float MaxSlopeDeg, float Cap)
{
	if (!IsValid())
	{
		Open.Reset();
		return;
	}
	const float MaxStep = static_cast<float>(FMath::Tan(FMath::DegreesToRadians(static_cast<double>(MaxSlopeDeg))) * Cell);
	constexpr float Far = 1.0e9f;
	Open.SetNumUninitialized(NX * NY);
	for (int32 Y = 0; Y < NY; ++Y)
	{
		for (int32 X = 0; X < NX; ++X)
		{
			const float Here = Get(X, Y);
			bool bBad = Here <= NoGround * 0.5f;
			const int32 Dx[4] = { 1, -1, 0, 0 };
			const int32 Dy[4] = { 0, 0, 1, -1 };
			for (int32 K = 0; K < 4 && !bBad; ++K)
			{
				const float Near = Get(X + Dx[K], Y + Dy[K]);
				bBad = Near <= NoGround * 0.5f || FMath::Abs(Near - Here) > MaxStep;
			}
			Open[Y * NX + X] = bBad ? 0.f : Far;
		}
	}
	// Distancia al más cercano malo: dos pasadas (chaflán 1 / 1,414).
	auto Relax = [this](int32 X, int32 Y, int32 Ox, int32 Oy, float Step)
	{
		if (X + Ox >= 0 && Y + Oy >= 0 && X + Ox < NX && Y + Oy < NY)
		{
			Open[Y * NX + X] = FMath::Min(Open[Y * NX + X], Open[(Y + Oy) * NX + X + Ox] + Step);
		}
	};
	for (int32 Y = 0; Y < NY; ++Y)
	{
		for (int32 X = 0; X < NX; ++X)
		{
			Relax(X, Y, -1, 0, 1.f); Relax(X, Y, 0, -1, 1.f); Relax(X, Y, -1, -1, 1.4142f); Relax(X, Y, 1, -1, 1.4142f);
		}
	}
	for (int32 Y = NY - 1; Y >= 0; --Y)
	{
		for (int32 X = NX - 1; X >= 0; --X)
		{
			Relax(X, Y, 1, 0, 1.f); Relax(X, Y, 0, 1, 1.f); Relax(X, Y, 1, 1, 1.4142f); Relax(X, Y, -1, 1, 1.4142f);
		}
	}
	for (float& Value : Open)
	{
		Value = FMath::Min(Value * static_cast<float>(Cell), Cap);
	}
}

bool TNTctScenery::FHeightField::HeightAt(const FVector2D& P, float& OutZ) const
{
	const double Fx = (P.X - Origin.X) / Cell;
	const double Fy = (P.Y - Origin.Y) / Cell;
	const int32 X0 = FMath::FloorToInt(Fx);
	const int32 Y0 = FMath::FloorToInt(Fy);
	const float A = Get(X0, Y0);
	const float B = Get(X0 + 1, Y0);
	const float C = Get(X0, Y0 + 1);
	const float D = Get(X0 + 1, Y0 + 1);
	const float Low = NoGround * 0.5f;
	if (A <= Low || B <= Low || C <= Low || D <= Low)
	{
		return false;
	}
	const double Tx = Fx - X0;
	const double Ty = Fy - Y0;
	OutZ = static_cast<float>(FMath::Lerp(FMath::Lerp(static_cast<double>(A), static_cast<double>(B), Tx), FMath::Lerp(static_cast<double>(C), static_cast<double>(D), Tx), Ty));
	return true;
}

FVector TNTctScenery::FHeightField::NormalAt(const FVector2D& P) const
{
	float Xp, Xm, Yp, Ym;
	if (!HeightAt(P + FVector2D(Cell, 0.0), Xp) || !HeightAt(P - FVector2D(Cell, 0.0), Xm)
		|| !HeightAt(P + FVector2D(0.0, Cell), Yp) || !HeightAt(P - FVector2D(0.0, Cell), Ym))
	{
		return FVector::UpVector;
	}
	return FVector(-(Xp - Xm) / (2.0 * Cell), -(Yp - Ym) / (2.0 * Cell), 1.0).GetSafeNormal();
}

float TNTctScenery::FHeightField::OpenAt(const FVector2D& P) const
{
	if (Open.Num() != NX * NY)
	{
		return 0.f;
	}
	const int32 X = FMath::RoundToInt((P.X - Origin.X) / Cell);
	const int32 Y = FMath::RoundToInt((P.Y - Origin.Y) / Cell);
	return (X >= 0 && Y >= 0 && X < NX && Y < NY) ? Open[Y * NX + X] : 0.f;
}

double TNTctScenery::KeepOutDistance(const TArray<FKeepOut>& KeepOuts, const FVector2D& P)
{
	double Nearest = 1.0e9;
	for (const FKeepOut& Zone : KeepOuts)
	{
		Nearest = FMath::Min(Nearest, FVector2D::Distance(P, Zone.Center) - static_cast<double>(Zone.Radius));
	}
	return Nearest;
}

uint32 TNTctScenery::MakeSeed(uint32 ManifestSeed, FName Variant, uint32 MatchSeed)
{
	return TNProcMap::HashCell(ManifestSeed ^ 0x7C75C3Du, static_cast<int32>(FCrc::StrCrc32(*Variant.ToString())), static_cast<int32>(MatchSeed));
}

ETNProcBiome TNTctScenery::PrimaryBiome(FName Variant)
{
	const FString Name = Variant.ToString();
	auto Has = [&Name](const TCHAR* Part) { return Name.Contains(Part, ESearchCase::IgnoreCase); };
	if (Has(TEXT("volcan"))) { return ETNProcBiome::Volcanic; }
	if (Has(TEXT("coliseo")) || Has(TEXT("anfiteatro")) || Has(TEXT("ajedrez")) || Has(TEXT("damas")) || Has(TEXT("reloj")) || Has(TEXT("santorini")))
	{
		return ETNProcBiome::Human;
	}
	if (Has(TEXT("fortaleza")) || Has(TEXT("espiral")) || Has(TEXT("plataformas"))) { return ETNProcBiome::Rocky; }
	if (Has(TEXT("zigurat")) || Has(TEXT("tablero")) || Has(TEXT("espana"))) { return ETNProcBiome::Desert; }
	if (Has(TEXT("panal")) || Has(TEXT("colmena")) || Has(TEXT("filipinas"))) { return ETNProcBiome::Jungle; }
	if (Has(TEXT("galapagos")) || Has(TEXT("tortuga"))) { return ETNProcBiome::Mangrove; }
	if (Has(TEXT("yin"))) { return ETNProcBiome::Water; }
	return ETNProcBiome::Beach;
}

ETNProcBiome TNTctScenery::SecondaryBiome(ETNProcBiome Primary)
{
	switch (Primary)
	{
	case ETNProcBiome::Beach:    return ETNProcBiome::Jungle;
	case ETNProcBiome::Jungle:   return ETNProcBiome::Mangrove;
	case ETNProcBiome::Mangrove: return ETNProcBiome::Water;
	case ETNProcBiome::Water:    return ETNProcBiome::Jungle;
	case ETNProcBiome::Desert:   return ETNProcBiome::Rocky;
	case ETNProcBiome::Volcanic: return ETNProcBiome::Rocky;
	case ETNProcBiome::Rocky:    return ETNProcBiome::Water;
	default:                     return ETNProcBiome::Beach;
	}
}

TArray<ETNBeachElement> TNTctScenery::DecorElementsFor(ETNProcBiome Biome)
{
	switch (Biome)
	{
	case ETNProcBiome::Beach:    return { ETNBeachElement::Rock, ETNBeachElement::SandCastleSmall, ETNBeachElement::Driftwood, ETNBeachElement::Rock };
	case ETNProcBiome::Jungle:   return { ETNBeachElement::MossyLog, ETNBeachElement::Rock, ETNBeachElement::MossyLog };
	case ETNProcBiome::Mangrove: return { ETNBeachElement::MossyLog, ETNBeachElement::Driftwood, ETNBeachElement::Rock };
	case ETNProcBiome::Water:    return { ETNBeachElement::MossyLog, ETNBeachElement::Rock, ETNBeachElement::Driftwood };
	case ETNProcBiome::Desert:
	case ETNProcBiome::Rocky:
	case ETNProcBiome::Volcanic: return { ETNBeachElement::Rock, ETNBeachElement::Rock, ETNBeachElement::RockCluster };
	default:                     return { ETNBeachElement::Sandbags, ETNBeachElement::AmmoCrate, ETNBeachElement::TankTrap, ETNBeachElement::BeachChair };
	}
}

uint32 TNTctScenery::FingerprintOf(const TArray<FDecorPick>& Decor)
{
	uint32 Hash = 0x811C9DC5u;
	auto Mix = [&Hash](int32 Value) { Hash = TNProcMap::Hash32(Hash ^ static_cast<uint32>(Value)); };
	Mix(Decor.Num());
	for (const FDecorPick& Pick : Decor)
	{
		Mix(static_cast<int32>(Pick.Element));
		Mix(FMath::RoundToInt(Pick.Location.X));
		Mix(FMath::RoundToInt(Pick.Location.Y));
		Mix(FMath::RoundToInt(Pick.Location.Z));
		Mix(FMath::RoundToInt(Pick.YawDeg * 10.f));
		Mix(FMath::RoundToInt(Pick.Scale * 100.f));
	}
	return Hash;
}

TNTctScenery::FPlan TNTctScenery::MakePlan(const FHeightField& Field, float WaterBaseZ, const TArray<FKeepOut>& KeepOuts, ETNProcBiome Primary,
	uint32 Seed, const FPlanOptions& Options)
{
	using namespace TNTctSceneryDetail;
	using namespace TNProcMap;
	FPlan Plan;
	if (!Field.IsValid() || Field.Open.Num() != Field.NX * Field.NY)
	{
		return Plan;
	}
	const FVector2D Origin = Field.Origin;
	const FVector2D Extent = Field.Max() - Origin;

	// Mapa de biomas del terreno: el principal con manchas del secundario (como las regiones de biomas del mapa generado).
	FLayout Layout;
	Layout.Params.Seed = Seed;
	Layout.BiomeCell = BiomeCellSize;
	Layout.BiomeW = FMath::Max(2, FMath::CeilToInt(Extent.X / BiomeCellSize) + 1);
	Layout.BiomeH = FMath::Max(2, FMath::CeilToInt(Extent.Y / BiomeCellSize) + 1);
	Layout.BiomeWeights.SetNumZeroed(Layout.BiomeW * Layout.BiomeH * NumBiomes);
	const ETNProcBiome Secondary = SecondaryBiome(Primary);
	for (int32 Y = 0; Y < Layout.BiomeH; ++Y)
	{
		for (int32 X = 0; X < Layout.BiomeW; ++X)
		{
			const double Noise = 0.5 + 0.5 * Fbm2(Seed ^ 0xB10Eu, (X + 0.5) * BiomeCellSize / 7000.0, (Y + 0.5) * BiomeCellSize / 7000.0, 2);
			const float Mix = Secondary == Primary ? 0.f : static_cast<float>(SmoothStep(0.5, 0.72, Noise)) * 0.85f;
			const int32 Cell = (Y * Layout.BiomeW + X) * NumBiomes;
			Layout.BiomeWeights[Cell + BiomeIndex(Primary)] += 1.f - Mix;
			Layout.BiomeWeights[Cell + BiomeIndex(Secondary)] += Mix;
		}
	}

	// ── Vegetación, rocas pequeñas y objetos sueltos: el reparto de PlaceFloraRows (mismas especies que el mapa generado) ──
	TArray<FFloraSpecies> Tables[NumBiomes];
	for (int32 B = 0; B < NumBiomes; ++B) { FloraSpeciesFor(BiomeFromIndex(B), Tables[B]); }
	FFloraQuery Query;
	Query.Field = &Field;
	Query.KeepOuts = &KeepOuts;
	Query.Origin = Origin;
	Query.WaterZ = WaterBaseZ;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const FFloraGrid Grid = FloraGridFor(FVector2D::ZeroVector, Extent, Pass);
		TArray<FFloraInstance> Placed;
		PlaceFloraRows(Layout, Tables, Query, Pass, Grid, 0, Grid.NY, Placed, Options.FloraDensity);
		for (FFloraInstance& Instance : Placed)
		{
			const FFloraSpecies& Species = Tables[Instance.Biome][Instance.Species];
			// Sin colisión no valen las piedras grandes ni lo macizo (se atravesarían): eso lo hace el decorado con colisión.
			if (Species.Shape == EFloraShape::Rock || (Species.Shape == EFloraShape::Prop && TNPropMesh::TNPropSolid(Species.Prop)))
			{
				continue;
			}
			Instance.Location.X += Origin.X;
			Instance.Location.Y += Origin.Y;
			Instance.Location.Z += WaterBaseZ;
			Plan.Flora.Add(Instance);
		}
	}

	// ── Decorado con colisión: una pieza posible por casilla, solo donde sobra sitio y lejos de salidas y puntos de objetos ──
	const int32 CellsX = FMath::CeilToInt(Extent.X / DecorCell);
	const int32 CellsY = FMath::CeilToInt(Extent.Y / DecorCell);
	for (int32 Cy = 0; Cy < CellsY && Plan.Decor.Num() < Options.MaxDecor; ++Cy)
	{
		for (int32 Cx = 0; Cx < CellsX && Plan.Decor.Num() < Options.MaxDecor; ++Cx)
		{
			FRng Rng(static_cast<uint64>(HashCell(Seed ^ 0xDEC0u, Cx, Cy)));
			const double Roll = Rng.Unit();
			const FVector2D Where = Origin + FVector2D((Cx + Rng.Range(0.2, 0.8)) * DecorCell, (Cy + Rng.Range(0.2, 0.8)) * DecorCell);
			const double PickRoll = Rng.Unit();
			const float Scale = static_cast<float>(Rng.Range(0.7, 1.1));
			const float Yaw = static_cast<float>(Rng.Range(0.0, 360.0));
			if (Roll >= Options.DecorChance)
			{
				continue;
			}
			float GroundZ = 0.f;
			if (!Field.HeightAt(Where, GroundZ) || Field.NormalAt(Where).Z < DecorMinNormalZ)
			{
				continue;
			}
			const TArray<ETNBeachElement> Choices = DecorElementsFor(Layout.DominantBiomeAt(Where - Origin));
			const ETNBeachElement Element = Choices[FMath::Min(Choices.Num() - 1, static_cast<int32>(PickRoll * Choices.Num()))];
			const double Radius = TNBeach::FootprintRadius(Element) * Scale;
			// Sitio de sobra alrededor (rampas y bordes quedan fuera) y salidas y puntos de objetos libres.
			if (Field.OpenAt(Where) < Radius + Options.DecorClearance || KeepOutDistance(KeepOuts, Where) < Radius + Options.DecorClearance)
			{
				continue;
			}
			bool bClear = true;
			for (const FDecorPick& Other : Plan.Decor)
			{
				const double OtherRadius = TNBeach::FootprintRadius(Other.Element) * Other.Scale;
				bClear &= FVector2D::Distance(Where, FVector2D(Other.Location.X, Other.Location.Y)) >= Radius + OtherRadius + DecorGap;
			}
			if (!bClear)
			{
				continue;
			}
			FDecorPick Pick;
			Pick.Element = Element;
			Pick.Location = FVector(FMath::RoundToDouble(Where.X), FMath::RoundToDouble(Where.Y), FMath::RoundToDouble(GroundZ));
			Pick.YawDeg = FMath::RoundToFloat(Yaw);
			Pick.Scale = FMath::RoundToFloat(Scale * 100.f) / 100.f;
			Plan.Decor.Add(Pick);
		}
	}
	Plan.Fingerprint = FingerprintOf(Plan.Decor);

	// ── Anclas de la fauna: suelo llano y abierto, repartido por la arena; cada 36 m, un «módulo» de animales ──
	const double FaunaCell = FMath::Max(300.0, static_cast<double>(Options.FaunaCell));
	const int32 FaunaX = FMath::CeilToInt(Extent.X / FaunaCell);
	const int32 FaunaY = FMath::CeilToInt(Extent.Y / FaunaCell);
	constexpr double ModuleSize = 3600.0;
	TArray<FFaunaAnchor> Anchors;
	for (int32 Cy = 0; Cy < FaunaY; ++Cy)
	{
		for (int32 Cx = 0; Cx < FaunaX; ++Cx)
		{
			FRng Rng(static_cast<uint64>(HashCell(Seed ^ 0xFA17u, Cx, Cy)));
			const FVector2D Where = Origin + FVector2D((Cx + Rng.Range(0.1, 0.9)) * FaunaCell, (Cy + Rng.Range(0.1, 0.9)) * FaunaCell);
			const float S = static_cast<float>(Rng.Range(0.0, 14000.0));
			float GroundZ = 0.f;
			if (!Field.HeightAt(Where, GroundZ) || Field.OpenAt(Where) < FaunaMinOpen || KeepOutDistance(KeepOuts, Where) < 0.0)
			{
				continue;
			}
			FFaunaAnchor& Anchor = Anchors.AddDefaulted_GetRef();
			Anchor.P = Where;
			Anchor.S = S;
			Anchor.Module = FMath::FloorToInt((Where.X - Origin.X) / ModuleSize) + 1000 * FMath::FloorToInt((Where.Y - Origin.Y) / ModuleSize);
			Anchor.Biome = Layout.DominantBiomeAt(Where - Origin);
		}
	}
	// Con demasiadas, una de cada tantas (siempre las mismas).
	const int32 Stride = FMath::Max(1, FMath::CeilToInt(static_cast<float>(Anchors.Num()) / FMath::Max(1, Options.MaxFaunaAnchors)));
	for (int32 Index = 0; Index < Anchors.Num(); Index += Stride)
	{
		Plan.Fauna.Add(Anchors[Index]);
	}
	return Plan;
}
