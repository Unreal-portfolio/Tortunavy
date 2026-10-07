#include "World/Beach/TN_BeachSeaweed.h"
#include "World/TN_HazardEffects.h"
#include "World/Beach/TN_BeachNearby.h"
#include "World/Beach/TN_BeachStun.h"
#include "World/Beach/TN_BeachTrapSynthComponent.h"
#include "Core/TN_Log.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Player/TN_StaminaComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "World/TN_SeaweedDecisions.h"
#include "ProceduralMeshComponent.h"
#include "TN_BeachTrapKit.h"

/**
 * Geometría de las algas: mancha de arena mojada, pila de «gotas» aplastadas de varios verdes y marrones, cintas
 * onduladas que salen de la pila hacia el borde (con vesículas) y, en la malla viva, tallos de pie que se mecen y las
 * algas que se enrollan alrededor de cada tortuga enganchada (topología fija: los huecos libres se esconden bajo la arena).
 */
namespace TNBeachSeaweedDetail
{
	using FBuffers = TNBeachTrapKit::FBuffers;

	/** Tortugas enganchadas a la vez como mucho (partidas de hasta ocho: con cuatro, la quinta pasaba sin que la cogiera). */
	constexpr int32 MaxCatches = 8;
	constexpr int32 WrapStrands = 5;
	constexpr int32 WrapPoints = 9;
	constexpr int32 NumFronds = 7;
	constexpr int32 FrondPoints = 7;
	constexpr double WrapHeight = 100.0;
	/** Parte de la elipse que engancha (el borde son cintas sueltas). */
	constexpr double CatchFraction = 0.85;

	/**
	 * Alcance de las reglas en planta, en veces el semieje mayor: la elipse más grande que miran (soltar a 1,2 veces la de
	 * enganchar, ~1,02) con holgura. Más el margen fijo (cm) para la cápsula.
	 */
	constexpr double ReachAxes = 1.15;
	constexpr double ReachMargin = 120.0;

	/** Radio (cm, en planta, con la escala del actor) fuera del cual ningún personaje cumple ninguna regla de las algas. */
	double ReachRadius(double InAx, double InAy, const FTransform& ActorXf)
	{
		return (FMath::Max(InAx, InAy) * ReachAxes + ReachMargin) * ActorXf.GetMaximumAxisScale();
	}

	/** Tipos de efecto de PlayCatchFX. */
	constexpr int32 FXCatch = 0;
	constexpr int32 FXTug = 1;
	constexpr int32 FXRelease = 2;

	FLinearColor Kelp(int32 Index)
	{
		static const uint32 Hex[5] = { 0x2F5A36, 0x3E6B2F, 0x56702C, 0x6B4F2A, 0x4A5E2A };
		return TNPlaygroundKit::Rgb(Hex[((Index % 5) + 5) % 5], 0.35f);
	}

	/** Altura de la pila en (X, Y) del actor. */
	double MoundAt(double X, double Y, double InAx, double InAy, double Height)
	{
		const double Q = FMath::Square(X / (InAx * 0.55)) + FMath::Square(Y / (InAy * 0.55));
		return Q >= 1.0 ? 0.0 : Height * FMath::Pow(1.0 - Q, 0.8);
	}

	/** Punto del borde de la elipse en la dirección Theta. */
	double EdgeRadius(double Theta, double InAx, double InAy)
	{
		const double C = FMath::Cos(Theta) / InAx;
		const double S = FMath::Sin(Theta) / InAy;
		return 1.0 / FMath::Max(1e-6, FMath::Sqrt(C * C + S * S));
	}

	/** Cinta plana de dos caras a lo largo de Path, con ancho por punto. */
	void AddRibbon(FBuffers& B, const TArray<FVector>& Path, const TArray<double>& Widths, const FLinearColor& Root, const FLinearColor& Tip)
	{
		for (int32 i = 0; i + 1 < Path.Num(); ++i)
		{
			const FVector Along0 = (Path[FMath::Min(i + 1, Path.Num() - 1)] - Path[FMath::Max(i - 1, 0)]).GetSafeNormal2D();
			const FVector Along1 = (Path[FMath::Min(i + 2, Path.Num() - 1)] - Path[i]).GetSafeNormal2D();
			const FVector Side0(-Along0.Y, Along0.X, 0.0);
			const FVector Side1(-Along1.Y, Along1.X, 0.0);
			const FVector P0 = Path[i] - Side0 * Widths[i] * 0.5;
			const FVector P1 = Path[i] + Side0 * Widths[i] * 0.5;
			const FVector P2 = Path[i + 1] + Side1 * Widths[i + 1] * 0.5;
			const FVector P3 = Path[i + 1] - Side1 * Widths[i + 1] * 0.5;
			const FLinearColor Col = TNPlaygroundKit::Mix(Root, Tip, static_cast<double>(i) / FMath::Max(1, Path.Num() - 2));
			B.AddQuad(P0, P1, P2, P3, FVector::UpVector, Col);
			B.AddQuad(P0, P1, P2, P3, -FVector::UpVector, TNPlaygroundKit::Shade(Col, 0.75));
		}
	}

	/** Mancha mojada, pila y cintas. Devuelve dónde nacen los tallos de pie. */
	void BuildPatch(FBuffers& B, double InAx, double InAy, double Height, uint32 Seed, TArray<FVector>& OutFrondBases)
	{
		// Mancha de arena mojada, algo más grande que las algas y de borde irregular.
		constexpr int32 StainSeg = 28;
		for (int32 k = 0; k < StainSeg; ++k)
		{
			const double A0 = TNPlaygroundKit::KitTwoPi * k / StainSeg;
			const double A1 = TNPlaygroundKit::KitTwoPi * (k + 1) / StainSeg;
			const double R0 = 1.08 + 0.08 * TNProcMesh::TNProcHashNoise(k, 1, Seed);
			const double R1 = 1.08 + 0.08 * TNProcMesh::TNProcHashNoise((k + 1) % StainSeg, 1, Seed);
			B.AddTri(FVector(0.0, 0.0, 1.0), FVector(FMath::Cos(A0) * InAx * R0, FMath::Sin(A0) * InAy * R0, 1.0),
				FVector(FMath::Cos(A1) * InAx * R1, FMath::Sin(A1) * InAy * R1, 1.0), FVector::UpVector, TNBeachTrapKit::SandWet());
		}

		// Pila: gotas aplastadas que se solapan (la mitad de abajo queda bajo la arena).
		const int32 Blobs = 6;
		for (int32 b = 0; b < Blobs; ++b)
		{
			const double Bx = (TNPlaygroundKit::Hash01(b, 1, Seed) - 0.5) * InAx * 0.6;
			const double By = (TNPlaygroundKit::Hash01(b, 2, Seed) - 0.5) * InAy * 0.6;
			const FVector Radii(InAx * FMath::Lerp(0.2, 0.34, TNPlaygroundKit::Hash01(b, 3, Seed)), InAy * FMath::Lerp(0.2, 0.34, TNPlaygroundKit::Hash01(b, 4, Seed)),
				Height * FMath::Lerp(0.55, 1.0, TNPlaygroundKit::Hash01(b, 5, Seed)));
			const double Yaw = TNPlaygroundKit::KitTwoPi * TNPlaygroundKit::Hash01(b, 6, Seed);
			const FVector Ex(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.0);
			const FVector Ey(-Ex.Y, Ex.X, 0.0);
			TNPlaygroundKit::AddEllipsoid(B, FVector(Bx, By, -Height * 0.15), Ex, Ey, FVector::UpVector, Radii, 12, 6, Kelp(b));
		}

		// Cintas desde la pila hacia el borde, onduladas y pegadas a la forma de la pila.
		const int32 Ribbons = FMath::Clamp(FMath::RoundToInt32((InAx + InAy) / 24.0), 16, 44);
		for (int32 r = 0; r < Ribbons; ++r)
		{
			const double Theta = TNPlaygroundKit::KitTwoPi * (r + 0.6 * TNPlaygroundKit::Hash01(r, 1, Seed)) / Ribbons;
			const double Edge = EdgeRadius(Theta, InAx, InAy);
			const double Start = Edge * FMath::Lerp(0.05, 0.35, TNPlaygroundKit::Hash01(r, 2, Seed));
			const double End = Edge * FMath::Lerp(0.92, 1.06, TNPlaygroundKit::Hash01(r, 3, Seed));
			const double Wave = FMath::Lerp(20.0, 45.0, TNPlaygroundKit::Hash01(r, 4, Seed));
			const double Width = FMath::Lerp(26.0, 44.0, TNPlaygroundKit::Hash01(r, 5, Seed));
			const FVector Dir(FMath::Cos(Theta), FMath::Sin(Theta), 0.0);
			const FVector Perp(-Dir.Y, Dir.X, 0.0);
			TArray<FVector> Path;
			TArray<double> Widths;
			constexpr int32 Samples = 9;
			for (int32 i = 0; i < Samples; ++i)
			{
				const double U = static_cast<double>(i) / (Samples - 1);
				const double Rad = FMath::Lerp(Start, End, U);
				FVector P = Dir * Rad + Perp * (Wave * FMath::Sin(U * 7.0 + r));
				P.Z = MoundAt(P.X, P.Y, InAx, InAy, Height) + 3.0 + 2.0 * (r % 3);
				Path.Add(P);
				Widths.Add(Width * FMath::Lerp(1.0, 0.3, U));
			}
			AddRibbon(B, Path, Widths, Kelp(r), TNPlaygroundKit::Mix(Kelp(r), TNPlaygroundKit::Rgb(0x9A9A3A, 0.3f), 0.5));
			// Vesículas de aire en algunas cintas.
			if (r % 3 == 0)
			{
				for (const int32 Idx : { 3, 6 })
				{
					TNPlaygroundKit::AddBall(B, Path[Idx] + FVector(0.0, 0.0, 5.0), 9.0, 8, TNPlaygroundKit::Rgb(0x7A8B3A, 0.4f));
				}
			}
		}

		// Tallos de pie: sobre la pila, repartidos.
		OutFrondBases.Reset();
		for (int32 f = 0; f < NumFronds; ++f)
		{
			const double A = TNPlaygroundKit::KitTwoPi * (f + 0.5 * TNPlaygroundKit::Hash01(f, 8, Seed)) / NumFronds;
			const double Rr = FMath::Lerp(0.1, 0.45, TNPlaygroundKit::Hash01(f, 9, Seed));
			const double X = FMath::Cos(A) * InAx * Rr;
			const double Y = FMath::Sin(A) * InAy * Rr;
			OutFrondBases.Add(FVector(X, Y, MoundAt(X, Y, InAx, InAy, Height) - 4.0));
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// ATN_BeachSeaweed
// ─────────────────────────────────────────────────────────────────────────────

ATN_BeachSeaweed::ATN_BeachSeaweed()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(4.f);

	PatchMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PatchMesh"));
	PatchMesh->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureVisual(PatchMesh);
}

void ATN_BeachSeaweed::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_BeachSeaweed, Catches);
	DOREPLIFETIME(ATN_BeachSeaweed, bCut);
}

void ATN_BeachSeaweed::ApplySpec()
{
	using namespace TNBeachSeaweedDetail;
	const double Fit = TNBeachTrapKit::FitRadius(Spec.Element, Spec.SizeScale);
	const uint32 Seed = TNBeachTrapKit::SeedOf(Spec.Seed, 23u);
	Ax = Fit * 0.9;
	Ay = Ax * FMath::Lerp(0.78, 1.0, TNPlaygroundKit::Hash01(1, 1, Seed));
	if (Spec.Extent > 1.f)
	{
		Ay = FMath::Min(Ay, 0.5 * static_cast<double>(Spec.Extent));
	}
	Ay = FMath::Max(Ay, 120.0);
	MoundH = FMath::Clamp(0.1 * FMath::Min(Ax, Ay), 25.0, 65.0);

	TNBeachTrapKit::FBuffers Patch;
	BuildPatch(Patch, Ax, Ay, MoundH, Seed, FrondBases);
	TNBeachTrapKit::SetMesh(PatchMesh, this, Patch, TN_ART("Beach.Seaweed.Patch"));
	FrondPhases.Reset();
	FrondHeights.Reset();
	for (int32 f = 0; f < FrondBases.Num(); ++f)
	{
		FrondPhases.Add(static_cast<float>(TNPlaygroundKit::KitTwoPi * TNPlaygroundKit::Hash01(f, 11, Seed)));
		FrondHeights.Add(static_cast<float>(FMath::Lerp(70.0, 130.0, TNPlaygroundKit::Hash01(f, 12, Seed))));
	}
	WrapSlots.SetNum(MaxCatches);
	for (int32 w = 0; w < WrapSlots.Num(); ++w)
	{
		WrapSlots[w].Phase = static_cast<float>(1.3 * w);
	}
	if (LiveMesh)
	{
		RebuildLiveMesh();
	}
}

void ATN_BeachSeaweed::BeginPlay()
{
	Super::BeginPlay();
	UWorld* World = GetWorld();
	if (World && World->IsGameWorld() && GetNetMode() != NM_DedicatedServer)
	{
		LiveMesh = NewObject<UProceduralMeshComponent>(this, NAME_None, RF_Transient);
		LiveMesh->SetupAttachment(GetRootComponent());
		LiveMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		LiveMesh->SetCanEverAffectNavigation(false);
		LiveMesh->bUseAsyncCooking = true;
		LiveMesh->RegisterComponent();
		RebuildLiveMesh();
		Voice = UTN_BeachTrapSynthComponent::AttachTo(this, GetActorLocation() + FVector(0.0, 0.0, 40.0), 500.f, 2600.f);
		Splash.Init(this, ETNTrapBurstShape::Blob, TNPlaygroundKit::Rgb(0x3E6B2F, 0.4f), 24);
		Splash.SetMotion(-1100.f, 1.f, 16.f, 6.f, 0.35f, 0.6f);
	}
}

void ATN_BeachSeaweed::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Nadie se queda frenado con las algas destruidas (fin de ronda).
	TArray<TWeakObjectPtr<ACharacter>> Keys;
	Holds.GetKeys(Keys);
	for (const TWeakObjectPtr<ACharacter>& Key : Keys)
	{
		if (ACharacter* Turtle = Key.Get())
		{
			RemoveHold(Turtle);
		}
	}
	Holds.Reset();
	Super::EndPlay(EndPlayReason);
}

// ─────────────────────────────────────────────────────────────────────────────
// Consultas
// ─────────────────────────────────────────────────────────────────────────────

int32 ATN_BeachSeaweed::FindCatchIndex(const ACharacter* Turtle) const
{
	if (!Turtle)
	{
		return INDEX_NONE;
	}
	for (int32 i = 0; i < Catches.Num(); ++i)
	{
		if (Catches[i].Turtle == Turtle)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

bool ATN_BeachSeaweed::IsCaught(const ACharacter* Turtle) const
{
	return FindCatchIndex(Turtle) != INDEX_NONE;
}

bool ATN_BeachSeaweed::IsInPatch(const ACharacter* Turtle, double Grow) const
{
	if (!Turtle)
	{
		return false;
	}
	const UCapsuleComponent* Capsule = Turtle->GetCapsuleComponent();
	const FVector Local = GetActorTransform().InverseTransformPosition(Turtle->GetActorLocation());
	const double Feet = Local.Z - (Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 70.0);
	if (Feet < -150.0 || Feet > MoundH + 60.0)
	{
		return false;
	}
	const double Nx = Local.X / (Ax * TNBeachSeaweedDetail::CatchFraction * Grow);
	const double Ny = Local.Y / (Ay * TNBeachSeaweedDetail::CatchFraction * Grow);
	return Nx * Nx + Ny * Ny <= 1.0;
}

bool ATN_BeachSeaweed::ContainsPoint(const FVector& WorldPoint) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldPoint);
	if (Local.Z < -200.0 || Local.Z > MoundH + 200.0)
	{
		return false;
	}
	constexpr double Grow = 1.15;
	const double Nx = Local.X / (Ax * Grow);
	const double Ny = Local.Y / (Ay * Grow);
	return Nx * Nx + Ny * Ny <= 1.0;
}

bool ATN_BeachSeaweed::ServerHitBySlap(const FVector& SlapOrigin, const FVector& SlapPoint)
{
	if (!HasAuthority() || bCut || (!ContainsPoint(SlapOrigin) && !ContainsPoint(SlapPoint)))
	{
		return false;
	}
	++HitsTaken;
	if (!TNHazard::SeaweedCut(HitsTaken, UTN_HazardTuning::Get().SeaweedHitsToCut))
	{
		return true;
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Playa] Algas %s cortadas de un golpe."), *GetName());
	bCut = true;
	Catches.Reset();
	Tracks.Reset();
	HandleCatchesChanged();
	ApplyCutLocal();
	FlushNetDormancy();
	ForceNetUpdate();
	return true;
}

void ATN_BeachSeaweed::OnRep_Cut()
{
	if (!IsActorTickEnabled())
	{
		SetActorTickEnabled(true);
	}
	ApplyCutLocal();
}

void ATN_BeachSeaweed::ApplyCutLocal()
{
	if (!bCut || bCutApplied)
	{
		return;
	}
	bCutApplied = true;
	PredictedSince = -1.0;
	TArray<TWeakObjectPtr<ACharacter>> Keys;
	Holds.GetKeys(Keys);
	for (const TWeakObjectPtr<ACharacter>& Key : Keys)
	{
		if (ACharacter* Turtle = Key.Get())
		{
			RemoveHold(Turtle);
		}
	}
	Holds.Reset();
	if (PatchMesh)
	{
		PatchMesh->SetVisibility(false);
	}
	if (LiveMesh)
	{
		LiveMesh->SetVisibility(false);
		Splash.Burst(GetActorLocation() + FVector(0.0, 0.0, MoundH), 20, FVector::UpVector, 420.f, 1.1f, static_cast<float>(FMath::Min(Ax, Ay) * 0.6));
	}
}

bool ATN_BeachSeaweed::IsVisuallyHeld(const ACharacter* Turtle) const
{
	if (IsCaught(Turtle))
	{
		return true;
	}
	return PredictedSince >= 0.0 && Turtle && Turtle->IsLocallyControlled() && GetNetMode() == NM_Client;
}

// ─────────────────────────────────────────────────────────────────────────────
// Reglas
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachSeaweed::ServerUpdate()
{
	using namespace TNBeachSeaweedDetail;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = TNBeachTrapKit::ServerNow(World);
	bool bChanged = false;

	// Enganchadas: meneos, arrastradas fuera y suelta.
	for (int32 i = Catches.Num() - 1; i >= 0; --i)
	{
		FTNSeaweedCatch& Entry = Catches[i];
		ACharacter* Held = Entry.Turtle;
		bool bRelease = !TNBeachTrapKit::IsFreeTurtle(Held);
		if (!bRelease)
		{
			FTugTrack& Track = Tracks.FindOrAdd(Held);
			const UCharacterMovementComponent* Move = Held->GetCharacterMovement();
			const FVector Accel = Move ? Move->GetCurrentAcceleration() : FVector::ZeroVector;
			const FVector2D Dir2 = FVector2D(Accel.X, Accel.Y).GetSafeNormal();
			if (!Dir2.IsNearlyZero())
			{
				if (!Track.LastDir.IsNearlyZero() && FVector2D::DotProduct(Dir2, Track.LastDir) < 0.0 && Now - Track.LastWiggle > 0.12)
				{
					Entry.ReleaseAt -= WiggleTug;
					++Entry.Tugs;
					Track.LastWiggle = Now;
					bChanged = true;
				}
				Track.LastDir = Dir2;
			}
			bRelease = Now >= Entry.ReleaseAt || !IsInPatch(Held, 1.2);
		}
		if (bRelease)
		{
			if (Held)
			{
				ImmuneUntil.Add(Held, Now + ImmuneSeconds);
				Tracks.Remove(Held);
			}
			Catches.RemoveAt(i);
			bChanged = true;
		}
	}

	// Nuevas: con los pies en las algas y sin gracia pendiente (solo quien está al alcance).
	TArray<ACharacter*> Near;
	TNBeachNearby::Gather(World, GetActorLocation(), ReachRadius(Ax, Ay, GetActorTransform()), Near);
	for (ACharacter* Walker : Near)
	{
		if (Catches.Num() >= MaxCatches || !TNBeachTrapKit::IsFreeTurtle(Walker) || IsCaught(Walker))
		{
			continue;
		}
		if (const double* Until = ImmuneUntil.Find(Walker))
		{
			if (Now < *Until)
			{
				continue;
			}
		}
		const UCharacterMovementComponent* Move = Walker->GetCharacterMovement();
		if (!Move || Move->IsFalling() || !IsInPatch(Walker, 1.0))
		{
			continue;
		}
		FTNSeaweedCatch& Entry = Catches.AddDefaulted_GetRef();
		Entry.Turtle = Walker;
		Entry.ReleaseAt = static_cast<float>(Now + CatchSeconds);
		Entry.Tugs = 0;
		Tracks.Add(Walker, FTugTrack());
		bChanged = true;
		UE_LOG(LogTortunabo, Verbose, TEXT("[Playa] Algas %s enganchan a %s."), *GetName(), *Walker->GetName());
	}

	for (auto It = ImmuneUntil.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || Now > It.Value() + 5.0)
		{
			It.RemoveCurrent();
		}
	}
	if (bChanged)
	{
		HandleCatchesChanged();
		ForceNetUpdate();
	}
}

void ATN_BeachSeaweed::UpdateLocalPrediction()
{
	if (GetNetMode() != NM_Client)
	{
		return;
	}
	UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	ACharacter* Mine = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
	if (!Mine)
	{
		PredictedSince = -1.0;
		return;
	}
	const double Now = TNBeachTrapKit::ServerNow(World);
	if (IsCaught(Mine))
	{
		// Confirmado: manda el estado replicado.
		PredictedSince = -1.0;
		return;
	}
	if (PredictedSince >= 0.0)
	{
		if (Now - PredictedSince > 0.6 || !TNBeachTrapKit::IsFreeTurtle(Mine))
		{
			// El servidor no lo ha confirmado: se deshace.
			PredictedSince = -1.0;
			LocalImmuneUntil = Now + 1.0;
		}
		return;
	}
	const UCharacterMovementComponent* Move = Mine->GetCharacterMovement();
	if (Now >= LocalImmuneUntil && Move && !Move->IsFalling() && TNBeachTrapKit::IsFreeTurtle(Mine) && IsInPatch(Mine, 0.95))
	{
		PredictedSince = Now;
		LocalFXTime = Now;
		PlayCatchFX(Mine, TNBeachSeaweedDetail::FXCatch);
	}
}

void ATN_BeachSeaweed::UpdateHolds()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	TSet<TWeakObjectPtr<ACharacter>> Seen;
	TArray<ACharacter*> Near;
	TNBeachNearby::Gather(World, GetActorLocation(), TNBeachSeaweedDetail::ReachRadius(Ax, Ay, GetActorTransform()), Near);
	for (ACharacter* Walker : Near)
	{
		if (!IsValid(Walker) || !TNBeachTrapKit::SimulatesMovement(Walker))
		{
			continue;
		}
		const bool bFree = TNBeachTrapKit::IsFreeTurtle(Walker);
		const bool bHeld = bFree && IsVisuallyHeld(Walker);
		const bool bWade = bFree && !bHeld && IsInPatch(Walker, 1.0 / TNBeachSeaweedDetail::CatchFraction);
		if (bHeld || bWade)
		{
			ApplyHold(Walker, bHeld ? HeldSpeed : WadeSpeed, bHeld);
			Seen.Add(Walker);
		}
	}
	TArray<TWeakObjectPtr<ACharacter>> Keys;
	Holds.GetKeys(Keys);
	for (const TWeakObjectPtr<ACharacter>& Key : Keys)
	{
		if (!Seen.Contains(Key))
		{
			if (ACharacter* Turtle = Key.Get())
			{
				RemoveHold(Turtle);
			}
			else
			{
				Holds.Remove(Key);
			}
		}
	}
}

void ATN_BeachSeaweed::ApplyHold(ACharacter* Turtle, float Cap, bool bHeld)
{
	UCharacterMovementComponent* Move = Turtle ? Turtle->GetCharacterMovement() : nullptr;
	if (!Move)
	{
		return;
	}
	FHoldState* State = Holds.Find(Turtle);
	if (!State)
	{
		State = &Holds.Add(Turtle);
	}
	UTN_StaminaComponent* Stamina = Turtle->FindComponentByClass<UTN_StaminaComponent>();
	if (Stamina)
	{
		// Con el nombre de esta alga: los topes de los demás (mareo, llevar a otra, caparazón) siguen aparte.
		Stamina->SetSpeedCap(LimitSource(), Cap);
	}
	State->AppliedCap = Cap;
	if (bHeld != State->bHeld)
	{
		State->bHeld = bHeld;
		// Enganchada: el salto no la levanta (cada intento es un tirón que cuenta el servidor). Al soltarse, el componente
		// devuelve el salto que toque (el de base o el de otra zona que siga puesta).
		if (Stamina)
		{
			if (bHeld)
			{
				Stamina->SetJumpLimit(LimitSource(), 0.f);
			}
			else
			{
				Stamina->ClearJumpLimit(LimitSource());
			}
		}
		if (bHeld)
		{
			Turtle->MovementModeChangedDelegate.AddUniqueDynamic(this, &ATN_BeachSeaweed::OnHeldModeChanged);
		}
		else
		{
			Turtle->MovementModeChangedDelegate.RemoveDynamic(this, &ATN_BeachSeaweed::OnHeldModeChanged);
		}
	}
	if (bHeld)
	{
		// En el aire (el panzazo) no manda MaxWalkSpeed: también se recorta la velocidad horizontal.
		const FVector Flat(Move->Velocity.X, Move->Velocity.Y, 0.0);
		if (Flat.Size() > Cap * 1.05f)
		{
			const FVector Clamped = Flat.GetSafeNormal() * Cap;
			Move->Velocity.X = Clamped.X;
			Move->Velocity.Y = Clamped.Y;
		}
	}
}

void ATN_BeachSeaweed::RemoveHold(ACharacter* Turtle)
{
	FHoldState* State = Turtle ? Holds.Find(Turtle) : nullptr;
	if (!State)
	{
		return;
	}
	Turtle->MovementModeChangedDelegate.RemoveDynamic(this, &ATN_BeachSeaweed::OnHeldModeChanged);
	// Solo lo de esta alga: si está en el caparazón (aturdida), el tope del caparazón sigue.
	if (UTN_StaminaComponent* Stamina = Turtle->FindComponentByClass<UTN_StaminaComponent>())
	{
		Stamina->ClearSpeedCap(LimitSource());
		Stamina->ClearJumpLimit(LimitSource());
	}
	Holds.Remove(Turtle);
}

void ATN_BeachSeaweed::OnHeldModeChanged(ACharacter* Turtle, EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	const UCharacterMovementComponent* Move = Turtle ? Turtle->GetCharacterMovement() : nullptr;
	if (!Move || !Move->IsFalling() || PrevMovementMode != MOVE_Walking)
	{
		return;
	}
	if (HasAuthority())
	{
		const int32 Index = FindCatchIndex(Turtle);
		if (Catches.IsValidIndex(Index))
		{
			Catches[Index].ReleaseAt -= JumpTug;
			++Catches[Index].Tugs;
			HandleCatchesChanged();
			ForceNetUpdate();
		}
	}
	else if (Turtle->IsLocallyControlled())
	{
		// Sacudida inmediata en el cliente dueño; el tirón lo cuenta el servidor.
		LocalFXTime = TNBeachTrapKit::ServerNow(GetWorld());
		PlayCatchFX(Turtle, TNBeachSeaweedDetail::FXTug);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Efectos
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachSeaweed::OnRep_Catches()
{
	// Dormida por distancia (UTN_BeachTickWakeSubsystem): un enganche la despierta ya, sin esperar a la siguiente mirada.
	if (!IsActorTickEnabled())
	{
		SetActorTickEnabled(true);
	}
	HandleCatchesChanged();
}

float ATN_BeachSeaweed::GetTickWakeDistance() const
{
	return static_cast<float>(TNSeaweedLogic::FREEZE_DISTANCE) + GetFootprintRadius();
}

bool ATN_BeachSeaweed::IsTickBusy() const
{
	if (Catches.Num() > 0 || Holds.Num() > 0 || PredictedSince >= 0.0 || (bCut && Splash.IsLive()))
	{
		return true;
	}
	for (const FWrapSlot& Slot : WrapSlots)
	{
		if (Slot.Curl > 0.f)
		{
			return true;
		}
	}
	return false;
}

void ATN_BeachSeaweed::HandleCatchesChanged()
{
	using namespace TNBeachSeaweedDetail;
	const double Now = TNBeachTrapKit::ServerNow(GetWorld());
	for (const FTNSeaweedCatch& Entry : Catches)
	{
		const ACharacter* Turtle = Entry.Turtle;
		if (!Turtle)
		{
			continue;
		}
		const FTNSeaweedCatch* Before = SeenCatches.FindByPredicate([Turtle](const FTNSeaweedCatch& Old) { return Old.Turtle == Turtle; });
		const bool bLocalRecent = Turtle->IsLocallyControlled() && Now - LocalFXTime < 0.8;
		if (!Before)
		{
			if (!bLocalRecent)
			{
				PlayCatchFX(Turtle, FXCatch);
			}
		}
		else if (Before->Tugs != Entry.Tugs && !bLocalRecent)
		{
			PlayCatchFX(Turtle, FXTug);
		}
	}
	for (const FTNSeaweedCatch& Old : SeenCatches)
	{
		const ACharacter* Turtle = Old.Turtle;
		if (!Turtle || IsCaught(Turtle))
		{
			continue;
		}
		PlayCatchFX(Turtle, FXRelease);
		if (Turtle->IsLocallyControlled() && GetNetMode() == NM_Client)
		{
			LocalImmuneUntil = Now + ImmuneSeconds;
			PredictedSince = -1.0;
		}
	}
	SeenCatches = Catches;
}

void ATN_BeachSeaweed::PlayCatchFX(const ACharacter* Turtle, int32 Kind)
{
	using namespace TNBeachSeaweedDetail;
	if (!Turtle || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const UCapsuleComponent* Capsule = Turtle->GetCapsuleComponent();
	const FVector Feet = Turtle->GetActorLocation() - FVector(0.0, 0.0, Capsule ? Capsule->GetScaledCapsuleHalfHeight() - 15.0 : 55.0);
	switch (Kind)
	{
	case FXCatch:
		Splash.Burst(Feet, 10, FVector::UpVector, 420.f, 0.9f, 30.f);
		if (Voice)
		{
			Voice->TriggerSoundAt(ETNBeachTrapSound::Squelch, Feet, FMath::FRandRange(0.8f, 0.95f), 1.f);
		}
		break;
	case FXTug:
		Splash.Burst(Feet, 4, FVector::UpVector, 320.f, 1.1f, 25.f);
		if (Voice)
		{
			Voice->TriggerSoundAt(ETNBeachTrapSound::Squelch, Feet, FMath::FRandRange(1.05f, 1.3f), 0.6f);
		}
		for (FWrapSlot& Slot : WrapSlots)
		{
			if (Slot.Turtle.Get() == Turtle)
			{
				Slot.Jerk = 1.f;
			}
		}
		break;
	default:
		Splash.Burst(Feet, 8, FVector::UpVector, 500.f, 1.f, 35.f);
		if (Voice)
		{
			Voice->TriggerSoundAt(ETNBeachTrapSound::Squelch, Feet, FMath::FRandRange(1.35f, 1.55f), 0.85f);
		}
		break;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Algas vivas
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachSeaweed::UpdateWraps(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World || WrapSlots.Num() == 0)
	{
		return;
	}
	// Huecos para las tortugas enganchadas que aún no tienen.
	TArray<ACharacter*> Near;
	TNBeachNearby::Gather(World, GetActorLocation(), TNBeachSeaweedDetail::ReachRadius(Ax, Ay, GetActorTransform()), Near);
	for (ACharacter* Walker : Near)
	{
		if (!IsValid(Walker) || !IsVisuallyHeld(Walker))
		{
			continue;
		}
		const bool bHasSlot = WrapSlots.ContainsByPredicate([Walker](const FWrapSlot& Slot) { return Slot.Turtle.Get() == Walker; });
		if (bHasSlot)
		{
			continue;
		}
		for (FWrapSlot& Slot : WrapSlots)
		{
			if (!Slot.Turtle.IsValid() || Slot.Curl <= 0.01f)
			{
				Slot.Turtle = Walker;
				Slot.Curl = 0.f;
				Slot.Jerk = 0.f;
				break;
			}
		}
	}
	const FTransform ActorXf = GetActorTransform();
	for (FWrapSlot& Slot : WrapSlots)
	{
		const ACharacter* Turtle = Slot.Turtle.Get();
		const bool bHeld = Turtle && IsVisuallyHeld(Turtle);
		const float Target = bHeld ? 1.f : 0.f;
		Slot.Curl += (Target - Slot.Curl) * FMath::Min(1.f, DeltaSeconds * (Target > Slot.Curl ? 6.f : 3.5f));
		if (!bHeld && Slot.Curl < 0.01f)
		{
			Slot.Curl = 0.f;
			Slot.Turtle = nullptr;
		}
		Slot.Jerk = FMath::Max(0.f, Slot.Jerk - DeltaSeconds * 3.f);
		if (Turtle)
		{
			const UCapsuleComponent* Capsule = Turtle->GetCapsuleComponent();
			const FVector Feet = Turtle->GetActorLocation() - FVector(0.0, 0.0, Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 70.0);
			Slot.Anchor = ActorXf.InverseTransformPosition(Feet);
		}
	}
}

void ATN_BeachSeaweed::RebuildLiveMesh()
{
	using namespace TNBeachSeaweedDetail;
	if (!LiveMesh)
	{
		return;
	}
	TNBeachTrapKit::FBuffers Live;
	// Buffers de trabajo reutilizados entre tallos y hebras: sin reservar memoria por tubo.
	TArray<FVector> Path;
	TArray<double> Radii;
	TArray<FLinearColor> Colors;
	// Tallos de pie que se mecen (más cuando alguien está enganchado cerca).
	for (int32 f = 0; f < FrondBases.Num(); ++f)
	{
		Path.Reset();
		Radii.Reset();
		Colors.Reset();
		const float Phase = FrondPhases.IsValidIndex(f) ? FrondPhases[f] : 0.f;
		const double Height = FrondHeights.IsValidIndex(f) ? FrondHeights[f] : 90.0;
		for (int32 i = 0; i < FrondPoints; ++i)
		{
			const double U = static_cast<double>(i) / (FrondPoints - 1);
			const double Sway = 22.0 * U * U;
			Path.Add(FrondBases[f] + FVector(Sway * FMath::Sin(AnimTime * 1.3 + Phase), Sway * FMath::Sin(AnimTime * 0.9 + Phase * 1.7), Height * U));
			Radii.Add(FMath::Lerp(7.0, 2.5, U));
			Colors.Add(TNPlaygroundKit::Mix(Kelp(f), TNPlaygroundKit::Rgb(0x8A9A3A, 0.3f), U));
		}
		TNPlaygroundKit::AddTube(Live, Path, Radii, 5, Colors, FVector::ForwardVector, true);
	}
	// Algas enrolladas: cada hueco tiene siempre sus hebras (escondidas bajo la arena si está libre).
	for (const FWrapSlot& Slot : WrapSlots)
	{
		const double C = Slot.Curl;
		for (int32 j = 0; j < WrapStrands; ++j)
		{
			Path.Reset();
			Radii.Reset();
			Colors.Reset();
			const double Base = TNPlaygroundKit::KitTwoPi * j / WrapStrands + Slot.Phase;
			for (int32 i = 0; i < WrapPoints; ++i)
			{
				const double U = static_cast<double>(i) / (WrapPoints - 1);
				const double Ang = Base + U * 1.7 * TNPlaygroundKit::KitPi * C + 0.3 * C * FMath::Sin(AnimTime * 3.0 + j);
				const double Rad = FMath::Lerp(62.0, 40.0, U * C) * (1.0 - 0.18 * Slot.Jerk * U);
				const double Z = U * WrapHeight * C - (1.0 - C) * 25.0 + 6.0 * Slot.Jerk * FMath::Sin(AnimTime * 40.0 + j);
				Path.Add(Slot.Anchor + FVector(FMath::Cos(Ang) * Rad, FMath::Sin(Ang) * Rad, Z));
				Radii.Add(FMath::Lerp(8.0, 3.0, U));
				Colors.Add(Kelp(j + 1));
			}
			TNPlaygroundKit::AddTube(Live, Path, Radii, 5, Colors, FVector::UpVector, true);
		}
	}
	if (LiveMesh->GetNumSections() == 0)
	{
		const TArray<FProcMeshTangent> NoTangents;
		LiveMesh->CreateMeshSection_LinearColor(0, Live.Verts, Live.Tris, Live.Normals, Live.UVs, Live.Colors, NoTangents, false);
		LiveMesh->SetMaterial(0, TNPlaygroundKit::VertexColorMaterial());
	}
	else
	{
		// Misma topología: solo posiciones y normales.
		const TArray<FVector2D> KeepUVs;
		const TArray<FColor> KeepColors;
		const TArray<FProcMeshTangent> KeepTangents;
		LiveMesh->UpdateMeshSection(0, Live.Verts, Live.Normals, KeepUVs, KeepColors, KeepTangents);
	}
}

namespace
{
	/** Distancia (cm) de Location a la cámara local más cercana; muy grande si no hay ninguna. */
	double TNSeaweedViewDistance(const UWorld* World, const FVector& Location)
	{
		double Best = TNumericLimits<double>::Max();
		if (!World)
		{
			return Best;
		}
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* PC = It->Get();
			if (PC && PC->IsLocalController() && PC->PlayerCameraManager)
			{
				Best = FMath::Min(Best, FVector::Dist(Location, PC->PlayerCameraManager->GetCameraLocation()));
			}
		}
		return Best;
	}
}

void ATN_BeachSeaweed::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bCut)
	{
		ApplyCutLocal();
		Splash.Tick(DeltaSeconds);
		return;
	}
	if (HasAuthority())
	{
		ServerUpdate();
	}
	UpdateLocalPrediction();
	UpdateHolds();
	if (LiveMesh)
	{
		AnimTime += DeltaSeconds;
		UpdateWraps(DeltaSeconds);
		bool bWrapping = false;
		for (const FWrapSlot& Slot : WrapSlots)
		{
			bWrapping |= Slot.Curl > 0.f;
		}
		// El vaivén en reposo no se reconstruye cada frame salvo cerca de la cámara (TN_SeaweedDecisions.h).
		SinceLiveRebuild += DeltaSeconds;
		const float Interval = TNSeaweedLogic::RebuildInterval(bWrapping, LiveMesh->WasRecentlyRendered(0.3f),
			TNSeaweedViewDistance(GetWorld(), GetActorLocation()));
		if (TNSeaweedLogic::ShouldRebuild(Interval, SinceLiveRebuild))
		{
			RebuildLiveMesh();
			SinceLiveRebuild = 0.f;
		}
		Splash.Tick(DeltaSeconds);
	}
}
