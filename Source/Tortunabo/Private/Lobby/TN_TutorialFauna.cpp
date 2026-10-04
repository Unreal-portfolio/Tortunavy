// ─────────────────────────────────────────────────────────────────────────────
// ATN_TutorialFauna — fauna ambiental del tutorial: los animales del mapa procedural (TN_ProcMapFaunaMeshes.h) en sitios
// fijos junto al pasillo de cada bioma, con reposo, paseítos y huida. Versión corta de ATN_ProcFauna (que depende del
// generador del mapa); solo visual y local.
// ─────────────────────────────────────────────────────────────────────────────

#include "Lobby/TN_TutorialFauna.h"
#include "Core/TN_ProjectMaterials.h"
#include "Lobby/TN_TutorialCourse.h"
#include "Art/TN_Art.h"
#include "TN_TutorialLayout.h"
#include "../World/ProcMap/TN_ProcMapFaunaMeshes.h"
#include "../World/ProcMap/TN_ProcMapRuntimeMesh.h"
#include "Core/TN_Log.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"

namespace TNTutorialFaunaDetail
{
	using namespace TNFauna;

	/** Solo se animan (y se ven) los que están a menos de esto de la cámara local (cm). */
	constexpr float WakeRadius = 6000.f;
	/** Como en ATN_ProcFauna: dejan acercarse más que su ficha y se les ve correr antes de desaparecer. */
	constexpr float AlarmScale = 0.5f;
	constexpr float FleeScale = 0.6f;
	/** Tiempo escondidos antes de volver a su sitio (s). */
	constexpr float HideMin = 6.f;
	constexpr float HideMax = 12.f;
	/** Estados (FAnimal::State). */
	constexpr uint8 StateIdle = 0;
	constexpr uint8 StateWalk = 1;
	constexpr uint8 StateFlee = 2;
	constexpr uint8 StateHidden = 3;
	constexpr int32 BoneCount = static_cast<int32>(ETNFaunaBone::Count);

	/** Especies que caben en el tutorial: las de tierra y orilla que andan (sin agua honda, sin murciélagos). */
	bool IsUsable(ETNFaunaSpecies Species)
	{
		const FTNFaunaSpec& Sp = TNFaunaSpec(Species);
		return (Sp.Habitat == ETNFaunaHabitat::Land || Sp.Habitat == ETNFaunaHabitat::Shore) && Sp.Gait != ETNFaunaGait::Swim
			&& Sp.Gait != ETNFaunaGait::Hover;
	}

	/**
	 * Pieza de arte de cada animal que puede salir en el tutorial (Docs/Arte_Assets.md): el animal entero, que va en la pieza
	 * del cuerpo; con sustituto, las demás piezas (cabeza, patas, alas, cola) no se ponen.
	 */
	FName FaunaSlot(ETNFaunaSpecies Species)
	{
		using SP = ETNFaunaSpecies;
		switch (Species)
		{
			case SP::Monkey:      return TN_ART("Lobby.Tutorial.Fauna.Monkey");
			case SP::Toucan:      return TN_ART("Lobby.Tutorial.Fauna.Toucan");
			case SP::DartFrog:    return TN_ART("Lobby.Tutorial.Fauna.DartFrog");
			case SP::Capybara:    return TN_ART("Lobby.Tutorial.Fauna.Capybara");
			case SP::Crab:        return TN_ART("Lobby.Tutorial.Fauna.Crab");
			case SP::BabyTurtle:  return TN_ART("Lobby.Tutorial.Fauna.BabyTurtle");
			case SP::Gull:        return TN_ART("Lobby.Tutorial.Fauna.Gull");
			case SP::Sandpiper:   return TN_ART("Lobby.Tutorial.Fauna.Sandpiper");
			case SP::Lizard:      return TN_ART("Lobby.Tutorial.Fauna.Lizard");
			case SP::Meerkat:     return TN_ART("Lobby.Tutorial.Fauna.Meerkat");
			case SP::Roadrunner:  return TN_ART("Lobby.Tutorial.Fauna.Roadrunner");
			case SP::Vulture:     return TN_ART("Lobby.Tutorial.Fauna.Vulture");
			case SP::Ibex:        return TN_ART("Lobby.Tutorial.Fauna.Ibex");
			case SP::Marmot:      return TN_ART("Lobby.Tutorial.Fauna.Marmot");
			case SP::Eagle:       return TN_ART("Lobby.Tutorial.Fauna.Eagle");
			case SP::FiddlerCrab: return TN_ART("Lobby.Tutorial.Fauna.FiddlerCrab");
			case SP::TreeFrog:    return TN_ART("Lobby.Tutorial.Fauna.TreeFrog");
			case SP::Mudskipper:  return TN_ART("Lobby.Tutorial.Fauna.Mudskipper");
			case SP::Cat:         return TN_ART("Lobby.Tutorial.Fauna.Cat");
			case SP::Pigeon:      return TN_ART("Lobby.Tutorial.Fauna.Pigeon");
			case SP::Hen:         return TN_ART("Lobby.Tutorial.Fauna.Hen");
			case SP::Rabbit:      return TN_ART("Lobby.Tutorial.Fauna.Rabbit");
			default:              return NAME_None;
		}
	}

	/** Una acción de reposo al azar de las de su ficha (Look si no tiene). */
	uint8 PickAct(const FTNFaunaSpec& Sp, float Unit)
	{
		TArray<uint8, TInlineAllocator<16>> Acts;
		for (uint8 a = 1; a < static_cast<uint8>(ETNFaunaAct::Count); ++a)
		{
			if (Sp.Acts & TNFaunaActBit(static_cast<ETNFaunaAct>(a))) { Acts.Add(a); }
		}
		if (Acts.Num() == 0) { return static_cast<uint8>(ETNFaunaAct::Look); }
		return Acts[FMath::Clamp(static_cast<int32>(Unit * Acts.Num()), 0, Acts.Num() - 1)];
	}
}

ATN_TutorialFauna::ATN_TutorialFauna()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = false;
	SetCanBeDamaged(false);
	SetActorEnableCollision(false);
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
}

float ATN_TutorialFauna::RandUnit()
{
	Rng ^= Rng << 13;
	Rng ^= Rng >> 17;
	Rng ^= Rng << 5;
	return static_cast<float>(Rng & 0xFFFFFF) / static_cast<float>(0x1000000);
}

float ATN_TutorialFauna::GroundAt(const FVector& WorldPoint) const
{
	const ATN_TutorialCourse* CourseActor = Course.Get();
	if (!CourseActor)
	{
		return static_cast<float>(WorldPoint.Z);
	}
	const FVector Local = CourseActor->WorldToLocal(WorldPoint);
	return static_cast<float>(CourseActor->LocalToWorld(FVector(Local.X, Local.Y, TNTutorial::SurfaceZ(Local.X, Local.Y))).Z);
}

void ATN_TutorialFauna::Init(const ATN_TutorialCourse* InCourse)
{
	using namespace TNTutorialFaunaDetail;
	using namespace TNTutorial;
	Course = InCourse;
	if (!InCourse || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// ── Sitios: cada 8,5 m del pasillo, un grupito de un animal de su bioma, en la meseta o (algunos) en el camino ──
	struct FSite
	{
		uint8 Species = 0;
		FVector Local = FVector::ZeroVector;
	};
	TArray<FSite> Sites;
	TNProcMap::FRng R(static_cast<uint64>(Seed) * 31ull + 7ull);
	for (double X = -100.0; X < Dims::PartB1 - 300.0; X += 850.0)
	{
		if (PartOf(X) == INDEX_NONE)
		{
			continue;
		}
		const FTNFaunaBiomeTable Table = TNFaunaBiomeTableOf(DominantBiome(X));
		float Total = 0.f;
		for (int32 e = 0; e < Table.Num; ++e)
		{
			if (IsUsable(Table.Entries[e].Species)) { Total += Table.Entries[e].Weight; }
		}
		if (Total <= 0.f)
		{
			continue;
		}
		float Pick = static_cast<float>(R.Unit()) * Total;
		ETNFaunaSpecies Species = Table.Entries[0].Species;
		for (int32 e = 0; e < Table.Num; ++e)
		{
			if (!IsUsable(Table.Entries[e].Species)) { continue; }
			Species = Table.Entries[e].Species;
			Pick -= Table.Entries[e].Weight;
			if (Pick <= 0.f) { break; }
		}
		const FTNFaunaSpec& Sp = TNFaunaSpec(Species);
		const int32 Group = R.RangeInt(FMath::Max(1, Sp.GroupMin), FMath::Clamp(Sp.GroupMax, 1, 4));
		const double Side = R.Chance(0.5) ? 1.0 : -1.0;
		const bool bOnPath = R.Unit() < Sp.OnPath * 0.6;
		const double W = CorridorHalfWidth(X);
		const double BW = BankWidth(X);
		const double BaseD = bOnPath ? Side * (W - R.Range(60.0, 170.0)) : Side * (W + BW + R.Range(120.0, 480.0));
		for (int32 g = 0; g < Group; ++g)
		{
			const double Xg = X + R.Range(-0.6, 0.6) * Sp.GroupSpread;
			double Dg = BaseD + R.Range(-0.4, 0.4) * Sp.GroupSpread;
			const double Wg = CorridorHalfWidth(Xg);
			const double BWg = BankWidth(Xg);
			Dg = bOnPath ? FMath::Clamp(Dg, -(Wg - 40.0), Wg - 40.0) : Side * FMath::Max(FMath::Abs(Dg), Wg + BWg + 60.0);
			const double Yg = CorridorCenter(Xg) + Dg;
			if (PartOf(Xg) == INDEX_NONE || Yg > RimRight(Xg) - 150.0 || Yg < -RimLeft(Xg) + 150.0)
			{
				continue;
			}
			// En el camino, nunca en la zanja, la laguna ni el arroyo.
			if (bOnPath && ((Xg > Dims::TrenchX0 - 200.0 && Xg < Dims::TrenchX1 + 200.0) || (Xg > Dims::PoolX0 - 200.0 && Xg < Dims::PoolX1 + 200.0)
				|| Xg > Dims::StreamX0 - 200.0))
			{
				continue;
			}
			FSite& Site = Sites.AddDefaulted_GetRef();
			Site.Species = static_cast<uint8>(Species);
			Site.Local = FVector(Xg, Yg, SurfaceZ(Xg, Yg));
		}
	}

	// ── Animales ──
	int32 KindOf[TNFaunaNumSpecies];
	for (int32& K : KindOf) { K = INDEX_NONE; }
	for (const FSite& Site : Sites)
	{
		const FTNFaunaSpec& Sp = TNFaunaSpec(static_cast<ETNFaunaSpecies>(Site.Species));
		FAnimal& A = Animals.AddDefaulted_GetRef();
		A.Species = Site.Species;
		A.Home = InCourse->LocalToWorld(Site.Local);
		A.Pos = A.Home;
		A.Goal = A.Home;
		A.Yaw = static_cast<float>(R.Range(0.0, 360.0));
		A.Size = static_cast<float>(R.Range(Sp.ScaleMin, Sp.ScaleMax));
		A.Clock = static_cast<float>(R.Range(0.0, 10.0));
		A.Dur = static_cast<float>(R.Range(1.5, 4.0));
		A.Act = PickAct(Sp, static_cast<float>(R.Unit()));
		A.Presence = 1.f;
		int32& KindIdx = KindOf[Site.Species];
		if (KindIdx == INDEX_NONE)
		{
			KindIdx = Kinds.Num();
			Kinds.AddDefaulted_GetRef().Species = Site.Species;
		}
		A.Kind = KindIdx;
		A.Slot = Kinds[KindIdx].Members.Num();
		Kinds[KindIdx].Members.Add(Animals.Num() - 1);
	}

	// ── Piezas: una ISM por pieza de cada especie, con las transformadas en el mundo ──
	UMaterialInterface* Solid = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_ProcFoliage.M_ProcFoliage"), nullptr, LOAD_NoWarn);
	if (!Solid)
	{
		Solid = TNMaterials::VertexColor();
	}
	UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_ProcGlow.M_ProcGlow"), nullptr, LOAD_NoWarn);
	if (!Glow)
	{
		Glow = Solid;
	}
	for (FKind& K : Kinds)
	{
		TArray<FTNFaunaPart> Parts;
		FTNFaunaRig Rig;
		TNFaunaBuildSpecies(static_cast<ETNFaunaSpecies>(K.Species), Parts, Rig);
		K.BodyZ = static_cast<float>(Rig.BodyZ);
		K.Height = static_cast<float>(Rig.Height);
		K.FirstPart = PartISMs.Num();
		// Pieza de arte del animal entero: en la primera pieza del cuerpo; con sustituto, las demás piezas no se ponen.
		const FName Slot = TNTutorialFaunaDetail::FaunaSlot(static_cast<ETNFaunaSpecies>(K.Species));
		const bool bArt = TNArt::Find(Slot) != nullptr;
		bool bBodyDone = false;
		for (FTNFaunaPart& Part : Parts)
		{
			const bool bBody = !bBodyDone && Part.Bone == ETNFaunaBone::Body && !Part.bGlow;
			if (bArt && !bBody)
			{
				continue;
			}
			UStaticMesh* Mesh = TNProcRuntimeMesh::MakeStaticMesh(this, Part.Mesh, Part.bGlow ? Glow : Solid, false, 0.f, 1.f, 0.f);
			if (!Mesh)
			{
				continue;
			}
			TArray<FTransform>& Xf = PartXf.AddDefaulted_GetRef();
			for (const int32 Member : K.Members)
			{
				Xf.Add(FTransform(FQuat::Identity, Animals[Member].Pos, FVector::ZeroVector));
			}
			UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
			ISM->SetStaticMesh(Mesh);
			ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			ISM->SetCanEverAffectNavigation(false);
			ISM->SetGenerateOverlapEvents(false);
			ISM->SetCastShadow(Part.bShadow);
			ISM->bEvaluateWorldPositionOffset = false;
			ISM->SetMobility(EComponentMobility::Movable);
			ISM->SetupAttachment(SceneRoot);
			ISM->RegisterComponent();
			// Transformadas de las instancias en el mundo: el componente se queda en el origen.
			ISM->SetAbsolute(true, true, true);
			ISM->SetWorldTransform(FTransform::Identity);
			ISM->SetCullDistances(FMath::RoundToInt(WakeRadius), FMath::RoundToInt(WakeRadius + 1000.f));
			ISM->AddInstances(Xf, false, false);
			if (bBody)
			{
				TNArt::ApplyToInstances(ISM, Slot);
				bBodyDone = true;
			}
			PartISMs.Add(ISM);
			PartMeshes.Add(Mesh);
			PartBone.Add(static_cast<uint8>(Part.Bone));
			PartPivot.Add(Part.Pivot);
		}
		K.NumParts = PartISMs.Num() - K.FirstPart;
	}
	for (FAnimal& A : Animals)
	{
		A.bShown = true;
		WriteHidden(A);
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Tutorial] Fauna: %d animales de %d especies."), Animals.Num(), Kinds.Num());
}

void ATN_TutorialFauna::SetFaunaVisible(bool bInVisible)
{
	if (bVisible == bInVisible)
	{
		return;
	}
	bVisible = bInVisible;
	for (UInstancedStaticMeshComponent* ISM : PartISMs)
	{
		if (ISM)
		{
			ISM->SetVisibility(bInVisible);
		}
	}
}

void ATN_TutorialFauna::Tick(float DeltaSeconds)
{
	using namespace TNTutorialFaunaDetail;
	Super::Tick(DeltaSeconds);
	UWorld* World = GetWorld();
	if (!bVisible || !World || Animals.Num() == 0)
	{
		return;
	}
	const float Dt = FMath::Min(DeltaSeconds, 0.1f);

	// La cámara local (para despertar a los de cerca) y las tortugas (las que asustan).
	bool bHasView = false;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (PC && PC->IsLocalController() && PC->PlayerCameraManager)
		{
			ViewLoc = PC->PlayerCameraManager->GetCameraLocation();
			bHasView = true;
			break;
		}
	}
	if (!bHasView)
	{
		return;
	}
	Threats.Reset();
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		if (FVector::DistSquared(It->GetActorLocation(), ViewLoc) < FMath::Square(WakeRadius + 2000.f))
		{
			Threats.Add(It->GetActorLocation());
		}
	}

	const float WakeSq = FMath::Square(WakeRadius);
	for (FAnimal& A : Animals)
	{
		const bool bNear = FVector::DistSquared(A.Pos, ViewLoc) < WakeSq;
		if (!bNear)
		{
			if (A.bShown)
			{
				WriteHidden(A);
			}
			continue;
		}
		Simulate(A, Dt);
		if (A.State == StateHidden || A.Presence <= 0.01f)
		{
			if (A.bShown)
			{
				WriteHidden(A);
			}
			continue;
		}
		Write(A);
	}

	for (FKind& K : Kinds)
	{
		if (K.DirtyMax < K.DirtyMin)
		{
			continue;
		}
		const int32 First = K.DirtyMin;
		const int32 Count = K.DirtyMax - K.DirtyMin + 1;
		for (int32 p = 0; p < K.NumParts; ++p)
		{
			const int32 Part = K.FirstPart + p;
			if (UInstancedStaticMeshComponent* ISM = PartISMs[Part])
			{
				// El tramo que ha cambiado (con el ajuste de la malla de arte, si la pieza la tiene).
				const TArrayView<const FTransform> Range(PartXf[Part].GetData() + First, Count);
				TNArt::UpdateInstances(ISM, First, Range, false, false, false);
			}
		}
		K.DirtyMin = MAX_int32;
		K.DirtyMax = -1;
	}
}

void ATN_TutorialFauna::Simulate(FAnimal& A, float Dt)
{
	using namespace TNTutorialFaunaDetail;
	const FTNFaunaSpec& Sp = TNFaunaSpec(static_cast<ETNFaunaSpecies>(A.Species));
	A.Clock += Dt;
	A.StateT += Dt;

	// La tortuga más cerca.
	float ThreatSq = TNumericLimits<float>::Max();
	FVector ThreatLoc = FVector::ZeroVector;
	for (const FVector& T : Threats)
	{
		const float D = static_cast<float>(FVector::DistSquared2D(T, A.Pos));
		if (D < ThreatSq && FMath::Abs(T.Z - A.Pos.Z) < 400.0)
		{
			ThreatSq = D;
			ThreatLoc = T;
		}
	}
	const float Alarm = Sp.AlarmRange * AlarmScale;

	// Cabeza: mira a un lado y a otro a golpes.
	if (FMath::Abs(A.Look - A.LookGoal) < 2.f && RandUnit() < Dt * 0.6f)
	{
		A.LookGoal = (RandUnit() * 2.f - 1.f) * 55.f;
	}
	A.Look = FMath::FInterpTo(A.Look, A.LookGoal, Dt, 9.f);

	switch (A.State)
	{
		case StateIdle:
		case StateWalk:
		{
			if (A.Presence < 1.f)
			{
				A.Presence = FMath::Min(1.f, A.Presence + Dt / 0.6f);
			}
			if (ThreatSq < Alarm * Alarm)
			{
				A.State = StateFlee;
				A.StateT = 0.f;
				FVector Away = (A.Pos - ThreatLoc).GetSafeNormal2D();
				if (Away.IsNearlyZero())
				{
					Away = FRotator(0.f, RandUnit() * 360.f, 0.f).Vector();
				}
				A.FleeDir = Away;
				A.Yaw = static_cast<float>(Away.Rotation().Yaw);
				break;
			}
			if (A.State == StateIdle)
			{
				if (A.StateT > A.Dur)
				{
					A.StateT = 0.f;
					if (RandUnit() < 0.45f)
					{
						// Paseíto alrededor de su sitio.
						const float Ang = RandUnit() * 360.f;
						const float Dist = RandUnit() * Sp.WanderRadius * 0.4f;
						A.Goal = A.Home + FRotator(0.f, Ang, 0.f).Vector() * Dist;
						A.Goal.Z = GroundAt(A.Goal);
						A.State = StateWalk;
						A.Dur = 6.f;
					}
					else
					{
						A.Act = PickAct(Sp, RandUnit());
						A.Dur = 2.f + RandUnit() * 3.f;
					}
				}
				break;
			}
			// Paseo.
			const FVector To = A.Goal - A.Pos;
			const float Dist = static_cast<float>(To.Size2D());
			if (Dist < 20.f || A.StateT > A.Dur)
			{
				A.State = StateIdle;
				A.StateT = 0.f;
				A.Dur = 1.5f + RandUnit() * 3.f;
				break;
			}
			const float TargetYaw = static_cast<float>(To.Rotation().Yaw);
			A.Yaw = FMath::FixedTurn(A.Yaw, TargetYaw, Sp.TurnRate * Dt);
			const float Step = FMath::Min(Dist, Sp.WalkSpeed * Dt);
			const FVector Next = A.Pos + FRotator(0.f, A.Yaw, 0.f).Vector() * Step;
			const float Ground = GroundAt(Next);
			if (FMath::Abs(Ground - static_cast<float>(A.Pos.Z)) < 60.f)
			{
				A.Pos = FVector(Next.X, Next.Y, Ground);
			}
			else
			{
				A.Goal = A.Pos;
			}
			A.Gait += Step / FMath::Max(1.f, Sp.Stride);
			break;
		}
		case StateFlee:
		{
			const float Speed = Sp.FleeSpeed * FleeScale;
			if (Sp.Flee == ETNFaunaFlee::Fly)
			{
				// Echa a volar hacia fuera y hacia arriba, y se pierde.
				A.Open = FMath::Min(1.f, A.Open + Dt * 6.f);
				A.FlapPh += Sp.FlapHz * Dt;
				A.Air += (250.f + 300.f * A.StateT) * Dt;
				A.Pos += A.FleeDir * (Speed * 0.5f * Dt);
				A.Presence = 1.f - FMath::SmoothStep(1.4f, 2.4f, A.StateT);
			}
			else if (Sp.Flee == ETNFaunaFlee::Burrow)
			{
				// Carrerita y se mete en la tierra temblando.
				if (A.StateT < Sp.Dash)
				{
					const FVector Next = A.Pos + A.FleeDir * (Speed * 0.5f * Dt);
					const float Ground = GroundAt(Next);
					if (FMath::Abs(Ground - static_cast<float>(A.Pos.Z)) < 60.f) { A.Pos = FVector(Next.X, Next.Y, Ground); }
					A.Gait += Speed * 0.5f * Dt / FMath::Max(1.f, Sp.Stride);
				}
				A.Presence = 1.f - FMath::SmoothStep(Sp.Dash, Sp.Dash + 0.6f, A.StateT);
			}
			else
			{
				// Sale corriendo (o trepa, o busca agua): se aleja y desaparece.
				const FVector Next = A.Pos + A.FleeDir * (Speed * Dt);
				const float Ground = GroundAt(Next);
				if (FMath::Abs(Ground - static_cast<float>(A.Pos.Z)) < 120.f) { A.Pos = FVector(Next.X, Next.Y, Ground); }
				A.Gait += Speed * Dt / FMath::Max(1.f, Sp.Stride);
				A.Presence = 1.f - FMath::SmoothStep(0.8f, 1.4f, A.StateT);
			}
			if (A.Presence <= 0.01f)
			{
				A.State = StateHidden;
				A.StateT = 0.f;
				A.Dur = HideMin + RandUnit() * (HideMax - HideMin);
			}
			break;
		}
		case StateHidden:
		default:
		{
			// Vuelve a su sitio cuando no hay nadie cerca.
			if (A.StateT > A.Dur && ThreatSq > FMath::Square(Alarm * 2.f))
			{
				A.Pos = A.Home;
				A.Goal = A.Home;
				A.Air = 0.f;
				A.Open = 0.f;
				A.Presence = 0.02f;
				A.State = StateIdle;
				A.StateT = 0.f;
				A.Dur = 2.f;
			}
			break;
		}
	}
}

void ATN_TutorialFauna::WriteHidden(FAnimal& A)
{
	if (!Kinds.IsValidIndex(A.Kind))
	{
		return;
	}
	FKind& K = Kinds[A.Kind];
	const FTransform Gone(FQuat::Identity, A.Pos, FVector::ZeroVector);
	for (int32 p = 0; p < K.NumParts; ++p)
	{
		PartXf[K.FirstPart + p][A.Slot] = Gone;
	}
	A.bShown = false;
	K.DirtyMin = FMath::Min(K.DirtyMin, A.Slot);
	K.DirtyMax = FMath::Max(K.DirtyMax, A.Slot);
}

void ATN_TutorialFauna::Write(FAnimal& A)
{
	using namespace TNTutorialFaunaDetail;
	if (!Kinds.IsValidIndex(A.Kind))
	{
		return;
	}
	FKind& K = Kinds[A.Kind];
	FTransform Bones[BoneCount];
	PoseBones(A, Bones);
	const float Grow = A.Size * FMath::SmoothStep(0.f, 1.f, A.Presence);
	const FTransform Root(FRotator(0.f, A.Yaw, 0.f), A.Pos + FVector(0.0, 0.0, A.Air), FVector(Grow));
	const FTransform& BodyBone = Bones[static_cast<int32>(ETNFaunaBone::Body)];
	const FTransform BodyFrame = FTransform(BodyBone.GetRotation(), FVector(0.0, 0.0, K.BodyZ) + BodyBone.GetTranslation()) * Root;
	for (int32 p = 0; p < K.NumParts; ++p)
	{
		const int32 Part = K.FirstPart + p;
		const int32 BoneIdx = FMath::Clamp(static_cast<int32>(PartBone[Part]), 0, BoneCount - 1);
		FTransform& Dst = PartXf[Part][A.Slot];
		if (BoneIdx == static_cast<int32>(ETNFaunaBone::Body))
		{
			Dst = FTransform(FQuat::Identity, FVector::ZeroVector, BodyBone.GetScale3D()) * BodyFrame;
		}
		else
		{
			const FTransform& Local = Bones[BoneIdx];
			Dst = FTransform(Local.GetRotation(), PartPivot[Part] + Local.GetTranslation(), Local.GetScale3D()) * BodyFrame;
		}
	}
	A.bShown = true;
	K.DirtyMin = FMath::Min(K.DirtyMin, A.Slot);
	K.DirtyMax = FMath::Max(K.DirtyMax, A.Slot);
}

void ATN_TutorialFauna::PoseBones(const FAnimal& A, FTransform* OutBones) const
{
	using namespace TNTutorialFaunaDetail;
	for (int32 b = 0; b < BoneCount; ++b) { OutBones[b] = FTransform::Identity; }
	const FTNFaunaSpec& Sp = TNFaunaSpec(static_cast<ETNFaunaSpecies>(A.Species));
	const bool bMoving = A.State == StateWalk || (A.State == StateFlee && Sp.Flee != ETNFaunaFlee::Fly);
	const bool bAirborne = A.State == StateFlee && Sp.Flee == ETNFaunaFlee::Fly && A.Air > 5.f;
	const bool bIdle = A.State == StateIdle;
	const float Move = bMoving ? 1.f : 0.f;
	const float Ph = A.Gait * UE_TWO_PI;
	const float Tm = A.Clock;
	const ETNFaunaAct Act = static_cast<ETNFaunaAct>(A.Act);
	// La acción de reposo va y viene (entra y sale suave dentro de su duración).
	const float ActW = bIdle ? FMath::SmoothStep(0.f, 0.3f, A.StateT) * (1.f - FMath::SmoothStep(A.Dur - 0.3f, A.Dur, A.StateT)) : 0.f;
	auto Set = [OutBones](ETNFaunaBone Bone, const FRotator& Rot)
	{
		OutBones[static_cast<int32>(Bone)] = FTransform(Rot);
	};

	FRotator BodyRot = FRotator::ZeroRotator;
	FVector BodyOff(0.0, 0.0, FMath::Abs(FMath::Sin(Ph)) * 1.5f * Move);
	if (A.State == StateFlee && Sp.Flee == ETNFaunaFlee::Burrow && A.StateT > Sp.Dash)
	{
		// Se entierra temblando.
		BodyRot.Roll = FMath::Sin(Tm * 44.f) * 12.f;
		BodyOff.Z -= (A.StateT - Sp.Dash) * 25.f;
	}

	// Cabeza: mira, picotea o pasta.
	float HeadPitch = bAirborne ? 12.f : FMath::Sin(Ph * 2.f) * 6.f * Move;
	if (Act == ETNFaunaAct::Peck || Act == ETNFaunaAct::Graze)
	{
		const float Dip = Sp.bLongNeck ? FMath::Clamp(FMath::Sin(A.StateT * 1.6f) * 1.6f, 0.f, 1.f) : FMath::Max(0.f, FMath::Sin(A.StateT * 7.f));
		HeadPitch -= ActW * (Sp.bLongNeck ? 100.f : 55.f) * Dip;
	}
	if (A.State == StateFlee && A.StateT < 0.3f)
	{
		HeadPitch += 10.f;
	}
	Set(ETNFaunaBone::Head, FRotator(HeadPitch, A.Look, 0.f));

	switch (Sp.Body)
	{
		case ETNFaunaBody::Bird:
		{
			BodyRot.Roll += FMath::Sin(Ph) * 5.f * Move;
			for (const float Side : { -1.f, 1.f })
			{
				float LegPitch = FMath::Sin(Ph + (Side > 0.f ? UE_PI : 0.f)) * 40.f * Move;
				FVector LegScale = FVector::OneVector;
				if (bAirborne)
				{
					LegPitch = -70.f;
					LegScale.Z = 0.6f;
				}
				else if (Act == ETNFaunaAct::OneLeg && Side > 0.f)
				{
					LegPitch = FMath::Lerp(LegPitch, -15.f, ActW);
					LegScale.Z = FMath::Lerp(1.f, 0.35f, ActW);
				}
				OutBones[static_cast<int32>(Side < 0.f ? ETNFaunaBone::LegBL : ETNFaunaBone::LegBR)] = FTransform(FRotator(LegPitch, 0.f, 0.f), FVector::ZeroVector, LegScale);
			}
			// Alas plegadas contra el costado o abiertas aleteando (como ATN_ProcFauna).
			float Open = A.Open;
			float Flap = FMath::Sin(A.FlapPh * UE_TWO_PI) * 42.f + 8.f;
			if (Act == ETNFaunaAct::Stretch && bIdle)
			{
				Open = FMath::Max(Open, ActW);
				Flap = 22.f + FMath::Sin(A.StateT * 3.f) * 14.f;
			}
			const FVector FoldScale(0.8f, 0.5f, 1.f);
			for (const float Side : { -1.f, 1.f })
			{
				const FQuat Fold = FQuat(FVector::ForwardVector, FMath::DegreesToRadians(100.f * Side)) * FQuat(FVector::UpVector, FMath::DegreesToRadians(90.f * Side));
				const FQuat Spread(FVector::ForwardVector, FMath::DegreesToRadians(Flap * Side));
				OutBones[static_cast<int32>(Side < 0.f ? ETNFaunaBone::WingL : ETNFaunaBone::WingR)] =
					FTransform(FQuat::Slerp(Fold, Spread, Open), FVector::ZeroVector, FMath::Lerp(FoldScale, FVector::OneVector, Open));
			}
			break;
		}
		case ETNFaunaBody::Quad:
		case ETNFaunaBody::Lizard:
		{
			const bool bSprawl = Sp.Body == ETNFaunaBody::Lizard;
			const float Amp = (bSprawl ? 28.f : 22.f) + 18.f * Move;
			float Sit = 0.f;
			if (Act == ETNFaunaAct::Sit) { Sit = 42.f * ActW; }
			if (Act == ETNFaunaAct::Sentinel) { Sit = 70.f * ActW; }
			BodyRot.Pitch += Sit;
			if (Act == ETNFaunaAct::PushUp)
			{
				BodyRot.Pitch += ActW * (0.5f + 0.5f * FMath::Sin(A.StateT * 11.f)) * 14.f;
			}
			if (bSprawl) { BodyRot.Yaw += FMath::Sin(Ph) * 9.f * Move; }
			Set(ETNFaunaBone::LegFL, FRotator(FMath::Sin(Ph) * Amp * Move, 0.f, 0.f));
			Set(ETNFaunaBone::LegBR, FRotator(FMath::Sin(Ph) * Amp * Move, 0.f, 0.f));
			Set(ETNFaunaBone::LegFR, FRotator(FMath::Sin(Ph + UE_PI) * Amp * Move, 0.f, 0.f));
			Set(ETNFaunaBone::LegBL, FRotator(FMath::Sin(Ph + UE_PI) * Amp * Move, 0.f, 0.f));
			const float Wag = (Act == ETNFaunaAct::Wag ? 25.f * ActW : 8.f) * FMath::Sin(Tm * (Act == ETNFaunaAct::Wag ? 9.f : 2.f));
			Set(ETNFaunaBone::Tail, FRotator(0.f, Wag, 0.f));
			break;
		}
		case ETNFaunaBody::Crab:
		{
			Set(ETNFaunaBone::LegFL, FRotator(0.f, 0.f, 12.f * Move * FMath::Sin(Ph)));
			Set(ETNFaunaBone::LegFR, FRotator(0.f, 0.f, -12.f * Move * FMath::Sin(Ph + UE_PI)));
			for (const float Side : { -1.f, 1.f })
			{
				const float Snap = Act == ETNFaunaAct::Claws ? ActW * FMath::Max(0.f, FMath::Sin(A.StateT * 8.f + (Side > 0.f ? 1.2f : 0.f))) : 0.f;
				Set(Side < 0.f ? ETNFaunaBone::ClawL : ETNFaunaBone::ClawR, FRotator(6.f + 30.f * Snap, 0.f, 0.f));
			}
			break;
		}
		default:
		{
			// Ranas y el resto: un saltito al moverse y la garganta que se infla (Croak) con la escala del cuerpo.
			BodyOff.Z += FMath::Max(0.f, FMath::Sin(Ph)) * 6.f * Move;
			break;
		}
	}
	FVector BodyScale = FVector::OneVector;
	if (Act == ETNFaunaAct::Croak && bIdle)
	{
		BodyScale.Y = 1.f + 0.15f * ActW * FMath::Max(0.f, FMath::Sin(A.StateT * 6.f));
	}
	OutBones[static_cast<int32>(ETNFaunaBone::Body)] = FTransform(BodyRot, BodyOff, BodyScale);
}
