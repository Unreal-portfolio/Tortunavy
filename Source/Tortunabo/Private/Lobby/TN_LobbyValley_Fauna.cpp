// ─────────────────────────────────────────────────────────────────────────────
// ATN_LobbyValley — vida del valle (solo visual y local, nada en un servidor
// dedicado): animales de suelo en los sectores (cuerpos rígidos de la fauna del mapa
// procedural que pasean, saltan, picotean o vigilan), pájaros del castillo que van de
// almena en almena y bandadas en círculo sobre el castillo y los sectores.
// ─────────────────────────────────────────────────────────────────────────────

#include "Lobby/TN_LobbyValley.h"
#include "Lobby/TN_SandCastleLobby.h"
#include "Art/TN_Art.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "TN_LobbyValleyTerrain.h"
#include "../World/ProcMap/TN_ProcMapAmbientFX.h"
#include "../World/ProcMap/TN_ProcMapFaunaMeshes.h"
#include "../World/ProcMap/TN_ProcMapRuntimeMesh.h"

namespace TNValleyFauna
{
	using namespace TNLobbyValley;
	using TNProcMesh::FTNProcMeshBuffers;
	using TNFauna::ETNFaunaSpecies;

	enum class EAnimalState : uint8 { Idle, Walk, Hidden, Jump };

	/** Qué hace en reposo (rígido: se mueve el cuerpo entero). */
	enum class EAnimalAct : uint8 { Stand, Look, Peck, Graze, Sentinel, Sit, Hop, Bob, Croak };

	struct FFaunaPick
	{
		ETNFaunaSpecies Species = ETNFaunaSpecies::Crab;
		int32 Count = 0;
	};

	/** Animales de cada sector: pocos y de los que se reconocen de lejos. */
	int32 PicksFor(EValleyStyle Style, FFaunaPick (&Out)[3])
	{
		using SP = ETNFaunaSpecies;
		int32 Num = 0;
		auto Add = [&Out, &Num](SP InSpecies, int32 InCount)
		{
			if (Num < 3)
			{
				Out[Num].Species = InSpecies;
				Out[Num].Count = InCount;
				++Num;
			}
		};
		switch (Style)
		{
			case EValleyStyle::Lagoon:   Add(SP::Flamingo, 4); Add(SP::Pelican, 1); Add(SP::Fish, 2); break;
			case EValleyStyle::Beach:    Add(SP::BabyTurtle, 3); Add(SP::Crab, 2); Add(SP::Gull, 1); break;
			case EValleyStyle::Dunes:    Add(SP::Meerkat, 3); Add(SP::Roadrunner, 1); break;
			case EValleyStyle::Canyon:   Add(SP::Lizard, 2); Add(SP::Vulture, 1); break;
			case EValleyStyle::Volcano:  Add(SP::FireBeetle, 2); Add(SP::Salamander, 2); break;
			case EValleyStyle::Cliffs:   Add(SP::Ibex, 2); Add(SP::Eagle, 1); break;
			case EValleyStyle::Snow:     Add(SP::Marmot, 2); Add(SP::Ibex, 1); break;
			case EValleyStyle::Forest:   Add(SP::Rabbit, 3); break;
			case EValleyStyle::Village:  Add(SP::Hen, 3); Add(SP::Cat, 1); break;
			case EValleyStyle::Farms:    Add(SP::Pigeon, 3); Add(SP::Hen, 1); break;
			case EValleyStyle::Jungle:   Add(SP::Monkey, 3); Add(SP::Toucan, 1); Add(SP::Capybara, 2); break;
			case EValleyStyle::Mangrove:
			default:                     Add(SP::Heron, 2); Add(SP::FiddlerCrab, 2); Add(SP::TreeFrog, 2); break;
		}
		return Num;
	}

	/**
	 * Pieza de arte de cada animal del valle (Docs/Arte_Assets.md): el animal entero en reposo, con su parte que brilla.
	 * Las gaviotas y palomas posadas del castillo son las mismas.
	 */
	FName FaunaSlot(ETNFaunaSpecies Species)
	{
		using SP = ETNFaunaSpecies;
		switch (Species)
		{
			case SP::Flamingo:    return TN_ART("Lobby.Valley.Fauna.Flamingo");
			case SP::Pelican:     return TN_ART("Lobby.Valley.Fauna.Pelican");
			case SP::Fish:        return TN_ART("Lobby.Valley.Fauna.Fish");
			case SP::BabyTurtle:  return TN_ART("Lobby.Valley.Fauna.BabyTurtle");
			case SP::Crab:        return TN_ART("Lobby.Valley.Fauna.Crab");
			case SP::Gull:        return TN_ART("Lobby.Valley.Fauna.Gull");
			case SP::Meerkat:     return TN_ART("Lobby.Valley.Fauna.Meerkat");
			case SP::Roadrunner:  return TN_ART("Lobby.Valley.Fauna.Roadrunner");
			case SP::Lizard:      return TN_ART("Lobby.Valley.Fauna.Lizard");
			case SP::Vulture:     return TN_ART("Lobby.Valley.Fauna.Vulture");
			case SP::FireBeetle:  return TN_ART("Lobby.Valley.Fauna.FireBeetle");
			case SP::Salamander:  return TN_ART("Lobby.Valley.Fauna.Salamander");
			case SP::Ibex:        return TN_ART("Lobby.Valley.Fauna.Ibex");
			case SP::Eagle:       return TN_ART("Lobby.Valley.Fauna.Eagle");
			case SP::Marmot:      return TN_ART("Lobby.Valley.Fauna.Marmot");
			case SP::Rabbit:      return TN_ART("Lobby.Valley.Fauna.Rabbit");
			case SP::Hen:         return TN_ART("Lobby.Valley.Fauna.Hen");
			case SP::Cat:         return TN_ART("Lobby.Valley.Fauna.Cat");
			case SP::Pigeon:      return TN_ART("Lobby.Valley.Fauna.Pigeon");
			case SP::Monkey:      return TN_ART("Lobby.Valley.Fauna.Monkey");
			case SP::Toucan:      return TN_ART("Lobby.Valley.Fauna.Toucan");
			case SP::Capybara:    return TN_ART("Lobby.Valley.Fauna.Capybara");
			case SP::Heron:       return TN_ART("Lobby.Valley.Fauna.Heron");
			case SP::FiddlerCrab: return TN_ART("Lobby.Valley.Fauna.FiddlerCrab");
			case SP::TreeFrog:    return TN_ART("Lobby.Valley.Fauna.TreeFrog");
			default:              return NAME_None;
		}
	}

	/** Aumento para que se lean desde el castillo (a 60-120 m): los pequeños, más. */
	float ViewScaleOf(const TNFauna::FTNFaunaSpec& Sp)
	{
		return FMath::Clamp(1.35f + (60.f - Sp.Length) / 70.f, 1.3f, 2.3f);
	}

	/**
	 * Junta las piezas de una especie en una malla rígida en reposo (alas plegadas contra el costado, como en
	 * ATN_ProcFauna): Solid con el material de la vegetación, Glow con el que brilla.
	 */
	void MergeParts(const TArray<TNFauna::FTNFaunaPart>& Parts, double BodyZ, FTNProcMeshBuffers& Solid, FTNProcMeshBuffers& Glow)
	{
		using TNFauna::ETNFaunaBone;
		for (const TNFauna::FTNFaunaPart& Part : Parts)
		{
			FQuat Rot = FQuat::Identity;
			FVector Scale3 = FVector::OneVector;
			FVector Offset(0.0, 0.0, BodyZ);
			if (Part.Bone != ETNFaunaBone::Body)
			{
				Offset += Part.Pivot;
				if (Part.Bone == ETNFaunaBone::WingL || Part.Bone == ETNFaunaBone::WingR)
				{
					const double Side = Part.Bone == ETNFaunaBone::WingL ? -1.0 : 1.0;
					Rot = FQuat(FVector::ForwardVector, FMath::DegreesToRadians(100.0 * Side)) * FQuat(FVector::UpVector, FMath::DegreesToRadians(90.0 * Side));
					Scale3 = FVector(0.8, 0.5, 1.0);
				}
			}
			FTNProcMeshBuffers& Dst = Part.bGlow ? Glow : Solid;
			const FTransform Xf(Rot, Offset, Scale3);
			const int32 Base = Dst.Verts.Num();
			for (int32 v = 0; v < Part.Mesh.Verts.Num(); ++v)
			{
				Dst.Verts.Add(Xf.TransformPosition(Part.Mesh.Verts[v]));
				Dst.Normals.Add(Rot.RotateVector(Part.Mesh.Normals[v]));
				Dst.UVs.Add(Part.Mesh.UVs[v]);
				Dst.Colors.Add(Part.Mesh.Colors[v]);
			}
			for (const int32 Index : Part.Mesh.Tris) { Dst.Tris.Add(Base + Index); }
		}
	}

	/** Acción de reposo al azar entre las de la especie (mirar alrededor sale más). */
	EAnimalAct PickAct(const TNFauna::FTNFaunaSpec& Sp, float Pick01)
	{
		using TNFauna::ETNFaunaAct;
		auto Has = [&Sp](ETNFaunaAct InAct) { return (Sp.Acts & TNFauna::TNFaunaActBit(InAct)) != 0; };
		EAnimalAct Options[12];
		int32 Num = 0;
		Options[Num++] = EAnimalAct::Look;
		if (Has(ETNFaunaAct::Look)) { Options[Num++] = EAnimalAct::Look; }
		if (Has(ETNFaunaAct::Peck)) { Options[Num++] = EAnimalAct::Peck; }
		if (Has(ETNFaunaAct::Graze)) { Options[Num++] = EAnimalAct::Graze; }
		if (Has(ETNFaunaAct::Sentinel)) { Options[Num++] = EAnimalAct::Sentinel; }
		if (Has(ETNFaunaAct::Sit)) { Options[Num++] = EAnimalAct::Sit; }
		if (Has(ETNFaunaAct::Hop)) { Options[Num++] = EAnimalAct::Hop; }
		if (Has(ETNFaunaAct::PushUp) || Has(ETNFaunaAct::Claws) || Has(ETNFaunaAct::Dig) || Has(ETNFaunaAct::Scratch)) { Options[Num++] = EAnimalAct::Bob; }
		if (Has(ETNFaunaAct::Croak)) { Options[Num++] = EAnimalAct::Croak; }
		Options[Num++] = EAnimalAct::Stand;
		return Options[FMath::Clamp(static_cast<int32>(Pick01 * Num), 0, Num - 1)];
	}

	/** Peso (0..1) de la acción en curso: entra y sale en 0,35 s. */
	float ActWeight(float T, float Dur)
	{
		return FMath::Clamp(FMath::Min(T, Dur - T) / 0.35f, 0.f, 1.f);
	}

	/** Gira From hacia To (grados) como mucho MaxStep. */
	float TurnToward(float From, float To, float MaxStep)
	{
		const float Delta = FMath::FindDeltaAngleDegrees(From, To);
		return FMath::UnwindDegrees(From + FMath::Clamp(Delta, -MaxStep, MaxStep));
	}

	FVector Bezier(const FVector& A, const FVector& C, const FVector& B, float T)
	{
		const float U = 1.f - T;
		return A * (U * U) + C * (2.f * U * T) + B * (T * T);
	}

	FVector BezierTangent(const FVector& A, const FVector& C, const FVector& B, float T)
	{
		return (C - A) * (2.f * (1.f - T)) + (B - C) * (2.f * T);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Animales de suelo
// ─────────────────────────────────────────────────────────────────────────────

bool ATN_LobbyValley::HabitatOk(uint8 InSpecies, const FVector2D& P, double Z) const
{
	using TNFauna::ETNFaunaHabitat;
	const TNFauna::FTNFaunaSpec& Sp = TNFauna::TNFaunaSpec(static_cast<TNFauna::ETNFaunaSpecies>(InSpecies));
	const double Rad = P.Size();
	// Fuera del castillo y del valle cercano, pero a la vista.
	if (Rad < TNLobbyValley::FaunaMinR - 300.0 || Rad > TNLobbyValley::FaunaMaxR + 1500.0) { return false; }
	const double Wz = TNLobbyValley::WaterZ;
	switch (Sp.Habitat)
	{
		case ETNFaunaHabitat::Water:
			return Z < Wz - 60.0;
		case ETNFaunaHabitat::Shallows:
			return Z > Wz - 160.0 && Z < Wz - 15.0;
		case ETNFaunaHabitat::Shore:
			return Z > Wz - 25.0 && Z < Wz + 55.0;
		case ETNFaunaHabitat::Land:
		default:
		{
			if (Z < Wz + 25.0 || !Grid.IsValid()) { return false; }
			// Los que trepan (cabras, águila) aguantan más pendiente.
			const bool bClimber = Sp.bHighGround || Sp.Flee == TNFauna::ETNFaunaFlee::Climb;
			return Grid->NormalAt(P).Z > (bClimber ? 0.55 : 0.8);
		}
	}
}

void ATN_LobbyValley::BuildFauna()
{
	using namespace TNValleyFauna;
	Animals.Reset();
	Kinds.Reset();
	if (!Grid.IsValid() || MaxAnimals <= 0) { return; }
	const FTNLobbyValleyGrid& G = *Grid;
	TNProcMap::FRng Rng(static_cast<uint64>(SeedU()) * 0xFA17ull + 5ull);
	SimRng = static_cast<uint32>(Rng.Next() | 1ull);
	int32 KindOf[TNFauna::TNFaunaNumSpecies];
	for (int32& KindIdx : KindOf) { KindIdx = INDEX_NONE; }
	int32 NextLookout = 0;

	for (int32 Sec = 0; Sec < NumSectors; ++Sec)
	{
		FFaunaPick Picks[3];
		const int32 NumPicks = PicksFor(ValleySectorAt(Sec).Style, Picks);
		for (int32 p = 0; p < NumPicks && Animals.Num() < MaxAnimals; ++p)
		{
			const TNFauna::FTNFaunaSpec& Sp = TNFauna::TNFaunaSpec(Picks[p].Species);
			const uint8 SpId = static_cast<uint8>(Picks[p].Species);
			const float View = ViewScaleOf(Sp);
			// El águila, en la cima de su aguja; el resto, donde su hábitat lo permita (los de las alturas, lo más alto).
			const bool bPerchOnly = Sp.Body == TNFauna::ETNFaunaBody::Bird && Sp.bHighGround;
			FVector Center = FVector::ZeroVector;
			bool bFound = false;
			if (bPerchOnly)
			{
				if (!Lookouts.IsValidIndex(NextLookout)) { continue; }
				Center = Lookouts[NextLookout++];
				bFound = true;
			}
			for (int32 Try = 0, Valid = 0; Try < 80 && !bPerchOnly && Valid < (Sp.bHighGround ? 8 : 1); ++Try)
			{
				const FVector2D P = ValleyClockPoint(Sec + Rng.Range(-0.36, 0.36), Rng.Range(FaunaMinR, FaunaMaxR));
				if (IsKeptOut(P, 150.0)) { continue; }
				const double Z = G.HeightAt(P);
				if (!HabitatOk(SpId, P, Z)) { continue; }
				++Valid;
				if (!bFound || Z > Center.Z)
				{
					Center = FVector(P, Z);
					bFound = true;
				}
			}
			if (!bFound) { continue; }
			for (int32 g = 0; g < Picks[p].Count && Animals.Num() < MaxAnimals; ++g)
			{
				FVector Where = Center;
				if (g > 0 && !bPerchOnly)
				{
					for (int32 Try = 0; Try < 8; ++Try)
					{
						const double Ang = Rng.Range(0.0, TNProcMap::TwoPi);
						const double Dist = Sp.GroupSpread * View * Rng.Range(0.3, 1.0);
						const FVector2D Q(Center.X + FMath::Cos(Ang) * Dist, Center.Y + FMath::Sin(Ang) * Dist);
						const double Z = G.HeightAt(Q);
						if (HabitatOk(SpId, Q, Z) && !IsKeptOut(Q, 100.0))
						{
							Where = FVector(Q, Z);
							break;
						}
					}
				}
				FValleyAnimal& A = Animals.AddDefaulted_GetRef();
				A.Species = SpId;
				A.Pos = Where;
				A.Home = Where;
				A.Goal = Where;
				A.Yaw = static_cast<float>(Rng.Range(-180.0, 180.0));
				A.BaseYaw = A.Yaw;
				A.Size = View * static_cast<float>(Rng.Range(Sp.ScaleMin, Sp.ScaleMax));
				A.Clock = static_cast<float>(Rng.Range(0.0, 20.0));
				A.Dur = static_cast<float>(Rng.Range(0.5, 4.0));
				A.SideSign = static_cast<int8>(Rng.Chance(0.5) ? 1 : -1);
				A.bFixed = bPerchOnly;
				A.Act = static_cast<uint8>(EAnimalAct::Look);
				const bool bFish = Sp.Body == TNFauna::ETNFaunaBody::Fish && Sp.Habitat == TNFauna::ETNFaunaHabitat::Water;
				A.State = static_cast<uint8>(bFish ? EAnimalState::Hidden : EAnimalState::Idle);
				int32& KindIdx = KindOf[SpId];
				if (KindIdx == INDEX_NONE)
				{
					KindIdx = Kinds.Num();
					Kinds.AddDefaulted_GetRef().Species = SpId;
				}
				A.Kind = KindIdx;
				A.Slot = Kinds[KindIdx].Xf.Num();
				Kinds[KindIdx].Xf.Add(FTransform::Identity);
			}
		}
	}
	if (Animals.Num() == 0) { return; }

	// Una malla rígida por especie (y la parte que brilla aparte), instanciada con todos los suyos.
	UMaterialInterface* SolidMat = ValleyFoliageMaterial();
	UMaterialInterface* GlowMat = ValleyGlowMaterial();
	TArray<FTNProcMeshBuffers> SolidParts;
	TArray<FTNProcMeshBuffers> GlowParts;
	SolidParts.SetNum(Kinds.Num());
	GlowParts.SetNum(Kinds.Num());
	for (int32 k = 0; k < Kinds.Num(); ++k)
	{
		FValleyKind& K = Kinds[k];
		TArray<TNFauna::FTNFaunaPart> Parts;
		TNFauna::FTNFaunaRig Rig;
		TNFauna::TNFaunaBuildSpecies(static_cast<ETNFaunaSpecies>(K.Species), Parts, Rig);
		K.BodyZ = static_cast<float>(Rig.BodyZ);
		K.HalfLen = static_cast<float>(Rig.HalfLen);
		K.Draft = static_cast<float>(Rig.Draft);
		MergeParts(Parts, Rig.BodyZ, SolidParts[k], GlowParts[k]);
	}
	for (const FValleyAnimal& A : Animals) { Kinds[A.Kind].Xf[A.Slot] = PoseAnimal(A); }
	for (int32 k = 0; k < Kinds.Num(); ++k)
	{
		FValleyKind& K = Kinds[k];
		const FName Slot = TNValleyFauna::FaunaSlot(static_cast<ETNFaunaSpecies>(K.Species));
		if (UStaticMesh* SolidMesh = TNProcRuntimeMesh::MakeStaticMesh(this, SolidParts[k], SolidMat, false, 0.f, 1.f, 0.f))
		{
			GeneratedMeshes.Add(SolidMesh);
			if (UInstancedStaticMeshComponent* Comp = MakeInstanced(SolidMesh, false, false, 0))
			{
				Comp->AddInstances(K.Xf, false, false);
				TNArt::ApplyToInstances(Comp, Slot);
				K.Solid = Comp;
			}
		}
		// Con sustituto de arte, la parte que brilla ya va en su malla.
		if (TNArt::Find(Slot))
		{
			continue;
		}
		if (UStaticMesh* GlowMesh = TNProcRuntimeMesh::MakeStaticMesh(this, GlowParts[k], GlowMat, false, 0.f, 1.f, 0.f))
		{
			GeneratedMeshes.Add(GlowMesh);
			if (UInstancedStaticMeshComponent* Comp = MakeInstanced(GlowMesh, false, false, 0))
			{
				Comp->AddInstances(K.Xf, false, false);
				K.Glow = Comp;
			}
		}
	}
}

bool ATN_LobbyValley::PickGoal(FValleyAnimal& A)
{
	const TNFauna::FTNFaunaSpec& Sp = TNFauna::TNFaunaSpec(static_cast<TNFauna::ETNFaunaSpecies>(A.Species));
	if (!Grid.IsValid()) { return false; }
	// Paseos cortos alrededor de su sitio: siempre en su sector y a la vista.
	const float Wander = FMath::Clamp(Sp.WanderRadius * A.Size * 0.8f, 150.f, 900.f);
	for (int32 Try = 0; Try < 6; ++Try)
	{
		const float Ang = RandIn(0.f, UE_TWO_PI);
		const float Dist = Wander * FMath::Sqrt(RandUnit());
		const FVector2D Q(A.Home.X + FMath::Cos(Ang) * Dist, A.Home.Y + FMath::Sin(Ang) * Dist);
		const double Z = Grid->HeightAt(Q);
		if (!HabitatOk(A.Species, Q, Z) || IsKeptOut(Q, 80.0)) { continue; }
		A.Goal = FVector(Q, Z);
		return true;
	}
	return false;
}

void ATN_LobbyValley::SimAnimal(FValleyAnimal& A, float Dt)
{
	using namespace TNValleyFauna;
	using TNFauna::ETNFaunaGait;
	const TNFauna::FTNFaunaSpec& Sp = TNFauna::TNFaunaSpec(static_cast<ETNFaunaSpecies>(A.Species));
	const FValleyKind& K = Kinds[A.Kind];
	const float Sz = A.Size;
	const double Wz = TNLobbyValley::WaterZ;
	A.Clock += Dt;
	A.StateT += Dt;
	A.Air = 0.f;
	A.Roll = 0.f;
	A.Pulse = 0.f;
	const bool bFloat = Sp.Gait == ETNFaunaGait::Swim && Sp.Body != TNFauna::ETNFaunaBody::Fish;
	const bool bHopper = Sp.Gait == ETNFaunaGait::Hop || Sp.HopHeight > 0.f;

	auto ToIdle = [this, &A, &Sp]()
	{
		A.State = static_cast<uint8>(EAnimalState::Idle);
		A.StateT = 0.f;
		A.Yaw = FMath::UnwindDegrees(A.Yaw);
		A.BaseYaw = A.Yaw;
		A.Act = static_cast<uint8>(PickAct(Sp, RandUnit()));
		A.Dur = RandIn(2.f, 6.f);
	};

	switch (static_cast<EAnimalState>(A.State))
	{
		case EAnimalState::Hidden:
		{
			// Pez bajo el agua: al rato salta cerca de su sitio.
			if (A.StateT < A.Dur) { break; }
			A.StateT = 0.f;
			A.Dur = RandIn(1.f, 3.f);
			for (int32 Try = 0; Try < 5; ++Try)
			{
				const float Ang = RandIn(0.f, UE_TWO_PI);
				const float Dist = RandIn(0.f, 500.f * Sz);
				const float Heading = RandIn(0.f, UE_TWO_PI);
				const FVector2D From(A.Home.X + FMath::Cos(Ang) * Dist, A.Home.Y + FMath::Sin(Ang) * Dist);
				const FVector2D To = From + FVector2D(FMath::Cos(Heading), FMath::Sin(Heading)) * (230.0 * Sz);
				if (Grid->HeightAt(From) < Wz - 50.0 && Grid->HeightAt(To) < Wz - 50.0)
				{
					A.JumpFrom = FVector(From, Wz);
					A.JumpTo = FVector(To, Wz);
					A.Yaw = FMath::RadiansToDegrees(Heading);
					A.State = static_cast<uint8>(EAnimalState::Jump);
					A.Dur = RandIn(0.8f, 1.1f);
					Splash(A.JumpFrom);
					break;
				}
			}
			break;
		}
		case EAnimalState::Jump:
		{
			// Arco fuera del agua, con el morro siguiendo la trayectoria.
			const float T = FMath::Clamp(A.StateT / A.Dur, 0.f, 1.f);
			const float Arc = 130.f * Sz;
			A.Pos = FMath::Lerp(A.JumpFrom, A.JumpTo, static_cast<double>(T));
			A.Air = 4.f * Arc * T * (1.f - T);
			const float Span = FMath::Max(1.f, static_cast<float>(FVector::Dist2D(A.JumpFrom, A.JumpTo)));
			A.Pitch = FMath::RadiansToDegrees(FMath::Atan(4.f * Arc * (1.f - 2.f * T) / Span));
			if (T >= 1.f)
			{
				Splash(A.JumpTo);
				A.State = static_cast<uint8>(EAnimalState::Hidden);
				A.StateT = 0.f;
				A.Dur = RandIn(2.f, 7.f);
				A.Pitch = 0.f;
			}
			break;
		}
		case EAnimalState::Walk:
		{
			const FVector2D ToGoal(A.Goal.X - A.Pos.X, A.Goal.Y - A.Pos.Y);
			const float Dist = static_cast<float>(ToGoal.Size());
			if (Dist < 25.f * Sz || A.StateT > A.Dur)
			{
				ToIdle();
				break;
			}
			const FVector2D Dir = ToGoal / Dist;
			const float Heading = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Dir.Y), static_cast<float>(Dir.X)));
			// Los cangrejos andan de lado.
			const bool bSide = Sp.Gait == ETNFaunaGait::Side;
			A.Yaw = TurnToward(A.Yaw, bSide ? Heading - 90.f * A.SideSign : Heading, Sp.TurnRate * Dt);
			const float Aligned = bSide ? 1.f : FMath::Clamp(1.f - FMath::Abs(FMath::FindDeltaAngleDegrees(A.Yaw, Heading)) / 90.f, 0.f, 1.f);
			const float Speed = FMath::Min(Sp.WalkSpeed * Sz * 0.8f, 420.f) * Aligned;
			A.Pitch = 0.f;
			if (bHopper)
			{
				// A saltitos: cada salto, un arco con el morro arriba al despegar y abajo al caer.
				const float Stride = FMath::Max(Sp.Stride, 15.f) * Sz;
				const float HopDur = FMath::Clamp(Stride / FMath::Max(1.f, Sp.WalkSpeed * Sz * 0.8f), 0.22f, 0.8f);
				A.Gait += Dt / HopDur;
				const float F = FMath::Frac(A.Gait);
				A.Air = FMath::Max(Sp.HopHeight, 8.f) * Sz * 4.f * F * (1.f - F);
				A.Pitch = 12.f * (1.f - 2.f * F);
			}
			else
			{
				// Andando: contoneo y un poco de rebote.
				A.Gait += Speed * Dt / FMath::Max(5.f, Sp.Stride * Sz);
				A.Air = FMath::Abs(FMath::Sin(A.Gait * UE_PI)) * 1.5f * Sz;
				A.Roll = FMath::Sin(A.Gait * UE_PI) * (bSide ? 3.f : 5.f);
			}
			const FVector2D Next(A.Pos.X + Dir.X * Speed * Dt, A.Pos.Y + Dir.Y * Speed * Dt);
			if (!HabitatOk(A.Species, Next, Grid->HeightAt(Next)))
			{
				ToIdle();
				break;
			}
			A.Pos.X = Next.X;
			A.Pos.Y = Next.Y;
			break;
		}
		case EAnimalState::Idle:
		default:
		{
			A.Pitch = 0.f;
			const float W = ActWeight(A.StateT, A.Dur);
			switch (static_cast<EAnimalAct>(A.Act))
			{
				case EAnimalAct::Look:
					A.Yaw = A.BaseYaw + 35.f * FMath::Sin(A.StateT * 1.1f) * W;
					break;
				case EAnimalAct::Peck:
				{
					const float Burst = FMath::Sin(A.StateT * 1.3f) > -0.2f ? 1.f : 0.f;
					A.Pitch = -30.f * FMath::Square(FMath::Max(0.f, FMath::Sin(A.StateT * 7.f))) * W * Burst;
					break;
				}
				case EAnimalAct::Graze:
					A.Pitch = (-14.f + 3.f * FMath::Sin(A.StateT * 8.f)) * W;
					break;
				case EAnimalAct::Sentinel:
					// De pie sobre las patas de atrás, vigilando.
					A.Pitch = 62.f * W;
					A.Yaw = A.BaseYaw + 25.f * FMath::Sin(A.StateT * 0.9f) * W;
					break;
				case EAnimalAct::Sit:
					A.Pitch = 25.f * W;
					break;
				case EAnimalAct::Hop:
				{
					const float F = FMath::Frac(A.StateT * 1.4f);
					A.Air = FMath::Max(Sp.HopHeight, 10.f) * Sz * 4.f * F * (1.f - F) * W;
					break;
				}
				case EAnimalAct::Bob:
					A.Pitch = 9.f * (0.5f + 0.5f * FMath::Sin(A.StateT * 9.f)) * W;
					A.Roll = 3.f * FMath::Sin(A.StateT * 11.f) * W;
					break;
				case EAnimalAct::Croak:
					A.Pulse = W * FMath::Max(0.f, FMath::Sin(A.StateT * 12.f));
					break;
				case EAnimalAct::Stand:
				default:
					A.Pitch = 1.2f * FMath::Sin(A.Clock * 2.f);
					break;
			}
			if (A.StateT >= A.Dur)
			{
				A.Yaw = FMath::UnwindDegrees(A.Yaw);
				A.BaseYaw = A.Yaw;
				A.StateT = 0.f;
				if (!A.bFixed && RandUnit() < 0.55f && PickGoal(A))
				{
					A.State = static_cast<uint8>(EAnimalState::Walk);
					A.Dur = 25.f;
					A.Gait = 0.f;
				}
				else
				{
					ToIdle();
				}
			}
			break;
		}
	}

	// Cota: la del suelo (o la del agua, si flota); los peces la llevan en su salto.
	const EAnimalState St = static_cast<EAnimalState>(A.State);
	if ((St == EAnimalState::Idle || St == EAnimalState::Walk) && !A.bFixed)
	{
		if (bFloat)
		{
			A.Pos.Z = Wz - K.Draft * Sz + 1.5f * FMath::Sin(A.Clock * 1.7f);
			A.Roll += 3.f * FMath::Sin(A.Clock * 1.1f);
		}
		else
		{
			A.Pos.Z = Grid->HeightAt(FVector2D(A.Pos.X, A.Pos.Y));
		}
	}
}

FTransform ATN_LobbyValley::PoseAnimal(const FValleyAnimal& A) const
{
	using namespace TNValleyFauna;
	if (static_cast<EAnimalState>(A.State) == EAnimalState::Hidden)
	{
		return FTransform(FQuat::Identity, A.Pos - FVector(0.0, 0.0, 300.0), FVector::ZeroVector);
	}
	const FValleyKind& K = Kinds[A.Kind];
	const float Sz = A.Size;
	FVector Loc = A.Pos + FVector(0.0, 0.0, A.Air);
	if (!FMath::IsNearlyZero(A.Pitch) && static_cast<EAnimalState>(A.State) != EAnimalState::Jump)
	{
		// Gira por la cadera: la cola se queda en el suelo al ponerse de pie o al bajar la cabeza.
		const FVector Hip = FVector(-K.HalfLen * 0.7f, 0.f, K.BodyZ * 0.35f) * Sz;
		const FVector Offset = Hip - FRotator(A.Pitch, 0.f, 0.f).RotateVector(Hip);
		Loc += FRotator(0.f, A.Yaw, 0.f).RotateVector(Offset);
	}
	FVector Scale3(Sz);
	if (A.Pulse > 0.f)
	{
		Scale3.Y *= 1.0 + 0.08 * A.Pulse;
		Scale3.Z *= 1.0 + 0.05 * A.Pulse;
	}
	return FTransform(FRotator(A.Pitch, A.Yaw, A.Roll), Loc, Scale3);
}

void ATN_LobbyValley::TickFauna(float Dt)
{
	if (!Grid.IsValid() || Animals.Num() == 0) { return; }
	for (FValleyAnimal& A : Animals)
	{
		SimAnimal(A, Dt);
		Kinds[A.Kind].Xf[A.Slot] = PoseAnimal(A);
	}
	for (FValleyKind& K : Kinds)
	{
		if (UInstancedStaticMeshComponent* SolidComp = K.Solid.Get()) { TNArt::UpdateInstances(SolidComp, 0, K.Xf, false, false, false); }
		if (UInstancedStaticMeshComponent* GlowComp = K.Glow.Get()) { TNArt::UpdateInstances(GlowComp, 0, K.Xf, false, false, false); }
	}
}

void ATN_LobbyValley::Splash(const FVector& LocalPos)
{
	if (TNAmbientFX::FEmitter* Emitter = TNAmbientFX::GetEmitter(this, SplashFX))
	{
		Emitter->Origin = GetActorTransform().TransformPosition(LocalPos);
		TNAmbientFX::Burst(*Emitter, 6);
	}
}

float ATN_LobbyValley::RandUnit()
{
	SimRng ^= SimRng << 13;
	SimRng ^= SimRng >> 17;
	SimRng ^= SimRng << 5;
	return static_cast<float>(SimRng & 0xFFFFFFu) / 16777215.f;
}

float ATN_LobbyValley::RandIn(float Lo, float Hi)
{
	return Lo + (Hi - Lo) * RandUnit();
}

// ─────────────────────────────────────────────────────────────────────────────
// Bandadas en círculo (TNAmbientFX)
// ─────────────────────────────────────────────────────────────────────────────

void ATN_LobbyValley::StartFlocks()
{
	using namespace TNLobbyValley;
	const FTransform Xf = GetActorTransform();
	auto Flock = [this, &Xf](double Hour, double Dist, double AboveZ, int32 BirdCount, float CircleR, float AngularSpeed, float BirdSize,
		const FLinearColor& Body, const FLinearColor& Wing, uint32 FlockSeed)
	{
		const FVector2D C = ValleyClockPoint(Hour, Dist);
		const double Ground = (Grid.IsValid() && Dist > 3000.0) ? FMath::Max(0.0, Grid->HeightAt(C)) : 0.0;
		TNAmbientFX::AddFlock(this, Xf.TransformPosition(FVector(C, Ground + AboveZ)), BirdCount, CircleR, AngularSpeed, BirdSize, Body, Wing, FlockSeed);
	};
	const FLinearColor GullBody(0.95f, 0.95f, 0.93f);
	const FLinearColor GullWing(0.68f, 0.7f, 0.74f);
	const FLinearColor Dark(0.14f, 0.13f, 0.12f);
	const FLinearColor DarkWing(0.24f, 0.2f, 0.16f);
	// Sobre el castillo: gaviotas altas y golondrinas rápidas por encima de la plaza.
	Flock(0.0, 0.0, 2500.0, 5, 2100.f, 0.15f, 1.0f, GullBody, GullWing, 101u);
	Flock(0.0, 600.0, 1500.0, 7, 1300.f, -0.55f, 0.42f, FLinearColor(0.06f, 0.08f, 0.2f), FLinearColor(0.1f, 0.12f, 0.28f), 102u);
	// Sobre los sectores.
	Flock(0.1, 10500.0, 2600.0, 6, 2800.f, 0.19f, 1.1f, GullBody, GullWing, 103u);
	Flock(1.1, 9500.0, 2200.0, 4, 2000.f, -0.17f, 1.05f, GullBody, GullWing, 104u);
	Flock(2.6, 11500.0, 4800.0, 3, 3200.f, 0.07f, 1.7f, FLinearColor(0.2f, 0.15f, 0.1f), FLinearColor(0.3f, 0.22f, 0.14f), 105u);
	Flock(5.9, 15000.0, 6500.0, 2, 3600.f, -0.06f, 1.9f, FLinearColor(0.3f, 0.2f, 0.1f), FLinearColor(0.45f, 0.32f, 0.15f), 106u);
	Flock(7.3, 10500.0, 2600.0, 7, 2100.f, 0.3f, 0.55f, Dark, DarkWing, 107u);
	Flock(8.6, 10000.0, 2200.0, 6, 2300.f, -0.27f, 0.6f, FLinearColor(0.55f, 0.57f, 0.62f), FLinearColor(0.42f, 0.44f, 0.5f), 108u);
	Flock(10.2, 10500.0, 2700.0, 5, 2500.f, 0.3f, 0.9f, FLinearColor(0.85f, 0.12f, 0.08f), FLinearColor(0.1f, 0.45f, 0.85f), 109u);
	Flock(11.1, 9200.0, 2300.0, 3, 2600.f, -0.12f, 1.3f, FLinearColor(0.97f, 0.97f, 0.95f), FLinearColor(0.85f, 0.85f, 0.8f), 110u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pájaros del castillo: de almena en almena
// ─────────────────────────────────────────────────────────────────────────────

void ATN_LobbyValley::StartCastleBirds()
{
	using namespace TNValleyFauna;
	CastleBirdList.Reset();
	Perches.Reset();
	bPerchesReady = false;
	PerchRetry = 0.4f;
	PerchTries = 0;
	const int32 Count = FMath::Clamp(CastleBirds, 0, 12);
	if (Count == 0) { return; }
	int32 PerType[2] = { 0, 0 };
	for (int32 i = 0; i < Count; ++i)
	{
		FCastleBird& B = CastleBirdList.AddDefaulted_GetRef();
		// Dos gaviotas por cada paloma.
		B.Type = (i % 3 == 2) ? 1 : 0;
		B.Slot = PerType[B.Type]++;
	}
	UMaterialInterface* BirdMat = ValleyBirdMaterial();
	UMaterialInterface* SolidMat = ValleyFoliageMaterial();
	const FLinearColor Bodies[2] = { FLinearColor(0.95f, 0.95f, 0.93f), FLinearColor(0.55f, 0.57f, 0.62f) };
	const FLinearColor Wings[2] = { FLinearColor(0.68f, 0.7f, 0.74f), FLinearColor(0.42f, 0.44f, 0.5f) };
	const ETNFaunaSpecies PerchSpecies[2] = { ETNFaunaSpecies::Gull, ETNFaunaSpecies::Pigeon };
	const FTransform Gone(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);
	for (int32 t = 0; t < 2; ++t)
	{
		BirdFlyXf[t].Init(Gone, PerType[t]);
		BirdPerchXf[t].Init(Gone, PerType[t]);
		if (PerType[t] == 0) { continue; }
		// Volando: el pájaro de las bandadas, con el peso del aleteo en el alfa (M_ProcBird lo mueve).
		FTNProcMeshBuffers Fly;
		TNAmbientFX::BuildBird(Fly, Bodies[t], Wings[t]);
		for (int32 v = 0; v < Fly.Verts.Num(); ++v)
		{
			Fly.Colors[v].A = static_cast<float>(FMath::Clamp(FMath::Abs(Fly.Verts[v].Y) / 50.0, 0.0, 1.0));
		}
		if (UStaticMesh* FlyMesh = TNProcRuntimeMesh::MakeStaticMesh(this, Fly, BirdMat, false, 0.f, 1.f, -2.f))
		{
			GeneratedMeshes.Add(FlyMesh);
			if (UInstancedStaticMeshComponent* FlyComp = MakeInstanced(FlyMesh, false, false, 30000))
			{
				FlyComp->AddInstances(BirdFlyXf[t], false, false);
				TNArt::ApplyToInstances(FlyComp, t == 0 ? TN_ART("Lobby.Valley.Fauna.GullFlying") : TN_ART("Lobby.Valley.Fauna.PigeonFlying"));
				BirdFlyISM[t] = FlyComp;
			}
		}
		// Posado: la gaviota o la paloma de la fauna, con las alas plegadas.
		TArray<TNFauna::FTNFaunaPart> Parts;
		TNFauna::FTNFaunaRig Rig;
		TNFauna::TNFaunaBuildSpecies(PerchSpecies[t], Parts, Rig);
		FTNProcMeshBuffers Solid;
		FTNProcMeshBuffers Glow;
		MergeParts(Parts, Rig.BodyZ, Solid, Glow);
		if (UStaticMesh* PerchMesh = TNProcRuntimeMesh::MakeStaticMesh(this, Solid, SolidMat, false, 0.f, 1.f, 0.f))
		{
			GeneratedMeshes.Add(PerchMesh);
			if (UInstancedStaticMeshComponent* PerchComp = MakeInstanced(PerchMesh, false, false, 0))
			{
				PerchComp->AddInstances(BirdPerchXf[t], false, false);
				// La misma malla que la gaviota y la paloma de los sectores: la misma pieza de arte.
				TNArt::ApplyToInstances(PerchComp, TNValleyFauna::FaunaSlot(PerchSpecies[t]));
				BirdPerchISM[t] = PerchComp;
			}
		}
	}
}

void ATN_LobbyValley::FindPerches()
{
	Perches.Reset();
	UWorld* World = GetWorld();
	if (!World) { return; }
	const ATN_SandCastleLobby* Castle = nullptr;
	double Best = FMath::Square(8000.0);
	for (TActorIterator<ATN_SandCastleLobby> It(World); It; ++It)
	{
		const double DistSq = FVector::DistSquared2D(It->GetActorLocation(), GetActorLocation());
		if (DistSq < Best)
		{
			Best = DistSq;
			Castle = *It;
		}
	}
	if (!Castle) { return; }
	// Trazas desde arriba sobre la muralla y la azotea de la torre del homenaje: el primer suelo firme (almenas, adarve,
	// azoteas de las torres). Las barreras invisibles no bloquean la visibilidad.
	const FTransform CastleXf = Castle->GetActorTransform();
	const FTransform ValleyXf = GetActorTransform();
	FCollisionQueryParams Query(SCENE_QUERY_STAT(TN_ValleyPerch), false);
	auto Probe = [&](const FVector2D& L, double MinZ, double MaxZ)
	{
		const FVector From = CastleXf.TransformPosition(FVector(L, 2600.0));
		const FVector To = CastleXf.TransformPosition(FVector(L, 0.0));
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Query) || Hit.ImpactNormal.Z < 0.7) { return; }
		const double InCastleZ = CastleXf.InverseTransformPosition(Hit.ImpactPoint).Z;
		if (InCastleZ < MinZ || InCastleZ > MaxZ) { return; }
		const FVector Local = ValleyXf.InverseTransformPosition(Hit.ImpactPoint);
		for (const FVector& Other : Perches)
		{
			if (FVector::DistSquared(Other, Local) < FMath::Square(260.0)) { return; }
		}
		Perches.Add(Local);
	};
	const double WallMid = ATN_SandCastleLobby::Radius + 95.0;
	for (int32 k = 0; k < 64; ++k)
	{
		const double A = TNProcMap::TwoPi * k / 64.0;
		Probe(FVector2D(-WallMid * FMath::Sin(A), WallMid * FMath::Cos(A)), 450.0, 1700.0);
	}
	for (int32 k = 0; k < 12; ++k)
	{
		const double A = TNProcMap::TwoPi * k / 12.0;
		Probe(FVector2D(-415.0 * FMath::Sin(A), ATN_SandCastleLobby::CutY + 415.0 * FMath::Cos(A)), 850.0, 1500.0);
	}
}

void ATN_LobbyValley::StartBirdFlight(FCastleBird& B, int32 ToPerch, bool bDetour)
{
	const FVector Start = B.bFlying ? B.To : Perches[B.Perch];
	B.Target = ToPerch;
	B.bFlying = true;
	B.bVia = bDetour;
	B.From = Start;
	if (bDetour)
	{
		// Rodeo por encima de la plaza antes de posarse.
		const float Ang = RandIn(0.f, UE_TWO_PI);
		const float Dist = RandIn(300.f, 1700.f);
		B.Via = FVector(FMath::Cos(Ang) * Dist, 900.f + FMath::Sin(Ang) * Dist * 0.6f, RandIn(1300.f, 2300.f));
		B.To = B.Via;
	}
	else
	{
		B.To = Perches[ToPerch];
	}
	const double Len = FVector::Dist(B.From, B.To);
	B.Ctrl = (B.From + B.To) * 0.5 + FVector(RandIn(-300.f, 300.f), RandIn(-300.f, 300.f), 250.0 + Len * 0.3);
	B.Dur = FMath::Max(1.2f, static_cast<float>(Len / 750.0) + 0.6f);
	B.T = 0.f;
}

void ATN_LobbyValley::TickCastleBirds(float Dt)
{
	using namespace TNValleyFauna;
	if (CastleBirdList.Num() == 0) { return; }
	if (!bPerchesReady)
	{
		// Las almenas se buscan un poco después de empezar (con la colisión del castillo ya hecha).
		PerchRetry -= Dt;
		if (PerchRetry > 0.f || PerchTries >= 6) { return; }
		++PerchTries;
		PerchRetry = 2.f;
		FindPerches();
		if (Perches.Num() < 4) { return; }
		bPerchesReady = true;
		for (FCastleBird& B : CastleBirdList)
		{
			B.Perch = FMath::Clamp(static_cast<int32>(RandUnit() * Perches.Num()), 0, Perches.Num() - 1);
			B.Yaw = RandIn(-180.f, 180.f);
			B.YawGoal = B.Yaw;
			B.Wait = RandIn(0.5f, 9.f);
			B.bFlying = false;
		}
	}

	// Las tortugas de esta máquina que se acercan espantan a los posados.
	TArray<FVector, TInlineAllocator<4>> Walkers;
	if (UWorld* World = GetWorld())
	{
		const FTransform ValleyXf = GetActorTransform();
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* PC = It->Get();
			const APawn* Walker = PC && PC->IsLocalController() ? PC->GetPawn() : nullptr;
			if (Walker) { Walkers.Add(ValleyXf.InverseTransformPosition(Walker->GetActorLocation())); }
		}
	}

	for (FCastleBird& B : CastleBirdList)
	{
		FTransform& FlyXf = BirdFlyXf[B.Type][B.Slot];
		FTransform& PerchXf = BirdPerchXf[B.Type][B.Slot];
		const float FlyScale = B.Type == 0 ? 0.95f : 0.62f;
		const float PerchScale = B.Type == 0 ? 1.35f : 1.2f;
		if (!B.bFlying)
		{
			B.Wait -= Dt;
			B.LookT -= Dt;
			if (B.LookT <= 0.f)
			{
				B.YawGoal = B.Yaw + RandIn(-70.f, 70.f);
				B.LookT = RandIn(0.8f, 2.4f);
			}
			B.Yaw = TurnToward(B.Yaw, B.YawGoal, 360.f * Dt);
			const FVector& Spot = Perches[B.Perch];
			for (const FVector& WalkerPos : Walkers)
			{
				if (FVector::DistSquared(WalkerPos, Spot) < FMath::Square(450.0)) { B.Wait = FMath::Min(B.Wait, 0.1f); }
			}
			if (B.Wait > 0.f)
			{
				PerchXf = FTransform(FRotator(0.f, B.Yaw, 0.f), Spot, FVector(PerchScale));
				FlyXf.SetScale3D(FVector::ZeroVector);
				continue;
			}
			// A otra almena (ni muy cerca ni muy lejos) y, a veces, con un rodeo por la plaza.
			int32 Next = B.Perch;
			for (int32 Try = 0; Try < 8; ++Try)
			{
				const int32 Candidate = FMath::Clamp(static_cast<int32>(RandUnit() * Perches.Num()), 0, Perches.Num() - 1);
				const double DistSq = FVector::DistSquared(Perches[Candidate], Spot);
				if (Candidate != B.Perch && DistSq > FMath::Square(600.0) && DistSq < FMath::Square(4000.0))
				{
					Next = Candidate;
					break;
				}
			}
			if (Next == B.Perch) { Next = (B.Perch + 1 + static_cast<int32>(RandUnit() * (Perches.Num() - 1))) % Perches.Num(); }
			StartBirdFlight(B, Next, RandUnit() < 0.35f);
		}

		B.T += Dt / B.Dur;
		if (B.T >= 1.f)
		{
			if (B.bVia)
			{
				// Del rodeo, a su almena.
				StartBirdFlight(B, B.Target, false);
			}
			else
			{
				B.bFlying = false;
				B.Perch = B.Target;
				B.Wait = RandIn(4.f, 14.f);
				B.YawGoal = B.Yaw;
				B.Roll = 0.f;
				PerchXf = FTransform(FRotator(0.f, B.Yaw, 0.f), Perches[B.Perch], FVector(PerchScale));
				FlyXf.SetScale3D(FVector::ZeroVector);
				continue;
			}
		}
		const float T = FMath::Clamp(B.T, 0.f, 1.f);
		const FVector Pos = Bezier(B.From, B.Ctrl, B.To, T);
		const FVector Tangent = BezierTangent(B.From, B.Ctrl, B.To, T);
		const float NewYaw = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Tangent.Y), static_cast<float>(Tangent.X)));
		// Se inclina hacia dentro de la curva.
		const float YawRate = FMath::FindDeltaAngleDegrees(B.Yaw, NewYaw) / FMath::Max(Dt, 1.0e-3f);
		B.Roll = FMath::FInterpTo(B.Roll, FMath::Clamp(YawRate * 0.25f, -35.f, 35.f), Dt, 4.f);
		B.Yaw = NewYaw;
		const float Pitch = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Tangent.Z), static_cast<float>(Tangent.Size2D()))) * 0.7f, -30.f, 30.f);
		FlyXf = FTransform(FRotator(Pitch, B.Yaw, B.Roll), Pos, FVector(FlyScale));
		PerchXf.SetScale3D(FVector::ZeroVector);
	}

	// Estos van de punta a punta del castillo. Sin MarkRenderStateDirty: el cambio de transformada ya rehace los límites del
	// ISM al final del fotograma (SendRenderInstanceData_Concurrent) sin recrear el proxy en cada fotograma (#566).
	for (int32 t = 0; t < 2; ++t)
	{
		if (UInstancedStaticMeshComponent* FlyComp = BirdFlyISM[t].Get()) { TNArt::UpdateInstances(FlyComp, 0, BirdFlyXf[t], false, false, false); }
		if (UInstancedStaticMeshComponent* PerchComp = BirdPerchISM[t].Get()) { TNArt::UpdateInstances(PerchComp, 0, BirdPerchXf[t], false, false, false); }
	}
}
