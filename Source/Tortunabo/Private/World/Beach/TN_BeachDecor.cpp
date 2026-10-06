#include "World/Beach/TN_BeachDecor.h"
#include "Core/TN_ProjectMaterials.h"
#include "TN_BeachDecorKit.h"
#include "World/Beach/TN_BeachLayout.h"
#include "../ProcMap/TN_ProcMapRuntimeMesh.h"
#include "Art/TN_Art.h"
#include "Art/TN_ArtMeshComponent.h"
#include "Core/TN_Log.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodySetup.h"
#include "TimerManager.h"
#include "UObject/Package.h"

// ─────────────────────────────────────────────────────────────────────────────
// TNBeachDecorKit: recetas montadas, colocación y animación (TN_BeachDecorKit.h). Lo usan ATN_BeachDecor (abajo) y el
// decorado instanciado de cada ronda (ATN_BeachDecorField).
// ─────────────────────────────────────────────────────────────────────────────

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto
// del bloque.
namespace TNBeachDecorDetail
{
	/** Mallas en caché de una receta (elemento y variante, o pieza de un tramo) y cómo se coloca cada ejemplar. */
	struct FCachedProp
	{
		TWeakObjectPtr<UStaticMesh> Body;
		TWeakObjectPtr<UStaticMesh> Moving;
		TNBeachProp::FPropInfo Info;
		bool bCollision = false;
		bool bHasMoving = false;
		bool bBuilt = false;
	};

	/** M_CosmeticVertexColor (color de vértice; el alfa es el brillo); TNMaterials::VertexColor. */
	UMaterialInterface* DecorMaterial()
	{
		static TWeakObjectPtr<UMaterialInterface> Cached;
		if (!Cached.IsValid())
		{
			Cached = TNMaterials::VertexColor();
		}
		return Cached.Get();
	}

	/**
	 * Malla estática en ejecución (RF_Transient | RF_DuplicateTransient, en el paquete transitorio y fuera del recolector:
	 * la comparten todos los ejemplares) con la colisión simple de la receta en su BodySetup: cajas, esferas y cápsulas,
	 * simple como compleja y sin nada que cocinar.
	 */
	UStaticMesh* MakeMesh(const TNProcMesh::FTNProcMeshBuffers& Buffers, const TNBeachProp::FParts* Collision)
	{
		UMaterialInterface* Mat = DecorMaterial();
		if (!Mat || Buffers.IsEmpty()) { return nullptr; }
		// Alfa de los propios buffers (FixedAlpha = -2): el brillo de M_CosmeticVertexColor.
		UStaticMesh* Mesh = TNProcRuntimeMesh::MakeStaticMesh(GetTransientPackage(), Buffers, Mat, false, 0.f, 1.f, -2.f);
		if (!Mesh) { return nullptr; }
		Mesh->AddToRoot();
		UBodySetup* Setup = Mesh->GetBodySetup();
		if (!Setup) { return Mesh; }
		Setup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		Setup->bNeverNeedsCookedCollisionData = true;
		if (!Collision) { return Mesh; }
		for (const TNBeachProp::FColBox& Box : Collision->Boxes)
		{
			FKBoxElem Elem(static_cast<float>(Box.Half.X * 2.0), static_cast<float>(Box.Half.Y * 2.0), static_cast<float>(Box.Half.Z * 2.0));
			Elem.Center = Box.Center;
			Elem.Rotation = Box.Rot.Rotator();
			Setup->AggGeom.BoxElems.Add(Elem);
		}
		for (const TNBeachProp::FColSphere& Ball : Collision->Spheres)
		{
			FKSphereElem Elem(static_cast<float>(Ball.Radius));
			Elem.Center = Ball.Center;
			Setup->AggGeom.SphereElems.Add(Elem);
		}
		for (const TNBeachProp::FColCapsule& Pill : Collision->Capsules)
		{
			const FVector Axis = Pill.B - Pill.A;
			const double Len = Axis.Size();
			if (Len < 1.0)
			{
				FKSphereElem Elem(static_cast<float>(Pill.Radius));
				Elem.Center = Pill.A;
				Setup->AggGeom.SphereElems.Add(Elem);
				continue;
			}
			FKSphylElem Elem(static_cast<float>(Pill.Radius), static_cast<float>(Len));
			Elem.Center = (Pill.A + Pill.B) * 0.5;
			Elem.Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Axis / Len).Rotator();
			Setup->AggGeom.SphylElems.Add(Elem);
		}
		return Mesh;
	}

	/** Caché de recetas de toda la partida (una por clave, montada en la primera máquina que la pide). */
	TMap<uint32, FCachedProp>& Cache()
	{
		static TMap<uint32, FCachedProp> Entries;
		return Entries;
	}

	bool IsCached(uint32 Key)
	{
		const FCachedProp* Entry = Cache().Find(Key);
		return Entry && Entry->bBuilt && Entry->Body.IsValid() && (!Entry->bHasMoving || Entry->Moving.IsValid());
	}

	/** Receta montada (una vez por clave; las mallas quedan para toda la partida). */
	FCachedProp PropFor(uint32 Key, TFunctionRef<void(TNBeachProp::FParts&)> Build)
	{
		FCachedProp& Entry = Cache().FindOrAdd(Key);
		const bool bLost = Entry.bBuilt && (!Entry.Body.IsValid() || (Entry.bHasMoving && !Entry.Moving.IsValid()));
		if (!Entry.bBuilt || bLost)
		{
			TNBeachProp::FParts Parts;
			Build(Parts);
			Entry.Info = Parts.Info;
			Entry.bCollision = Parts.HasCollision();
			Entry.bHasMoving = !Parts.Moving.IsEmpty();
			Entry.Body = MakeMesh(Parts.Body, &Parts);
			Entry.Moving = Entry.bHasMoving ? MakeMesh(Parts.Moving, nullptr) : nullptr;
			Entry.bBuilt = Entry.Body.IsValid();
		}
		return Entry;
	}

	TNBeachDecorKit::FRecipe ToRecipe(const FCachedProp& Prop)
	{
		TNBeachDecorKit::FRecipe Out;
		Out.Body = Prop.Body.Get();
		Out.Moving = Prop.Moving.Get();
		Out.Info = Prop.Info;
		Out.bCollision = Prop.bCollision && Out.Body != nullptr;
		return Out;
	}

	uint32 SingleKey(ETNBeachElement Element, int32 Variant)
	{
		return (static_cast<uint32>(Element) << 16) | (static_cast<uint32>(Variant) & 0xFFu);
	}

	uint32 PieceKey(ETNBeachElement Element, int32 Piece)
	{
		return (static_cast<uint32>(Element) << 16) | 0x8000u | (static_cast<uint32>(Piece) & 0xFFu);
	}

	/** Semilla de la receta: la misma para todos los ejemplares de una variante (la malla es compartida). */
	uint32 RecipeSeed(ETNBeachElement Element, int32 Index)
	{
		return TNBeachProp::HashMix(static_cast<uint32>(Element) * 131u + 17u, static_cast<uint32>(Index) * 7919u + 3u);
	}
}

TNBeachDecorKit::FRecipe TNBeachDecorKit::Single(ETNBeachElement Element, int32 Variant)
{
	const uint32 MeshSeed = TNBeachDecorDetail::RecipeSeed(Element, Variant);
	return TNBeachDecorDetail::ToRecipe(TNBeachDecorDetail::PropFor(TNBeachDecorDetail::SingleKey(Element, Variant),
		[Element, Variant, MeshSeed](TNBeachProp::FParts& Parts)
		{
			TNBeachProp::BuildDecor(Parts, Element, Variant, MeshSeed);
		}));
}

TNBeachDecorKit::FRecipe TNBeachDecorKit::Piece(ETNBeachElement Element, int32 PieceIndex)
{
	const uint32 MeshSeed = TNBeachDecorDetail::RecipeSeed(Element, PieceIndex);
	return TNBeachDecorDetail::ToRecipe(TNBeachDecorDetail::PropFor(TNBeachDecorDetail::PieceKey(Element, PieceIndex),
		[Element, PieceIndex, MeshSeed](TNBeachProp::FParts& Parts)
		{
			TNBeachProp::BuildTiledPiece(Parts, Element, PieceIndex, MeshSeed);
		}));
}

bool TNBeachDecorKit::IsSingleCached(ETNBeachElement Element, int32 Variant)
{
	return TNBeachDecorDetail::IsCached(TNBeachDecorDetail::SingleKey(Element, Variant));
}

bool TNBeachDecorKit::IsPieceCached(ETNBeachElement Element, int32 PieceIndex)
{
	return TNBeachDecorDetail::IsCached(TNBeachDecorDetail::PieceKey(Element, PieceIndex));
}

int32 TNBeachDecorKit::NumCachedRecipes()
{
	int32 Count = 0;
	for (const TPair<uint32, TNBeachDecorDetail::FCachedProp>& Entry : TNBeachDecorDetail::Cache())
	{
		Count += Entry.Value.bBuilt && Entry.Value.Body.IsValid() ? 1 : 0;
	}
	return Count;
}

int32 TNBeachDecorKit::VariantOf(ETNBeachElement Element, int32 Seed)
{
	const int32 NumVar = FMath::Max(1, TNBeachProp::NumVariants(Element));
	return static_cast<int32>(TNBeachProp::HashMix(static_cast<uint32>(Seed), 0x5EEDu) % static_cast<uint32>(NumVar));
}

float TNBeachDecorKit::ClampSize(float SizeScale)
{
	return FMath::Clamp(SizeScale, 0.5f, 1.6f);
}

FTransform TNBeachDecorKit::BodyPlacement(const TNBeachProp::FPropInfo& Info, int32 Seed, float Size)
{
	// Colocación de este ejemplar, igual en todas las máquinas (sale de su semilla): giro, inclinación y hundimiento.
	const uint32 USeed = static_cast<uint32>(Seed);
	const double Yaw = Info.bFreeYaw ? TNBeachProp::RndIn(USeed, 1, 0.0, 360.0) : TNBeachProp::RndIn(USeed, 1, -Info.YawJitter, Info.YawJitter);
	const double TiltDeg = Info.TiltMax * TNBeachProp::Rnd(USeed, 2);
	const double TiltDir = TNBeachProp::RndIn(USeed, 3, 0.0, UE_DOUBLE_TWO_PI);
	const double Sink = FMath::Lerp(static_cast<double>(Info.SinkMin), static_cast<double>(Info.SinkMax), TNBeachProp::Rnd(USeed, 4)) * Size;
	const FQuat Tilt(FVector(FMath::Cos(TiltDir), FMath::Sin(TiltDir), 0.0), FMath::DegreesToRadians(TiltDeg));
	return FTransform(Tilt * TNBeachProp::YawQ(Yaw), FVector(0.0, 0.0, -Sink), FVector(static_cast<double>(Size)));
}

bool TNBeachDecorKit::HasFixedYaw(const TNBeachLayout::FItem& Item)
{
	return Item.Element == ETNBeachElement::SandCastleHuge && Item.Role == TNBeachLayout::EItemRole::Castle;
}

double TNBeachDecorKit::FixedYawOf(const TNBeachLayout::FItem& Item)
{
	return 180.0 + static_cast<double>(static_cast<uint32>(Item.Spec.Seed) % 25u) - 12.0;
}

FTransform TNBeachDecorKit::ItemBodyPlacement(const TNBeachProp::FPropInfo& Info, const TNBeachLayout::FItem& Item, float Size)
{
	if (!HasFixedYaw(Item))
	{
		return BodyPlacement(Info, Item.Spec.Seed, Size);
	}
	// Sin giro al azar (la inclinación y el hundimiento, los mismos que siempre): la puerta mira donde dice el reparto.
	TNBeachProp::FPropInfo Fixed = Info;
	Fixed.bFreeYaw = false;
	Fixed.YawJitter = 0.f;
	return BodyPlacement(Fixed, Item.Spec.Seed, Size);
}

FTransform TNBeachDecorKit::ItemPlacement(const TNBeachLayout::FRoundLayout& Layout, const TNBeachLayout::FItem& Item)
{
	const FQuat Yaw = FRotator(0.0, HasFixedYaw(Item) ? FixedYawOf(Item) : Item.Yaw, 0.0).Quaternion();
	if (!TNBeachLayout::IsLitter(Item))
	{
		return FTransform(Yaw, FVector(Item.Pos.X, Item.Pos.Y, TNBeachLayout::PlacementZ(Item)));
	}
	FVector Normal = FVector::UpVector;
	const double Z = TNBeachLayout::MeshSandZ(Layout, Item.Pos.X, Item.Pos.Y, &Normal);
	// Sigue la cuesta, con tope: girar la vertical hacia la normal como mucho LitterMaxTilt.
	const double Angle = FMath::Acos(FMath::Clamp(Normal.Z, -1.0, 1.0));
	const FVector Axis = FVector::CrossProduct(FVector::UpVector, Normal);
	FQuat Tilt = FQuat::Identity;
	if (Axis.SizeSquared() > UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		Tilt = FQuat(Axis.GetSafeNormal(), FMath::Min(Angle, FMath::DegreesToRadians(LitterMaxTilt)));
	}
	return FTransform(Tilt * Yaw, FVector(Item.Pos.X, Item.Pos.Y, Z));
}

void TNBeachDecorKit::TilePlacements(ETNBeachElement Element, int32 Seed, float Size, float Extent, TMap<int32, TArray<FTransform>>& OutByPiece)
{
	OutByPiece.Reset();
	const uint32 USeed = static_cast<uint32>(Seed);
	const double Length = FMath::Clamp(Extent > 1.f ? static_cast<double>(Extent) : 6000.0, 1500.0, TNBeach::CourseLength);
	// A lo largo y a lo ancho, a su tamaño; el alto no: la pasarela sigue a ~1,1 m y la cuerda por encima de la tortuga.
	const FVector Scale3(Size, Size, 1.0);
	if (Element == ETNBeachElement::Boardwalk)
	{
		const double ModuleLen = TNBeachProp::BoardwalkKit::ModuleLen;
		const int32 RampKind = TNBeachProp::BoardwalkKit::RampKind;
		const int32 NumKinds = TNBeachProp::BoardwalkKit::NumKinds;
		const double Mod = ModuleLen * Size;
		const int32 Total = FMath::Max(1, FMath::RoundToInt32((Length - 2.0 * Mod) / Mod)) + 2;
		const double X0 = -0.5 * (Total - 1) * Mod;
		for (int32 m = 0; m < Total; ++m)
		{
			int32 Kind = RampKind;
			double Yaw = 0.0;
			if (m == 0)
			{
				// Bajada del extremo -X (la pieza baja hacia su +X: se gira).
				Yaw = 180.0;
			}
			else if (m < Total - 1)
			{
				// La mitad, tramos enteros; el resto, sin una tabla, con una rota, con una suelta o con tablas movidas.
				const double Roll = TNBeachProp::Rnd(USeed, 100 + m);
				Kind = Roll < 0.5 ? 0 : FMath::Clamp(1 + static_cast<int32>((Roll - 0.5) * 2.0 * (NumKinds - 1)), 1, NumKinds - 1);
				Yaw = TNBeachProp::Rnd(USeed, 300 + m) < 0.5 ? 180.0 : 0.0;
			}
			OutByPiece.FindOrAdd(Kind).Add(FTransform(TNBeachProp::YawQ(Yaw), FVector(X0 + m * Mod, 0.0, 0.0), Scale3));
		}
		return;
	}
	if (Element != ETNBeachElement::WoodenPostPath) { return; }
	const double Spacing = TNBeachProp::PostPathKit::Spacing;
	const double HalfWidth = TNBeachProp::PostPathKit::HalfWidth;
	const int32 RopeBase = TNBeachProp::PostPathKit::RopeBase;
	const int32 NumRopeKinds = TNBeachProp::PostPathKit::NumRopeKinds;
	const double Gap = Spacing * Size;
	const int32 Count = FMath::Max(2, FMath::RoundToInt32(Length / Gap) + 1);
	const double X0 = -0.5 * (Count - 1) * Gap;
	const int32 RopeKind = static_cast<int32>(TNBeachProp::HashMix(USeed, 77u) % static_cast<uint32>(NumRopeKinds));
	for (const double SideSign : { -1.0, 1.0 })
	{
		TArray<FVector> Attach;
		for (int32 i = 0; i < Count; ++i)
		{
			// Palos algo torcidos y descolocados: la mayoría rectos, alguno roto y alguno con vueltas de cuerda.
			const int32 Salt = (SideSign > 0.0 ? 1000 : 2000) + i;
			const double Roll = TNBeachProp::Rnd(USeed, Salt);
			const int32 PostKind = Roll < 0.7 ? 0 : (Roll < 0.85 ? 1 : 2);
			const FVector Base(X0 + i * Gap + TNBeachProp::RndIn(USeed, Salt + 5000, -40.0, 40.0) * Size, SideSign * HalfWidth * Size, 0.0);
			const double LeanDir = TNBeachProp::RndIn(USeed, Salt + 9000, 0.0, UE_DOUBLE_TWO_PI);
			const FQuat Lean(FVector(FMath::Cos(LeanDir), FMath::Sin(LeanDir), 0.0), FMath::DegreesToRadians(TNBeachProp::RndIn(USeed, Salt + 7000, 0.0, 9.0)));
			const FTransform Xf(Lean * TNBeachProp::YawQ(TNBeachProp::RndIn(USeed, Salt + 11000, 0.0, 360.0)), Base, Scale3);
			OutByPiece.FindOrAdd(PostKind).Add(Xf);
			Attach.Add(Xf.TransformPosition(FVector(0.0, 0.0, TNBeachProp::PostPathKit::AttachZ(PostKind))));
		}
		// Un tramo de cuerda de cada palo al siguiente, estirado en X hasta él.
		for (int32 i = 0; i + 1 < Count; ++i)
		{
			const FVector D = Attach[i + 1] - Attach[i];
			const double Dist = D.Size();
			if (Dist < 1.0) { continue; }
			OutByPiece.FindOrAdd(RopeBase + RopeKind).Add(FTransform(FQuat::FindBetweenNormals(FVector(1.0, 0.0, 0.0), D / Dist), Attach[i],
				FVector(Dist / Spacing, 1.0, 1.0)));
		}
	}
}

float TNBeachDecorKit::CullDistanceFor(ETNBeachElement Element, float Size)
{
	const double Radius = TNBeach::FootprintRadius(Element) * Size;
	return Radius >= 1000.0 ? 0.f : static_cast<float>(FMath::Clamp(Radius * 60.0, 12000.0, 60000.0));
}

float TNBeachDecorKit::AnimRangeFor(ETNBeachElement Element, float Size)
{
	return static_cast<float>(FMath::Clamp(TNBeach::FootprintRadius(Element) * Size * 12.0, 6000.0, 20000.0));
}

void TNBeachDecorKit::SetupCollision(UPrimitiveComponent* Comp, bool bCollision, bool bBlocksCamera)
{
	if (!Comp) { return; }
	if (!bCollision)
	{
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return;
	}
	Comp->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Comp->SetCollisionResponseToChannel(ECC_Camera, bBlocksCamera ? ECR_Block : ECR_Ignore);
}

FName TNBeachDecorKit::BodySlot(ETNBeachElement Element)
{
	switch (Element)
	{
	case ETNBeachElement::Coconut:           return TN_ART("Beach.Decor.Coconut");
	case ETNBeachElement::StrandedJellyfish: return TN_ART("Beach.Decor.Jellyfish");
	case ETNBeachElement::SixPackRings:      return TN_ART("Beach.Decor.SixPackRings");
	case ETNBeachElement::RedBra:            return TN_ART("Beach.Decor.RedBra");
	case ETNBeachElement::Clam:              return TN_ART("Beach.Decor.Clam");
	case ETNBeachElement::DecorShell:        return TN_ART("Beach.Decor.Shell");
	case ETNBeachElement::Starfish:          return TN_ART("Beach.Decor.Starfish");
	case ETNBeachElement::Rock:              return TN_ART("Beach.Decor.Rock");
	case ETNBeachElement::RockCluster:       return TN_ART("Beach.Decor.RockCluster");
	case ETNBeachElement::ShipSailWreck:     return TN_ART("Beach.Decor.ShipSailWreck");
	case ETNBeachElement::MossyLog:          return TN_ART("Beach.Decor.MossyLog");
	case ETNBeachElement::OldPlanks:         return TN_ART("Beach.Decor.OldPlanks");
	case ETNBeachElement::FishingNet:        return TN_ART("Beach.Decor.FishingNet");
	case ETNBeachElement::PlasticCup:        return TN_ART("Beach.Decor.PlasticCup");
	case ETNBeachElement::Bottle:            return TN_ART("Beach.Decor.Bottle");
	case ETNBeachElement::Lollipop:          return TN_ART("Beach.Decor.Lollipop");
	case ETNBeachElement::WatermelonRind:    return TN_ART("Beach.Decor.WatermelonRind");
	case ETNBeachElement::Straw:             return TN_ART("Beach.Decor.Straw");
	case ETNBeachElement::PlantedUmbrella:   return TN_ART("Beach.Decor.Umbrella");
	case ETNBeachElement::BeachChair:        return TN_ART("Beach.Decor.BeachChair");
	case ETNBeachElement::SandCastleSmall:   return TN_ART("Beach.Decor.SandCastleSmall");
	case ETNBeachElement::SandCastleHuge:    return TN_ART("Beach.Decor.SandCastleHuge");
	case ETNBeachElement::Driftwood:         return TN_ART("Beach.Decor.Driftwood");
	case ETNBeachElement::SodaCan:           return TN_ART("Beach.Decor.SodaCan");
	case ETNBeachElement::BottleCaps:        return TN_ART("Beach.Decor.BottleCaps");
	case ETNBeachElement::FlipFlop:          return TN_ART("Beach.Decor.FlipFlop");
	case ETNBeachElement::JuiceBox:          return TN_ART("Beach.Decor.JuiceBox");
	case ETNBeachElement::Buoy:              return TN_ART("Beach.Decor.Buoy");
	case ETNBeachElement::BeachTowel:        return TN_ART("Beach.Decor.BeachTowel");
	case ETNBeachElement::SunscreenBottle:   return TN_ART("Beach.Decor.Sunscreen");
	case ETNBeachElement::PopsicleSticks:    return TN_ART("Beach.Decor.PopsicleSticks");
	case ETNBeachElement::SnackShells:       return TN_ART("Beach.Decor.SnackShells");
	case ETNBeachElement::RopePiece:         return TN_ART("Beach.Decor.RopePiece");
	case ETNBeachElement::Sunglasses:        return TN_ART("Beach.Decor.Sunglasses");
	case ETNBeachElement::ToyBucket:         return TN_ART("Beach.Decor.ToyBucket");
	case ETNBeachElement::BeachBall:         return TN_ART("Beach.Decor.BeachBall");
	case ETNBeachElement::Frisbee:           return TN_ART("Beach.Decor.Frisbee");
	case ETNBeachElement::Cuttlebone:        return TN_ART("Beach.Decor.Cuttlebone");
	case ETNBeachElement::RubberDuck:        return TN_ART("Beach.Decor.RubberDuck");
	case ETNBeachElement::GullFeather:       return TN_ART("Beach.Decor.GullFeather");
	case ETNBeachElement::Sandbags:          return TN_ART("Beach.Decor.Sandbags");
	case ETNBeachElement::AmmoCrate:         return TN_ART("Beach.Decor.AmmoCrate");
	case ETNBeachElement::TankTrap:          return TN_ART("Beach.Decor.TankTrap");
	case ETNBeachElement::MilitaryHelmet:    return TN_ART("Beach.Decor.MilitaryHelmet");
	case ETNBeachElement::CamoNet:           return TN_ART("Beach.Decor.CamoNet");
	case ETNBeachElement::Jerrycan:          return TN_ART("Beach.Decor.Jerrycan");
	case ETNBeachElement::ToySoldiers:       return TN_ART("Beach.Decor.ToySoldiers");
	default:                                 return NAME_None;
	}
}

FName TNBeachDecorKit::MovingSlot(ETNBeachElement Element)
{
	switch (Element)
	{
	case ETNBeachElement::StrandedJellyfish: return TN_ART("Beach.Decor.JellyfishBell");
	case ETNBeachElement::Clam:              return TN_ART("Beach.Decor.ClamTop");
	case ETNBeachElement::FishingNet:        return TN_ART("Beach.Decor.FishingNetFlap");
	case ETNBeachElement::ShipSailWreck:     return TN_ART("Beach.Decor.SailRag");
	case ETNBeachElement::SandCastleSmall:   return TN_ART("Beach.Decor.SandCastleSmallFlag");
	case ETNBeachElement::SandCastleHuge:    return TN_ART("Beach.Decor.SandCastleHugeFlag");
	case ETNBeachElement::Sandbags:          return TN_ART("Beach.Decor.SandbagsFlag");
	case ETNBeachElement::CamoNet:           return TN_ART("Beach.Decor.CamoNetSheet");
	default:                                 return NAME_None;
	}
}

FName TNBeachDecorKit::PieceSlot(ETNBeachElement Element, int32 PieceIndex)
{
	if (Element == ETNBeachElement::Boardwalk)
	{
		return PieceIndex == TNBeachProp::BoardwalkKit::RampKind ? TN_ART("Beach.Decor.BoardwalkRamp") : TN_ART("Beach.Decor.BoardwalkModule");
	}
	if (Element == ETNBeachElement::WoodenPostPath)
	{
		return PieceIndex >= TNBeachProp::PostPathKit::RopeBase ? TN_ART("Beach.Decor.PathRope") : TN_ART("Beach.Decor.PathPost");
	}
	return NAME_None;
}

float TNBeachDecorKit::AnimPhaseOf(int32 Seed)
{
	return static_cast<float>(TNBeachProp::Rnd(static_cast<uint32>(Seed), 21));
}

FTransform TNBeachDecorKit::AnimPose(const TNBeachProp::FPropInfo& Info, float Time, float Phase, int32 Seed, FAnimState& State,
	TFunctionRef<bool()> IsSomeoneOnTop)
{
	const FVector SafeAxis = Info.AnimAxis.GetSafeNormal();
	const FVector Axis = SafeAxis.IsNearlyZero() ? FVector(0.0, 1.0, 0.0) : SafeAxis;
	const float Amp = Info.AnimAmp;
	const float Rate = FMath::Max(0.01f, Info.AnimRate);
	FQuat Rot = FQuat::Identity;
	FVector Scale = FVector::OneVector;
	switch (Info.Anim)
	{
	case TNBeachProp::EAnim::Breathe:
	{
		// Respira despacio (se ensancha al bajar) y a ratos tiembla deprisa.
		const float T = Time + Phase * 10.f;
		const float Breath = FMath::Sin(UE_TWO_PI * Rate * T);
		const float Burst = FMath::Max(0.f, FMath::Sin(UE_TWO_PI * 0.13f * T + 1.7f) - 0.6f) * 2.5f;
		const float Shiver = Burst * 0.012f * (FMath::Sin(T * 57.f) + 0.6f * FMath::Sin(T * 83.f + 1.3f));
		Scale = FVector(1.f + Amp * Breath + Shiver, 1.f + Amp * Breath - Shiver, 1.f - 1.4f * Amp * Breath + Shiver * 0.5f);
		break;
	}
	case TNBeachProp::EAnim::Clam:
	{
		// Cada ciclo: cerrada un rato, se abre en 1 s, se queda abierta (respirando), se cierra de golpe y rebota un poco.
		// Si al empezar el ciclo hay alguien encima, ese ciclo no se abre.
		const float Period = FMath::Max(5.f, Rate);
		const float Local = Time + Phase * Period;
		const int32 Cycle = FMath::FloorToInt32(Local / Period);
		const float InCycle = Local - static_cast<float>(Cycle) * Period;
		if (Cycle != State.ClamCycle)
		{
			State.ClamCycle = Cycle;
			State.bClamHeld = IsSomeoneOnTop();
		}
		const uint32 USeed = static_cast<uint32>(Seed);
		const float Hold = 1.2f + 1.6f * static_cast<float>(TNBeachProp::Rnd(USeed, 500 + (Cycle & 0xFFFF)));
		const float OpenAt = Period - (1.f + Hold + 0.35f + 0.4f);
		float Open = 0.f;
		if (!State.bClamHeld && InCycle > OpenAt)
		{
			const float U = InCycle - OpenAt;
			if (U < 1.f) { Open = FMath::InterpEaseOut(0.f, 1.f, U, 2.f); }
			else if (U < 1.f + Hold) { Open = 1.f - 0.05f * FMath::Sin((U - 1.f) * 6.f); }
			else if (U < 1.f + Hold + 0.35f) { Open = 1.f - FMath::InterpEaseIn(0.f, 1.f, (U - 1.f - Hold) / 0.35f, 2.f); }
			else { Open = 0.12f * FMath::Sin(FMath::Clamp((U - 1.35f - Hold) / 0.4f, 0.f, 1.f) * UE_PI); }
		}
		const float Amount = 0.7f + 0.3f * static_cast<float>(TNBeachProp::Rnd(USeed, 900 + (Cycle & 0xFFFF)));
		Rot = FQuat(Axis, FMath::DegreesToRadians(static_cast<double>(Amp * Amount * Open)));
		break;
	}
	case TNBeachProp::EAnim::Flutter:
	{
		// Ondea: giro alrededor de su eje con tres senos y un abombado a lo ancho.
		const float W = UE_TWO_PI * Rate * (Time + Phase * 10.f);
		const float Angle = Amp * (0.65f * FMath::Sin(W) + 0.25f * FMath::Sin(2.3f * W + 1.1f) + 0.1f * FMath::Sin(5.1f * W + 0.4f));
		Rot = FQuat(Axis, FMath::DegreesToRadians(static_cast<double>(Angle)));
		Scale = FVector(1.0, 1.0 + 0.12 * FMath::Sin(1.3 * W + 0.7), 1.0);
		break;
	}
	case TNBeachProp::EAnim::Sway:
	{
		// Se mece despacio con el viento.
		const float W = UE_TWO_PI * Rate * (Time + Phase * 10.f);
		const float Angle = Amp * (0.75f * FMath::Sin(W) + 0.25f * FMath::Sin(2.7f * W + 0.8f));
		Rot = FQuat(Axis, FMath::DegreesToRadians(static_cast<double>(Angle)));
		break;
	}
	default:
		break;
	}
	return FTransform(Rot, Info.AnimPivot, Scale);
}

// ─────────────────────────────────────────────────────────────────────────────
// Montículos de arena removida de los rebuscables (TNBeachDecorKit::SearchMoundMesh)
// ─────────────────────────────────────────────────────────────────────────────

namespace TNBeachDecorDetail
{
	/**
	 * Montículo de arena removida (junto a lo que se puede rebuscar): casquete irregular a manchas, con un faldón bajo la
	 * arena (sin hueco en las dunas), terrones alrededor y, según la variante, algo asomando. Aplanado: un disco casi a
	 * ras con marcas de escarbar.
	 */
	void BuildSearchMound(TNProcMesh::FTNProcMeshBuffers& M, int32 Variant, bool bFlat)
	{
		const uint32 Seed = 0x40D5u + static_cast<uint32>(Variant) * 131u + (bFlat ? 7u : 0u);
		const FLinearColor Sand = TNBeachProp::Hex(0xDCC08F);
		const FLinearColor Dug = TNBeachProp::Hex(0xC4A170);
		const FLinearColor Damp = TNBeachProp::Hex(0xAE8E61);
		const TNBeachProp::FBeachFrame Base(FVector::ZeroVector, FQuat::Identity);
		const double R = TNBeachDecorKit::SearchMoundRadius;
		if (bFlat)
		{
			TNBeachProp::AddRevolve(M, Base, TNBeachProp::CapProfile(R * 1.15, 5.0, 3, 10.0), 14, Damp, 0.12, Seed);
			for (int32 k = 0; k < 5; ++k)
			{
				const double A = TNProcMap::TwoPi * (k + 0.6 * TNBeachProp::Rnd(Seed, k)) / 5.0;
				const double Rr = R * (0.35 + 0.45 * TNBeachProp::Rnd(Seed, 10 + k));
				TNBeachProp::AddBlob(M, TNBeachProp::FBeachFrame(FVector(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, 3.0), TNBeachProp::YawQ(FMath::RadiansToDegrees(A))),
					15.0, 7.0, 3.5, 6, 2, Dug);
			}
			return;
		}
		const double H = TNBeachDecorKit::SearchMoundHeight;
		TNBeachProp::AddRevolve(M, Base, TNBeachProp::CapProfile(R, H, 5, 12.0), 16,
			[&](int32 Ring, int32 Side)
			{
				const float Tone = 0.92f + 0.12f * static_cast<float>(TNBeachProp::Rnd(Seed, 40 + Ring * 16 + Side));
				return TNBeachProp::Shade(((Ring + Side) % 3 == 0) ? Dug : Sand, Tone);
			},
			0.18, Seed);
		// Terrones alrededor del pie y por la ladera.
		for (int32 k = 0; k < 7; ++k)
		{
			const double A = TNProcMap::TwoPi * (k + 0.7 * TNBeachProp::Rnd(Seed, 60 + k)) / 7.0;
			const double Rr = R * (0.7 + 0.35 * TNBeachProp::Rnd(Seed, 70 + k));
			const double Lump = 10.0 + 9.0 * TNBeachProp::Rnd(Seed, 80 + k);
			const double Z = FMath::Max(2.0, H * (1.0 - FMath::Square(FMath::Min(1.0, Rr / R))) * 0.8);
			TNBeachProp::AddBlob(M, TNBeachProp::FBeachFrame(FVector(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, Z), TNBeachProp::YawQ(TNBeachProp::RndIn(Seed, 90 + k, 0.0, 360.0))),
				Lump, Lump * 0.8, Lump * 0.55, 6, 3, (k % 2) ? Damp : Dug);
		}
		switch (Variant)
		{
		case 1:
		{
			// Chapa de botella roja, de canto y medio enterrada.
			const TNBeachProp::FBeachFrame F(FVector(R * 0.2, -R * 0.12, H * 0.82), FQuat(FVector(1.0, 0.0, 0.0), FMath::DegreesToRadians(70.0)));
			const TArray<FVector2D> Cap = { FVector2D(0.0, -4.0), FVector2D(15.0, -4.0), FVector2D(16.0, 3.0), FVector2D(0.0, 4.0) };
			TNBeachProp::AddRevolve(M, F, Cap, 12, TNBeachProp::Hex(0xC8322D, 0.6f));
			break;
		}
		case 2:
		{
			// Palito de helado clavado, inclinado.
			const FQuat Rot = TNBeachProp::YawQ(30.0) * FQuat(FVector(0.0, 1.0, 0.0), FMath::DegreesToRadians(25.0));
			TNBeachProp::AddOBox(M, FVector(-R * 0.15, R * 0.1, H + 14.0), Rot, FVector(4.0, 1.2, 30.0), TNBeachProp::Hex(0xE2C79A));
			break;
		}
		case 3:
		{
			// Trozo de concha rosada asomando, de canto.
			const TNBeachProp::FBeachFrame F(FVector(R * 0.1, R * 0.18, H * 0.85), FQuat(FVector(0.6, 0.8, 0.0).GetSafeNormal(), FMath::DegreesToRadians(55.0)));
			TNBeachProp::AddBlob(M, F, 22.0, 17.0, 5.0, 10, 3, TNBeachProp::Hex(0xF2C4B4, 0.15f));
			break;
		}
		default:
			break;
		}
	}
}

UStaticMesh* TNBeachDecorKit::SearchMoundMesh(int32 Variant, bool bFlat)
{
	constexpr int32 FlatKey = 1000;
	const int32 Key = bFlat ? FlatKey : FMath::Clamp(Variant, 0, NumSearchMoundVariants - 1);
	static TMap<int32, TWeakObjectPtr<UStaticMesh>> Meshes;
	TWeakObjectPtr<UStaticMesh>& Slot = Meshes.FindOrAdd(Key);
	if (!Slot.IsValid())
	{
		TNProcMesh::FTNProcMeshBuffers Buffers;
		TNBeachDecorDetail::BuildSearchMound(Buffers, Key == FlatKey ? 0 : Key, bFlat);
		// Sin colisión (se pisa como la arena) y fuera del recolector: la comparten todos los montículos.
		Slot = TNBeachDecorDetail::MakeMesh(Buffers, nullptr);
	}
	return Slot.Get();
}

// ─────────────────────────────────────────────────────────────────────────────
// ATN_BeachDecor: una pieza como actor replicado (TN.Beach.Place y lo que otras piezas creen con SpawnElement). El
// decorado de cada ronda no pasa por aquí: lo monta instanciado ATN_BeachDecorField en cada máquina.
// ─────────────────────────────────────────────────────────────────────────────

ATN_BeachDecor::ATN_BeachDecor()
{
	// Solo tiene tick la parte animada, y solo cerca de una cámara (lo enciende y apaga UpdateAnimActivity).
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(GetRootComponent());
	BodyMesh->SetMobility(EComponentMobility::Movable);
	BodyMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	BodyMesh->SetGenerateOverlapEvents(false);

	AnimMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AnimMesh"));
	AnimMesh->SetupAttachment(BodyMesh);
	AnimMesh->SetMobility(EComponentMobility::Movable);
	AnimMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AnimMesh->SetGenerateOverlapEvents(false);
	AnimMesh->SetCanEverAffectNavigation(false);
	AnimMesh->SetVisibility(false);
}

void ATN_BeachDecor::ApplySpec()
{
	StopAnimation();
	ClearTiles();
	const ETNBeachElement Element = Spec.Element;
	if (TNBeach::CategoryOf(Element) != ETNBeachCategory::Decor)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Playa] ATN_BeachDecor con %s, que no es decorado: no se construye."), *UEnum::GetValueAsString(Element));
		// Sin malla: sin pieza de arte (se quita la de una construcción anterior).
		TNArt::SetMesh(BodyMesh, nullptr, NAME_None);
		TNArt::SetMesh(AnimMesh, nullptr, NAME_None);
		return;
	}
	// El tamaño va en la malla (el actor no se escala); fuera de 0,5-1,6 los escalones dejarían de poder subirse.
	const float Size = TNBeachDecorKit::ClampSize(Spec.SizeScale);
	if (TNBeachProp::IsTiled(Element))
	{
		BuildTiles(Element, Size);
	}
	else
	{
		BuildSingle(Element, Size);
	}
}

void ATN_BeachDecor::BuildSingle(ETNBeachElement Element, float Size)
{
	const TNBeachDecorKit::FRecipe Prop = TNBeachDecorKit::Single(Element, TNBeachDecorKit::VariantOf(Element, Spec.Seed));
	const TNBeachProp::FPropInfo& Info = Prop.Info;

	// Colocación de este ejemplar, igual en todas las máquinas (sale de Spec.Seed): giro, inclinación y hundimiento.
	BodyMesh->SetRelativeTransform(TNBeachDecorKit::BodyPlacement(Info, Spec.Seed, Size));

	UStaticMesh* BodyAsset = Prop.Body;
	const float Cull = TNBeachDecorKit::CullDistanceFor(Element, Size);
	BodyMesh->SetStaticMesh(BodyAsset);
	BodyMesh->SetCastShadow(Info.bCastShadow);
	BodyMesh->SetCullDistance(Cull);
	TNBeachDecorKit::SetupCollision(BodyMesh.Get(), Prop.bCollision, Info.bBlocksCamera);
	// Pieza de arte (Docs/Arte_Assets.md), con la sombra y la colisión ya puestas: las conserva el generado si no hay sustituto.
	TNArt::ApplyToComponent(BodyMesh.Get(), TNBeachDecorKit::BodySlot(Element));

	UStaticMesh* MovingAsset = Prop.Moving;
	AnimMesh->SetStaticMesh(MovingAsset);
	AnimMesh->SetVisibility(MovingAsset != nullptr);
	AnimMesh->SetRelativeTransform(FTransform(FQuat::Identity, Info.AnimPivot, FVector::OneVector));
	AnimMesh->SetCastShadow(Info.bCastShadow);
	AnimMesh->SetCullDistance(Cull);
	TNArt::ApplyToComponent(AnimMesh.Get(), TNBeachDecorKit::MovingSlot(Element));
	if (MovingAsset && Info.Anim != TNBeachProp::EAnim::None)
	{
		StartAnimation(static_cast<uint8>(Info.Anim), Info.AnimAxis, Info.AnimAmp, Info.AnimRate, TNBeachDecorKit::AnimRangeFor(Element, Size));
	}
}

void ATN_BeachDecor::BuildTiles(ETNBeachElement Element, float Size)
{
	TNArt::SetMesh(BodyMesh, nullptr, NAME_None);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TNArt::SetMesh(AnimMesh, nullptr, NAME_None);
	AnimMesh->SetVisibility(false);
	TMap<int32, TArray<FTransform>> ByPiece;
	TNBeachDecorKit::TilePlacements(Element, Spec.Seed, Size, Spec.Extent, ByPiece);

	USceneComponent* Root = GetRootComponent();
	for (TPair<int32, TArray<FTransform>>& Entry : ByPiece)
	{
		const TNBeachDecorKit::FRecipe Prop = TNBeachDecorKit::Piece(Element, Entry.Key);
		UStaticMesh* Mesh = Prop.Body;
		if (!Mesh) { continue; }
		UInstancedStaticMeshComponent* Ism = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
		Ism->SetupAttachment(Root);
		Ism->SetMobility(EComponentMobility::Movable);
		Ism->SetStaticMesh(Mesh);
		Ism->SetCastShadow(Prop.Info.bCastShadow);
		Ism->SetGenerateOverlapEvents(false);
		TNBeachDecorKit::SetupCollision(Ism, Prop.bCollision, false);
		Ism->RegisterComponent();
		Ism->AddInstances(Entry.Value, false, false);
		TNArt::ApplyToInstances(Ism, TNBeachDecorKit::PieceSlot(Element, Entry.Key));
		Tiles.Add(Ism);
	}
}

void ATN_BeachDecor::ClearTiles()
{
	for (UInstancedStaticMeshComponent* Ism : Tiles)
	{
		if (Ism) { Ism->DestroyComponent(); }
	}
	// Y los gemelos de colisión que deja TNArt::ApplyToInstances con un sustituto de arte (los ISM que no son de Tiles ni
	// mallas de arte): sin sustitutos no hay ninguno.
	TInlineComponentArray<UInstancedStaticMeshComponent*> Others;
	GetComponents(Others);
	for (UInstancedStaticMeshComponent* Ism : Others)
	{
		if (IsValid(Ism) && !Tiles.Contains(Ism) && !Ism->IsA<UTN_ArtMeshComponent>()) { Ism->DestroyComponent(); }
	}
	Tiles.Reset();
}

void ATN_BeachDecor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopAnimation();
	Super::EndPlay(EndPlayReason);
}

void ATN_BeachDecor::StartAnimation(uint8 Kind, const FVector& Axis, float Amp, float Rate, float Range)
{
	const UWorld* World = GetWorld();
	// Ni en el servidor dedicado ni en la ronda de prueba del editor.
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer) { return; }
	const FVector SafeAxis = Axis.GetSafeNormal();
	AnimKind = Kind;
	AnimAxis = SafeAxis.IsNearlyZero() ? FVector(0.0, 1.0, 0.0) : SafeAxis;
	AnimAmp = Amp;
	AnimRate = FMath::Max(0.01f, Rate);
	AnimRange = Range;
	const uint32 Seed = static_cast<uint32>(Spec.Seed);
	AnimPhase = TNBeachDecorKit::AnimPhaseOf(Spec.Seed);
	AnimTime = 0.f;
	ClamCycle = -1;
	bClamHeld = false;
	ApplyAnimPose();
	// Cada ejemplar mira a su ritmo (no todos en el mismo fotograma).
	const float FirstCheck = 0.1f + 0.5f * static_cast<float>(TNBeachProp::Rnd(Seed, 22));
	GetWorldTimerManager().SetTimer(AnimCheckTimer, this, &ATN_BeachDecor::UpdateAnimActivity, 0.5f, true, FirstCheck);
}

void ATN_BeachDecor::StopAnimation()
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AnimCheckTimer);
	}
	SetActorTickEnabled(false);
	AnimKind = 0;
}

void ATN_BeachDecor::UpdateAnimActivity()
{
	bool bNear = false;
	const UWorld* World = GetWorld();
	if (World && HasActorBegunPlay() && AnimKind != 0 && AnimMesh && AnimMesh->GetStaticMesh())
	{
		const FVector Here = AnimMesh->GetComponentLocation();
		const double RangeSq = FMath::Square(static_cast<double>(AnimRange));
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* PC = It->Get();
			if (PC && PC->IsLocalController() && PC->PlayerCameraManager
				&& FVector::DistSquared(PC->PlayerCameraManager->GetCameraLocation(), Here) < RangeSq)
			{
				bNear = true;
				break;
			}
		}
		// Si no se ha dibujado hace poco (detrás de la cámara o tapado), tampoco hace falta moverla.
		bNear = bNear && AnimMesh->WasRecentlyRendered(1.f);
	}
	if (bNear != IsActorTickEnabled())
	{
		SetActorTickEnabled(bNear);
	}
}

void ATN_BeachDecor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AnimTime += DeltaSeconds;
	ApplyAnimPose();
}

void ATN_BeachDecor::ApplyAnimPose()
{
	if (!AnimMesh || AnimKind == 0) { return; }
	TNBeachProp::FPropInfo Info;
	Info.Anim = static_cast<TNBeachProp::EAnim>(AnimKind);
	Info.AnimAxis = AnimAxis;
	Info.AnimAmp = AnimAmp;
	Info.AnimRate = AnimRate;
	TNBeachDecorKit::FAnimState State;
	State.ClamCycle = ClamCycle;
	State.bClamHeld = bClamHeld;
	const FTransform Pose = TNBeachDecorKit::AnimPose(Info, AnimTime, AnimPhase, Spec.Seed, State, [this]() { return IsSomeoneOnTop(); });
	ClamCycle = State.ClamCycle;
	bClamHeld = State.bClamHeld;
	// El pivote ya está puesto (BuildSingle): solo cambian el giro y la escala.
	AnimMesh->SetRelativeRotation(Pose.GetRotation());
	AnimMesh->SetRelativeScale3D(Pose.GetScale3D());
}

bool ATN_BeachDecor::IsSomeoneOnTop() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
	if (!GS) { return false; }
	const FVector Here = GetActorLocation();
	const double Reach = GetFootprintRadius() * 1.3 + 60.0;
	for (const APlayerState* PS : GS->PlayerArray)
	{
		const APawn* Turtle = PS ? PS->GetPawn() : nullptr;
		if (!Turtle) { continue; }
		const FVector Rel = Turtle->GetActorLocation() - Here;
		if (FVector(Rel.X, Rel.Y, 0.0).SizeSquared() < Reach * Reach && Rel.Z > -50.0 && Rel.Z < 500.0) { return true; }
	}
	return false;
}
