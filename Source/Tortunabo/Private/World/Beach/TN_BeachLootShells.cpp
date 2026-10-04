// ─────────────────────────────────────────────────────────────────────────────
// Conchas de puntos de la playa del modo carrera (UTN_BeachLootSubsystem::SpawnRoundShells): las mismas del cooperativo
// (ATN_ScorePickup, TNScoreShells: 1, 25, 50 y 100), repartidas en cada ronda con su semilla y en el servidor, después de
// crear los elementos (las que van encima o dentro de algo buscan su suelo con trazas contra ellos).
//
//  - Conchitas de 1: rachas por los caminos alternativos (la entrada de los corredores que se separan, los atajos por los
//    huecos estrechos de las filas, las pasarelas y los caminitos de palos, el hueco con algas que rodea el castillo con
//    salas y los lados de la playa) y arcos que dibujan el vuelo de las palas, los trampolines y las catapultas.
//  - Normales de 25 junto a los peligros: en medio de las algas, dentro de la concha que atrapa, del cubo roto y del hoyo
//    de la plataforma que se rompe, en las rodadas de los quads, en casa de los cangrejos y los erizos, bajo las gaviotas,
//    tras los sacos terreros y algunas tras las minas; y en la sala de las columnas y en lo alto de castillos pequeños.
//  - Especiales de 50 y 100 en lo difícil o escondido: la reina en la sala de arriba del castillo con salas y en lo alto
//    del castillo enorme; grandes en los rincones escondidos, en las trincheras, tras el alambre de espino y las minas,
//    sobre las plataformas móviles y en lo alto de castillos. Las cimas de las crestas llevan una normal.
//
// Los arcos, cimas, atajos, rincones, trincheras y caminos alternativos salen de los puntos interesantes del reparto
// (TNBeachLayout::FRoundLayout::Interest).
// ─────────────────────────────────────────────────────────────────────────────

#include "World/Beach/TN_BeachLoot.h"
#include "Settings/TN_GameplayAssetSettings.h"
#include "World/Beach/TN_BeachCatapult.h"
#include "World/Beach/TN_BeachDecorField.h"
#include "World/Beach/TN_BeachElement.h"
#include "World/Beach/TN_BeachRaceGenerator.h"
#include "World/TN_ScorePickup.h"
#include "World/TN_ScoreShells.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto
// del bloque.
namespace TNBeachShellDetail
{
	/** Dónde va cada concha (para el registro). */
	enum class ESpot : uint8
	{
		SideStreak,   ///< Racha de conchitas por un lado de la playa.
		PathWalk,     ///< Racha de conchitas por una pasarela o un caminito de palos.
		Bypass,       ///< Racha de conchitas por el hueco con algas que rodea el castillo con salas.
		JumpArc,      ///< Arco de conchitas sobre una pala, un trampolín o una catapulta.
		Hazard,       ///< Normal junto a un peligro.
		Behind,       ///< Tras el alambre, una mina o unos sacos terreros.
		CastleTop,    ///< En lo alto de un castillo de arena.
		DungeonRoom,  ///< Dentro de una sala del castillo con salas.
		Platform,     ///< Sobre una plataforma móvil.
		Nook,         ///< En un rincón escondido a un lado de la playa.
		Trench,       ///< En el canal de una trinchera.
		Summit,       ///< En la cima de una cresta de arena.
		Shortcut,     ///< Racha por un atajo (el hueco estrecho de una fila).
		Detour,       ///< Racha que entra en un camino alternativo.
		Count
	};

	const TCHAR* SpotName(ESpot Spot)
	{
		switch (Spot)
		{
			case ESpot::SideStreak:  return TEXT("rachas a los lados");
			case ESpot::PathWalk:    return TEXT("pasarelas y caminitos");
			case ESpot::Bypass:      return TEXT("rodeo del castillo");
			case ESpot::JumpArc:     return TEXT("arcos de salto");
			case ESpot::Hazard:      return TEXT("junto a peligros");
			case ESpot::Behind:      return TEXT("tras alambre, minas o sacos");
			case ESpot::CastleTop:   return TEXT("en lo alto de castillos");
			case ESpot::DungeonRoom: return TEXT("salas del castillo");
			case ESpot::Platform:    return TEXT("sobre plataformas móviles");
			case ESpot::Nook:        return TEXT("rincones");
			case ESpot::Trench:      return TEXT("trincheras");
			case ESpot::Summit:      return TEXT("cimas de crestas");
			case ESpot::Shortcut:    return TEXT("atajos");
			case ESpot::Detour:      return TEXT("caminos alternativos");
			default:                 return TEXT("?");
		}
	}

	/**
	 * Topes por ronda de cada tamaño (1, 25, 50 y 100): 300, 40, 12 y 2 con 1200 m de recorrido; los tres primeros, por
	 * TNBeach::CourseLengthScale con 800 m (la misma densidad). Las de 100 se quedan en 2: son las de la sala de arriba del
	 * castillo con salas y las de lo alto de los castillos enormes, y hay tantos castillos con salas como antes.
	 */
	constexpr int32 MaxPerTier[TNScoreShells::NumTiers] = { static_cast<int32>(300.0 * TNBeach::CourseLengthScale + 0.5),
		static_cast<int32>(40.0 * TNBeach::CourseLengthScale + 0.5), static_cast<int32>(12.0 * TNBeach::CourseLengthScale + 0.5), 2 };
	/** Separación (cm) entre dos conchitas y entre cualquier concha y una de más valor. */
	constexpr double SmallSpacing = 100.0;
	constexpr double SpecialSpacing = 300.0;
	/** Nada a menos de esto de la línea de salida (cm): lo mismo que el reparto (15 m libres). */
	constexpr double StartClear = TNBeachLayout::ItemsStartX;
	/** Gravedad de los arcos (cm/s²) y altura del centro de la tortuga sobre lo que pisa (por ahí pasan los arcos). */
	constexpr double Gravity = 980.0;
	constexpr double TurtleCenter = 70.0;
	/** Conchitas por arco, por racha lateral (mín. y máx.) y paso entre ellas (cm). */
	constexpr int32 ArcShells = 7;
	constexpr int32 StreakMin = 6;
	constexpr int32 StreakMax = 9;
	constexpr double StreakStep = 190.0;

	/** Planta del castillo con salas (la de TNBeachDungeonDetail::MakeLayout, TN_BeachSandDungeon.cpp: si cambia allí, aquí). */
	struct FDungeonRooms
	{
		/** Centro de la sala de las columnas (abajo) y de la sala de las ventanas (arriba, entre sus dos muretes), en el actor. */
		FVector RoomA = FVector::ZeroVector;
		FVector RoomC = FVector::ZeroVector;
	};

	FDungeonRooms DungeonRooms(float SizeScale)
	{
		constexpr double OuterWall = 240.0;
		constexpr double InnerWall = 180.0;
		constexpr double FloorZ = 50.0;
		constexpr double UpZ = FloorZ + 320.0;
		constexpr double StairRun = 8 * 45.0;
		constexpr double ExitRamp = 640.0;
		const double Fit = TNBeach::FootprintRadius(ETNBeachElement::SandDungeon) * FMath::Clamp(static_cast<double>(FMath::Clamp(SizeScale, 0.7f, 1.4f)), 0.3, 2.0);
		const double HX = FMath::Min(0.72 * Fit, 0.95 * Fit - ExitRamp);
		const double HY = 0.55 * Fit;
		const double IX0 = -HX + OuterWall;
		const double IX1 = HX - OuterWall;
		const double IY0 = -HY + OuterWall;
		const double IY1 = HY - OuterWall;
		const double ILen = IX1 - IX0;
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
		const double XA = IX0 + RoomA;
		const double XS0 = XA + InnerWall + FMath::Max(300.0, Corr);
		const double XS1 = XS0 + StairRun;
		FDungeonRooms Out;
		Out.RoomA = FVector(0.5 * (IX0 + XA), 0.5 * (IY0 + IY1), FloorZ + TNScoreShells::Hover);
		Out.RoomC = FVector(XS1 + 0.525 * (IX1 - XS1), 0.5 * (IY0 + IY1), UpZ + TNScoreShells::Hover);
		return Out;
	}

	/** Un número (float o double) de un UPROPERTY del elemento por su nombre, o Default si no lo tiene. */
	double ReadNumber(const UObject& Object, const TCHAR* Name, double Default)
	{
		const FProperty* Prop = FindFProperty<FProperty>(Object.GetClass(), Name);
		if (const FFloatProperty* AsFloat = CastField<FFloatProperty>(Prop))
		{
			return AsFloat->GetPropertyValue_InContainer(&Object);
		}
		if (const FDoubleProperty* AsDouble = CastField<FDoubleProperty>(Prop))
		{
			return AsDouble->GetPropertyValue_InContainer(&Object);
		}
		return Default;
	}

	const UBoxComponent* FindBox(const AActor& Actor, FName Name)
	{
		TInlineComponentArray<UBoxComponent*> Boxes(&Actor);
		for (const UBoxComponent* Box : Boxes)
		{
			if (Box && Box->GetFName() == Name)
			{
				return Box;
			}
		}
		return nullptr;
	}

	struct FPlanned
	{
		FVector At = FVector::ZeroVector;
		TNScoreShells::ETier Tier = TNScoreShells::ETier::Small;
		ESpot Spot = ESpot::SideStreak;
	};

	/** El plan de una ronda: dónde va cada concha (en el mundo) y cuántas de cada. */
	struct FPlanner
	{
		FPlanner(ATN_BeachRaceGenerator& InGen, UWorld& InWorld, int32 Seed)
			: Gen(InGen)
			, World(InWorld)
			, Layout(InGen.GetRoundLayout())
			, GenXf(InGen.GetActorTransform())
			, Rng(static_cast<int32>(HashCombine(GetTypeHash(Seed), 0x5E11u)))
		{
			// El actor de cada elemento replicado del reparto (los que aún no tienen clase no están) y, del decorado (local e
			// instanciado, sin actor), si está montado en el campo de decorado.
			Actors.Init(nullptr, Layout.Items.Num());
			Present.Init(false, Layout.Items.Num());
			Field = InGen.GetDecorField();
			for (int32 i = 0; i < Layout.Items.Num(); ++i)
			{
				ATN_BeachElement* Element = InGen.GetElementForItem(i);
				Actors[i] = IsValid(Element) ? Element : nullptr;
				Present[i] = Actors[i] != nullptr || (Field && Field->HasItem(i));
			}
		}

		ATN_BeachRaceGenerator& Gen;
		UWorld& World;
		const TNBeachLayout::FRoundLayout& Layout;
		FTransform GenXf;
		FRandomStream Rng;
		TArray<ATN_BeachElement*> Actors;
		/** Si cada elemento del reparto está (su actor o, en el decorado, su pieza en el campo). */
		TArray<bool> Present;
		const ATN_BeachDecorField* Field = nullptr;
		TArray<FPlanned> Shells;
		int32 TierCount[TNScoreShells::NumTiers] = {};
		int32 SpotCount[static_cast<int32>(ESpot::Count)] = {};

		FVector ToWorld(const FVector2D& Local, double LocalZ = 0.0) const
		{
			return GenXf.TransformPosition(FVector(Local.X, Local.Y, LocalZ));
		}

		/** Sobre la arena (con los asientos de la ronda), a la altura de una concha. */
		FVector OnSand(const FVector2D& Local) const
		{
			const FVector Flat = ToWorld(Local);
			return FVector(Flat.X, Flat.Y, Gen.GetGroundHeightAt(Flat) + TNScoreShells::Hover);
		}

		/** Dentro de la playa repartible y a Radius cm de lo que ocupa cada elemento del suelo (salvo el de SkipIndex). */
		bool IsClear(const FVector2D& Local, double Radius, int32 SkipIndex) const
		{
			if (Local.X < StartClear || Local.X > TNBeachLayout::ItemsEndX
				|| FMath::Abs(Local.Y) > TNBeachLayout::HalfWidth - TNBeachLayout::SideMargin - Radius)
			{
				return false;
			}
			return TNBeachLoot::IsClearOfLayout(Gen, Local, Radius, SkipIndex);
		}

		/** Índice del elemento de tipo Element cuyo centro está a menos de Within cm de Local (INDEX_NONE si no hay). */
		int32 ItemNear(const FVector2D& Local, ETNBeachElement Element, double Within) const
		{
			for (int32 i = 0; i < Layout.Items.Num(); ++i)
			{
				if (Layout.Items[i].Element == Element && FVector2D::DistSquared(Layout.Items[i].Pos, Local) < FMath::Square(Within))
				{
					return i;
				}
			}
			return INDEX_NONE;
		}

		/** Lo primero firme (terreno y elementos) bajo From hasta Depth cm. */
		bool TraceDown(const FVector& From, double Depth, FHitResult& OutHit) const
		{
			FCollisionQueryParams Query(SCENE_QUERY_STAT(TN_BeachShellTrace), false);
			return World.LineTraceSingleByObjectType(OutHit, From, From - FVector(0.0, 0.0, Depth), FCollisionObjectQueryParams(ECC_WorldStatic), Query);
		}

		/**
		 * Lo alto de un elemento (castillos): suelo casi llano del propio elemento cerca de su centro, a más de 1,5 m. Con su
		 * actor o, en el decorado (local e instanciado), con la caja de su malla en el campo y las trazas contra el campo (las
		 * huellas no se solapan: lo que se toca cerca de su centro es él).
		 */
		bool FindTop(int32 Index, double Radius, FVector& OutAt) const
		{
			const AActor* Owner = Actors.IsValidIndex(Index) ? Actors[Index] : nullptr;
			FBox Box(ForceInit);
			FVector Base = FVector::ZeroVector;
			if (Owner)
			{
				Box = Owner->GetComponentsBoundingBox(true);
				Base = Owner->GetActorLocation();
			}
			else if (Field && Field->GetItemBounds(Index, Box))
			{
				Owner = Field;
				Base = ToWorld(Layout.Items[Index].Pos, TNBeachLayout::PlacementZ(Layout.Items[Index]));
			}
			if (!Owner || !Box.IsValid || Box.Max.Z < Base.Z + 150.0)
			{
				return false;
			}
			const FVector2D Offsets[5] = { FVector2D(0.0, 0.0), FVector2D(0.2, 0.0), FVector2D(-0.2, 0.0), FVector2D(0.0, 0.2), FVector2D(0.0, -0.2) };
			for (const FVector2D& Offset : Offsets)
			{
				const FVector From(Base.X + Offset.X * Radius, Base.Y + Offset.Y * Radius, Box.Max.Z + 100.0);
				FHitResult Hit;
				if (TraceDown(From, Box.Max.Z - Base.Z + 300.0, Hit) && Hit.GetActor() == Owner && Hit.ImpactNormal.Z > 0.75
					&& Hit.ImpactPoint.Z > Base.Z + 150.0)
				{
					OutAt = Hit.ImpactPoint + FVector(0.0, 0.0, TNScoreShells::Hover);
					return true;
				}
			}
			return false;
		}

		bool HasRoom(TNScoreShells::ETier Tier) const
		{
			return TierCount[static_cast<int32>(Tier)] < MaxPerTier[static_cast<int32>(Tier)];
		}

		/** Apunta una concha si cabe en su tope y no está pegada a otra. */
		bool Add(const FVector& At, TNScoreShells::ETier Tier, ESpot Spot)
		{
			if (!HasRoom(Tier) || At.ContainsNaN())
			{
				return false;
			}
			for (const FPlanned& Other : Shells)
			{
				const double Need = Tier == TNScoreShells::ETier::Small && Other.Tier == TNScoreShells::ETier::Small ? SmallSpacing : SpecialSpacing;
				if (FVector::DistSquared(At, Other.At) < FMath::Square(Need))
				{
					return false;
				}
			}
			Shells.Add({ At, Tier, Spot });
			++TierCount[static_cast<int32>(Tier)];
			++SpotCount[static_cast<int32>(Spot)];
			return true;
		}

		/** Índices de los elementos de un tipo que existen en el mundo (actor o decorado local), en orden al azar (con la semilla). */
		TArray<int32> IndicesOf(ETNBeachElement Element)
		{
			TArray<int32> Out;
			for (int32 i = 0; i < Layout.Items.Num(); ++i)
			{
				if (Layout.Items[i].Element == Element && Present[i])
				{
					Out.Add(i);
				}
			}
			for (int32 k = Out.Num() - 1; k > 0; --k)
			{
				Out.Swap(k, Rng.RandRange(0, k));
			}
			return Out;
		}

		// ── Especiales y normales en sitios concretos ──

		void PlanDungeon()
		{
			for (const int32 i : IndicesOf(ETNBeachElement::SandDungeon))
			{
				const ATN_BeachElement& Dungeon = *Actors[i];
				const FTransform DungeonXf = Dungeon.GetActorTransform();
				const FDungeonRooms Rooms = DungeonRooms(Dungeon.GetSpec().SizeScale);
				// Arriba, tras la puerta, las algas del pasillo y la escalera: la reina (su suelo se confirma con una traza).
				FVector Top = DungeonXf.TransformPosition(Rooms.RoomC);
				FHitResult Hit;
				if (TraceDown(Top + FVector(0.0, 0.0, 300.0), 600.0, Hit) && FMath::Abs(Hit.ImpactPoint.Z + TNScoreShells::Hover - Top.Z) < 150.0)
				{
					Top.Z = Hit.ImpactPoint.Z + TNScoreShells::Hover;
				}
				Add(Top, HasRoom(TNScoreShells::ETier::Grand) ? TNScoreShells::ETier::Grand : TNScoreShells::ETier::Big, ESpot::DungeonRoom);
				// Abajo, en la sala de las columnas: una normal para quien entra.
				Add(DungeonXf.TransformPosition(Rooms.RoomA), TNScoreShells::ETier::Normal, ESpot::DungeonRoom);
			}
		}

		void PlanCastleTops()
		{
			for (const int32 i : IndicesOf(ETNBeachElement::SandCastleHuge))
			{
				FVector Top;
				if (FindTop(i, Layout.Items[i].Radius, Top))
				{
					Add(Top, HasRoom(TNScoreShells::ETier::Grand) ? TNScoreShells::ETier::Grand : TNScoreShells::ETier::Big, ESpot::CastleTop);
				}
			}
			// Castillos pequeños: grandes de 50 en los dos primeros y normales en unos pocos más.
			int32 SmallCastles = 0;
			for (const int32 i : IndicesOf(ETNBeachElement::SandCastleSmall))
			{
				FVector Top;
				if (SmallCastles < 5 && FindTop(i, Layout.Items[i].Radius, Top)
					&& Add(Top, SmallCastles < 2 ? TNScoreShells::ETier::Big : TNScoreShells::ETier::Normal, ESpot::CastleTop))
				{
					++SmallCastles;
				}
			}
		}

		/** Tras un elemento (hacia el mar, +X del generador): el alambre, las minas y los sacos terreros. */
		void PlanBehind()
		{
			int32 Wires = 0;
			for (const int32 i : IndicesOf(ETNBeachElement::BarbedWire))
			{
				const TNBeachLayout::FItem& Wire = Layout.Items[i];
				const FVector2D OnWire = Wire.Pos + Wire.Axis() * (Rng.FRandRange(-0.3f, 0.3f) * Wire.HalfLength);
				const FVector2D Behind = OnWire + FVector2D(Wire.Radius + 260.0, 0.0);
				if (Wires < 3 && IsClear(Behind, 120.0, i) && Add(OnSand(Behind), TNScoreShells::ETier::Big, ESpot::Behind))
				{
					++Wires;
				}
			}
			int32 Mines = 0;
			for (const int32 i : IndicesOf(ETNBeachElement::Mine))
			{
				const TNBeachLayout::FItem& Mine = Layout.Items[i];
				const FVector2D Behind = Mine.Pos + FVector2D(Mine.Radius + 250.0, Rng.FRandRange(-120.f, 120.f));
				if (!IsClear(Behind, 110.0, i))
				{
					continue;
				}
				if (Mines < 2)
				{
					Mines += Add(OnSand(Behind), TNScoreShells::ETier::Big, ESpot::Behind) ? 1 : 0;
				}
				else if (Rng.FRand() < 0.6f)
				{
					Add(OnSand(Behind), TNScoreShells::ETier::Normal, ESpot::Behind);
				}
			}
			int32 Bags = 0;
			for (const int32 i : IndicesOf(ETNBeachElement::Sandbags))
			{
				const TNBeachLayout::FItem& Parapet = Layout.Items[i];
				const FVector2D Behind = Parapet.Pos + FVector2D(Parapet.Radius + 180.0, Rng.FRandRange(-200.f, 200.f));
				if (Bags < 4 && IsClear(Behind, 110.0, i) && Add(OnSand(Behind), TNScoreShells::ETier::Normal, ESpot::Behind))
				{
					++Bags;
				}
			}
		}

		void PlanPlatforms()
		{
			int32 Count = 0;
			for (const int32 i : IndicesOf(ETNBeachElement::MovingPlatform))
			{
				// Encima de lo más alto del elemento al crearse (la plataforma pasa por debajo): se coge montada en ella.
				const AActor& Platform = *Actors[i];
				const FBox Box = Platform.GetComponentsBoundingBox(true);
				const FVector Base = Platform.GetActorLocation();
				const double Z = FMath::Min(Box.IsValid ? Box.Max.Z : Base.Z + 300.0, Base.Z + 800.0) + TNScoreShells::Hover;
				if (Count < 2 && Add(FVector(Base.X, Base.Y, Z), TNScoreShells::ETier::Big, ESpot::Platform))
				{
					++Count;
				}
			}
		}

		/** Normales de 25 junto a los peligros (con tope por tipo). */
		void PlanHazards()
		{
			auto AtCenter = [this](ETNBeachElement Element, int32 Cap)
			{
				int32 Count = 0;
				for (const int32 i : IndicesOf(Element))
				{
					if (Count >= Cap)
					{
						break;
					}
					if (Layout.Items[i].Role == TNBeachLayout::EItemRole::DungeonWing)
					{
						continue;
					}
					Count += Add(OnSand(Layout.Items[i].Pos), TNScoreShells::ETier::Normal, ESpot::Hazard) ? 1 : 0;
				}
			};
			// En medio de lo que enreda, atrapa o se hunde: para cogerla hay que meterse.
			AtCenter(ETNBeachElement::Seaweed, 5);
			AtCenter(ETNBeachElement::ClamTrap, 4);
			AtCenter(ETNBeachElement::WobblyPlatform, 3);

			// Dentro del cubo roto (en su suelo, a unos 70 cm).
			int32 Buckets = 0;
			for (const int32 i : IndicesOf(ETNBeachElement::BrokenBucket))
			{
				FHitResult Hit;
				const FVector Base = Actors[i]->GetActorLocation();
				if (Buckets < 3 && TraceDown(Base + FVector(0.0, 0.0, 150.0), 300.0, Hit)
					&& Add(Hit.ImpactPoint + FVector(0.0, 0.0, TNScoreShells::Hover), TNScoreShells::ETier::Normal, ESpot::Hazard))
				{
					++Buckets;
				}
			}

			// En las rodadas de los quads: dos por paso.
			for (const int32 i : IndicesOf(ETNBeachElement::QuadLane))
			{
				const TNBeachLayout::FItem& Lane = Layout.Items[i];
				const FVector2D Along = Lane.Axis();
				const FVector2D Side(-Along.Y, Along.X);
				const double Rut = 870.0 * Lane.Spec.SizeScale;
				for (int32 k = 0; k < 2; ++k)
				{
					for (int32 Attempt = 0; Attempt < 6; ++Attempt)
					{
						const FVector2D P = Lane.Pos + Along * (Rng.FRandRange(-0.75f, 0.75f) * Lane.HalfLength) + Side * (Rng.FRand() < 0.5f ? -Rut : Rut);
						if (IsClear(P, 120.0, i) && Add(OnSand(P), TNScoreShells::ETier::Normal, ESpot::Hazard))
						{
							break;
						}
					}
				}
			}

			// En casa de los cangrejos y los erizos, y bajo las gaviotas.
			int32 Homes = 0;
			for (const ETNBeachElement Element : { ETNBeachElement::GiantCrab, ETNBeachElement::SeaUrchin })
			{
				for (const int32 i : IndicesOf(Element))
				{
					const double Angle = Rng.FRandRange(0.f, 2.f * PI);
					const FVector2D P = Layout.Items[i].Pos + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Rng.FRandRange(400.f, 650.f);
					if (Homes < 5 && IsClear(P, 120.0, i) && Add(OnSand(P), TNScoreShells::ETier::Normal, ESpot::Hazard))
					{
						++Homes;
					}
				}
			}
			int32 Gulls = 0;
			for (const int32 i : IndicesOf(ETNBeachElement::GullZone))
			{
				const FVector2D P = Layout.Items[i].Pos;
				if (Gulls < 3 && IsClear(P, 120.0, i) && Add(OnSand(P), TNScoreShells::ETier::Normal, ESpot::Hazard))
				{
					++Gulls;
				}
			}
		}

		// ── Conchitas de 1 ──

		/** Arco del vuelo desde Origin (centro de la tortuga al despegar) con Forward cm/s por Dir y Up cm/s arriba. */
		void AddArc(const FVector& Origin, const FVector& Dir, double Forward, double Up)
		{
			const double Flight = 2.0 * FMath::Max(100.0, Up) / Gravity;
			for (int32 k = 0; k < ArcShells; ++k)
			{
				const double T = Flight * (0.18 + 0.72 * k / (ArcShells - 1));
				Add(Origin + Dir * (Forward * T) + FVector(0.0, 0.0, Up * T - 0.5 * Gravity * T * T), TNScoreShells::ETier::Small, ESpot::JumpArc);
			}
		}

		void PlanArcs()
		{
			const FVector Sea = Gen.GetSeaDirection().GetSafeNormal2D();
			// Palas: del mango en alto (balancín) o de la punta de la hoja que asoma de la roca (puente), con su impulso.
			for (const int32 i : IndicesOf(ETNBeachElement::SpadeRamp))
			{
				const ATN_BeachElement& Spade = *Actors[i];
				const bool bSeesaw = (static_cast<uint32>(Spade.GetSpec().Seed) & 1u) == 0u;
				const FVector Dir = Spade.GetActorForwardVector().GetSafeNormal2D();
				FVector Origin = Spade.GetActorLocation() + Dir * (0.4 * Layout.Items[i].Radius) + FVector(0.0, 0.0, 300.0);
				if (const UBoxComponent* Box = FindBox(Spade, bSeesaw ? FName(TEXT("GripBox")) : FName(TEXT("BladeBox"))))
				{
					const FVector Extent = Box->GetScaledBoxExtent();
					Origin = Box->GetComponentLocation() + (bSeesaw ? FVector::ZeroVector : Dir * (Extent.X * 0.5))
						+ FVector(0.0, 0.0, Extent.Z + TurtleCenter);
				}
				// Lo que da la pala y un poco de la carrerilla de la tortuga.
				AddArc(Origin, Dir, ReadNumber(Spade, TEXT("TipForward"), 700.0) + 250.0, ReadNumber(Spade, TEXT("TipUp"), 1250.0));
			}
			// Trampolines y catapultas: el arco que el reparto les deja libre (TNBeachLayout::EInterestKind::JumpArc, del
			// lanzador a donde cae), desde lo alto del lanzador hasta el suelo de la caída, con una altura según el largo.
			TArray<int32> Drawn;
			for (const TNBeachLayout::FInterestPoint& Point : Layout.Interest)
			{
				if (Point.Kind != TNBeachLayout::EInterestKind::JumpArc)
				{
					continue;
				}
				const int32 i = ItemNear(Point.Pos, Point.Source, 200.0);
				if (i == INDEX_NONE || !Actors[i] || Drawn.Contains(i))
				{
					continue;
				}
				Drawn.Add(i);
				const FBox Box = Actors[i]->GetComponentsBoundingBox(true);
				const FVector Base = Actors[i]->GetActorLocation();
				const FVector From(Base.X, Base.Y, (Box.IsValid ? FMath::Min(Box.Max.Z, Base.Z + 600.0) : Base.Z + 150.0) + TurtleCenter);
				const FVector To = OnSand(Point.To) + FVector(0.0, 0.0, TurtleCenter - TNScoreShells::Hover);
				AddCurve(From, To, FMath::Clamp(0.3 * FVector::Dist2D(From, To), 400.0, 1500.0));
				// La catapulta lanza hacia el final del arco (a la altura del centro de la tortuga): sigue las conchitas (#257).
				if (ATN_BeachCatapult* Catapult = Cast<ATN_BeachCatapult>(Actors[i]))
				{
					Catapult->SetLaunchTarget(To);
				}
			}
			// Los que no tienen arco en el reparto: con su impulso (LaunchUp y LaunchForward en cm/s si su clase los tiene),
			// los trampolines hacia su +X y las catapultas hacia el mar.
			for (const ETNBeachElement Element : { ETNBeachElement::Trampoline, ETNBeachElement::Catapult })
			{
				const bool bCatapult = Element == ETNBeachElement::Catapult;
				for (const int32 i : IndicesOf(Element))
				{
					if (Drawn.Contains(i))
					{
						continue;
					}
					const AActor& Launcher = *Actors[i];
					const FBox Box = Launcher.GetComponentsBoundingBox(true);
					const FVector Base = Launcher.GetActorLocation();
					const FVector Origin(Base.X, Base.Y, (Box.IsValid ? FMath::Min(Box.Max.Z, Base.Z + 600.0) : Base.Z + 150.0) + TurtleCenter);
					const FVector Dir = bCatapult ? Sea : Launcher.GetActorForwardVector().GetSafeNormal2D();
					double Forward = ReadNumber(Launcher, TEXT("LaunchForward"), bCatapult ? 1500.0 : 650.0);
					double Up = ReadNumber(Launcher, TEXT("LaunchUp"), bCatapult ? 1300.0 : 1600.0);
					// La catapulta da rapidez y ángulo (LaunchSpeed, LaunchPitch en grados).
					const double Speed = ReadNumber(Launcher, TEXT("LaunchSpeed"), -1.0);
					if (Speed > 0.0)
					{
						const double Pitch = FMath::DegreesToRadians(ReadNumber(Launcher, TEXT("LaunchPitch"), 45.0));
						Forward = Speed * FMath::Cos(Pitch);
						Up = Speed * FMath::Sin(Pitch);
					}
					AddArc(Origin, Dir, Forward, Up);
					if (ATN_BeachCatapult* Catapult = Cast<ATN_BeachCatapult>(Actors[i]))
					{
						// Donde el arco dibujado vuelve a la altura de salida, sobre la arena: ahí la manda la catapulta (#257).
						const FVector Flat = Origin + Dir * (Forward * 2.0 * FMath::Max(100.0, Up) / Gravity);
						Catapult->SetLaunchTarget(FVector(Flat.X, Flat.Y, Gen.GetGroundHeightAt(Flat) + TurtleCenter));
					}
				}
			}
		}

		/** Arco de conchitas de From a To que sube Rise cm por encima de la recta entre los dos (una parábola). */
		void AddCurve(const FVector& From, const FVector& To, double Rise)
		{
			for (int32 k = 0; k < ArcShells; ++k)
			{
				const double S = 0.15 + 0.8 * k / (ArcShells - 1);
				Add(FMath::Lerp(From, To, S) + FVector(0.0, 0.0, 4.0 * Rise * S * (1.0 - S)), TNScoreShells::ETier::Small, ESpot::JumpArc);
			}
		}

		// ── Puntos interesantes del reparto (TNBeachLayout::FInterestPoint) ──

		/** Especiales en lo escondido: rincones (grandes) y trincheras (dos grandes y el resto normales). */
		void PlanHiddenSpots()
		{
			int32 Nooks = 0;
			int32 TrenchCount = 0;
			for (const TNBeachLayout::FInterestPoint& Point : Layout.Interest)
			{
				if (Point.Kind == TNBeachLayout::EInterestKind::Nook && Nooks < 3)
				{
					const TNScoreShells::ETier Tier = HasRoom(TNScoreShells::ETier::Big) ? TNScoreShells::ETier::Big : TNScoreShells::ETier::Normal;
					Nooks += Add(OnSand(Point.Pos), Tier, ESpot::Nook) ? 1 : 0;
				}
				else if (Point.Kind == TNBeachLayout::EInterestKind::Trench && TrenchCount < 5)
				{
					const TNScoreShells::ETier Tier = TrenchCount < 2 && HasRoom(TNScoreShells::ETier::Big) ? TNScoreShells::ETier::Big : TNScoreShells::ETier::Normal;
					TrenchCount += Add(OnSand(Point.Pos), Tier, ESpot::Trench) ? 1 : 0;
				}
			}
		}

		/** Normales en las cimas de las crestas de arena (las de los castillos las ponen sus propias pasadas). */
		void PlanSummits()
		{
			int32 Count = 0;
			for (const TNBeachLayout::FInterestPoint& Point : Layout.Interest)
			{
				if (Point.Kind != TNBeachLayout::EInterestKind::Summit || Count >= 6
					|| Point.Source == ETNBeachElement::SandCastleHuge || Point.Source == ETNBeachElement::SandDungeon)
				{
					continue;
				}
				Count += Add(OnSand(Point.Pos) + FVector(0.0, 0.0, Point.Height), TNScoreShells::ETier::Normal, ESpot::Summit) ? 1 : 0;
			}
		}

		/** Rachas de conchitas por los atajos (huecos estrechos de las filas) y por el arranque de los caminos alternativos. */
		void PlanInterestStreaks()
		{
			for (const TNBeachLayout::FInterestPoint& Point : Layout.Interest)
			{
				if (Point.Kind == TNBeachLayout::EInterestKind::Shortcut)
				{
					// Los de los lanzadores ya tienen su arco.
					if (Point.Source == ETNBeachElement::Trampoline || Point.Source == ETNBeachElement::Catapult)
					{
						continue;
					}
					// El de nadar una poza que corta un corredor va de orilla a orilla por su centro: sobre el agua, flotando.
					const TNBeachLayout::FPool* Pool = nullptr;
					const FVector2D Mid = (Point.Pos + Point.To) * 0.5;
					for (const TNBeachLayout::FPool& Candidate : TNBeachLayout::Pools())
					{
						if (FVector2D::DistSquared(Candidate.Center, Mid) < FMath::Square(50.0))
						{
							Pool = &Candidate;
							break;
						}
					}
					const double Length = FVector2D::Distance(Point.Pos, Point.To);
					const int32 Count = FMath::Clamp(static_cast<int32>(Length / StreakStep) + 1, 2, 16);
					for (int32 k = 0; k < Count; ++k)
					{
						const FVector2D P = FMath::Lerp(Point.Pos, Point.To, static_cast<double>(k) / (Count - 1));
						if (Pool && TNBeachLayout::PoolU(*Pool, P) < 1.0)
						{
							Add(ToWorld(P, Pool->Water + 40.0), TNScoreShells::ETier::Small, ESpot::Shortcut);
						}
						else if (IsClear(P, 40.0, INDEX_NONE))
						{
							Add(OnSand(P), TNScoreShells::ETier::Small, ESpot::Shortcut);
						}
					}
				}
				else if (Point.Kind == TNBeachLayout::EInterestKind::Detour)
				{
					// Una racha que entra en el corredor, hacia el mar.
					for (int32 k = 0; k < 7; ++k)
					{
						const FVector2D P = Point.Pos + FVector2D(k * StreakStep, 50.0 * FMath::Sin(k * 0.8));
						if (IsClear(P, 90.0, INDEX_NONE))
						{
							Add(OnSand(P), TNScoreShells::ETier::Small, ESpot::Detour);
						}
					}
				}
			}
		}

		/** Por las pasarelas y los caminitos de palos (encima de las tablas, o en la arena si falta una). */
		void PlanPathWalks()
		{
			int32 Total = 0;
			for (const ETNBeachElement Element : { ETNBeachElement::Boardwalk, ETNBeachElement::WoodenPostPath })
			{
				for (const int32 i : IndicesOf(Element))
				{
					const TNBeachLayout::FItem& Path = Layout.Items[i];
					const int32 Count = FMath::Clamp(static_cast<int32>(1.6 * Path.HalfLength / 230.0), 2, 14);
					for (int32 k = 0; k < Count && Total < 110; ++k)
					{
						const FVector2D P = Path.Pos + Path.Axis() * (FMath::Lerp(-0.8, 0.8, static_cast<double>(k) / (Count - 1)) * Path.HalfLength);
						if (P.X < StartClear || P.X > TNBeachLayout::ItemsEndX)
						{
							continue;
						}
						const FVector Sand = OnSand(P);
						FHitResult Hit;
						const FVector At = TraceDown(Sand + FVector(0.0, 0.0, 400.0), 600.0, Hit)
							? Hit.ImpactPoint + FVector(0.0, 0.0, TNScoreShells::Hover) : Sand;
						Total += Add(At, TNScoreShells::ETier::Small, ESpot::PathWalk) ? 1 : 0;
					}
				}
			}
		}

		/** Por el hueco con algas que rodea el castillo con salas (junto a la selva): el otro camino. */
		void PlanBypass()
		{
			for (const int32 i : IndicesOf(ETNBeachElement::Seaweed))
			{
				if (Layout.Items[i].Role != TNBeachLayout::EItemRole::DungeonWing)
				{
					continue;
				}
				const FVector2D Center = Layout.Items[i].Pos;
				constexpr int32 Count = 11;
				for (int32 k = 0; k < Count; ++k)
				{
					const double Along = FMath::Lerp(-1100.0, 1100.0, static_cast<double>(k) / (Count - 1));
					const FVector2D P = Center + FVector2D(Along, 60.0 * FMath::Sin(Along / 350.0));
					if (IsClear(P, 90.0, i))
					{
						Add(OnSand(P), TNScoreShells::ETier::Small, ESpot::Bypass);
					}
				}
			}
		}

		/** Rachas por los lados de la playa, lejos del centro: guían por los caminos de fuera. */
		void PlanSideStreaks()
		{
			const double Usable = TNBeachLayout::HalfWidth - TNBeachLayout::SideMargin - 300.0;
			for (double BandX = StartClear + 2000.0; BandX < TNBeachLayout::ItemsEndX - 2000.0; BandX += 6500.0)
			{
				if (Rng.FRand() > 0.5f)
				{
					continue;
				}
				for (int32 Attempt = 0; Attempt < 8; ++Attempt)
				{
					const double SideSign = Rng.FRand() < 0.5f ? -1.0 : 1.0;
					const FVector2D Start(BandX + Rng.FRandRange(0.f, 3000.f), SideSign * Rng.FRandRange(0.3f, 0.88f) * Usable);
					const double Heading = FMath::DegreesToRadians(Rng.FRandRange(-20.f, 20.f));
					const FVector2D Dir(FMath::Cos(Heading), FMath::Sin(Heading));
					const FVector2D Normal(-Dir.Y, Dir.X);
					const int32 Count = Rng.RandRange(StreakMin, StreakMax);
					const double Phase = Rng.FRandRange(0.f, 2.f * PI);
					TArray<FVector2D> Points;
					bool bClear = true;
					for (int32 k = 0; k < Count && bClear; ++k)
					{
						const FVector2D P = Start + Dir * (k * StreakStep) + Normal * (70.0 * FMath::Sin(Phase + k * 0.7));
						bClear = IsClear(P, 110.0, INDEX_NONE);
						Points.Add(P);
					}
					if (!bClear)
					{
						continue;
					}
					for (const FVector2D& P : Points)
					{
						Add(OnSand(P), TNScoreShells::ETier::Small, ESpot::SideStreak);
					}
					break;
				}
			}
		}

		FString Summary() const
		{
			FString Where;
			for (int32 s = 0; s < static_cast<int32>(ESpot::Count); ++s)
			{
				if (SpotCount[s] > 0)
				{
					Where += FString::Printf(TEXT("%s%s %d"), Where.IsEmpty() ? TEXT("") : TEXT(", "), SpotName(static_cast<ESpot>(s)), SpotCount[s]);
				}
			}
			return FString::Printf(TEXT("%d de 1, %d de 25, %d de 50 y %d de 100 (%s)"), TierCount[0], TierCount[1], TierCount[2], TierCount[3],
				Where.IsEmpty() ? TEXT("ninguna") : *Where);
		}
	};
}

FString UTN_BeachLootSubsystem::SpawnRoundShells(ATN_BeachRaceGenerator& Gen, int32 Seed)
{
	using namespace TNBeachShellDetail;
	UWorld* World = GetWorld();
	if (!World)
	{
		return FString(TEXT("sin mundo"));
	}

	// Primero lo especial (así cada sitio difícil tiene su concha), luego las normales y al final las conchitas.
	FPlanner Plan(Gen, *World, Seed);
	Plan.PlanDungeon();
	Plan.PlanCastleTops();
	Plan.PlanHiddenSpots();
	Plan.PlanBehind();
	Plan.PlanPlatforms();
	Plan.PlanHazards();
	Plan.PlanSummits();
	Plan.PlanArcs();
	Plan.PlanBypass();
	Plan.PlanInterestStreaks();
	Plan.PlanPathWalks();
	Plan.PlanSideStreaks();

	// La concha de siempre (el Blueprint, con su valor puesto antes de aparecer; sin él, la clase nativa).
	UClass* ShellClass = UTN_GameplayAssetSettings::GetScorePickupClass();
	const FRotator Facing(0.0, Gen.GetActorRotation().Yaw, 0.0);
	for (const FPlanned& Planned : Plan.Shells)
	{
		const FTransform Where(Facing, Planned.At);
		ATN_ScorePickup* Shell = World->SpawnActorDeferred<ATN_ScorePickup>(ShellClass, Where, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Shell)
		{
			continue;
		}
		// El valor, antes de que aparezca: nace ya con su tamaño y su aspecto en todas las máquinas.
		Shell->SetScoreValue(TNScoreShells::ValueOf(Planned.Tier));
		Shell->FinishSpawning(Where);
		SpawnedShells.Add(Shell);
	}
	return Plan.Summary();
}
