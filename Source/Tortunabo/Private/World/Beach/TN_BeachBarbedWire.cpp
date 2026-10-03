#include "World/Beach/TN_BeachBarbedWire.h"
#include "World/Beach/TN_BeachNearby.h"
#include "World/Beach/TN_BeachStun.h"
#include "World/Beach/TN_BeachTrapSynthComponent.h"
#include "Core/TN_Log.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "ProceduralMeshComponent.h"
#include "TN_BeachTrapKit.h"

/**
 * Geometría del alambre: una hélice de rollos que avanza a lo largo de X (cada rollo se inclina hacia un lado distinto,
 * así se cruzan como en una concertina), con pinchos en cruz, un hilo tenso por arriba con más pinchos y estacas de
 * madera clavadas en la arena. La colisión son cajas por tramos de ~4 m.
 */
namespace TNBeachBarbedWireDetail
{
	using FBuffers = TNBeachTrapKit::FBuffers;
	using FHulls = TNBeachTrapKit::FHulls;

	/** Paso de la hélice (cm por rollo) y muestras por rollo. */
	constexpr double CoilPitch = 46.0;
	constexpr int32 SamplesPerLoop = 14;
	/** Radio del alambre, largo de los pinchos y separación de las estacas. */
	constexpr double WireRadius = 3.0;
	constexpr double BarbHalf = 11.0;
	constexpr double StakeSpacing = 700.0;
	/** Margen del sensor por fuera de la colisión. */
	constexpr double TouchMargin = 14.0;
	/** Margen (cm) del radio en el que se busca a quién toca: la cápsula de una tortuga con holgura. */
	constexpr double GatherMargin = 150.0;
	/** Semialto de las cajas de colisión sobre la altura del rollo. */
	constexpr double CollisionTopPad = 4.0;

	/** Pincho en cruz (dos varillas y el nudo) en Center, perpendicular a Tangent. */
	void AddBarb(FBuffers& B, const FVector& Center, const FVector& Tangent, double Twist, const FLinearColor& Color)
	{
		FVector N = FVector::CrossProduct(Tangent, FMath::Abs(Tangent.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
		const FVector Bn = FVector::CrossProduct(Tangent, N).GetSafeNormal();
		N = (N * FMath::Cos(Twist) + Bn * FMath::Sin(Twist)).GetSafeNormal();
		const FVector M = FVector::CrossProduct(Tangent, N).GetSafeNormal();
		const FVector D1 = (N + Tangent * 0.35).GetSafeNormal();
		const FVector D2 = (M - Tangent * 0.35).GetSafeNormal();
		TNPlaygroundKit::AddRod(B, Center - D1 * BarbHalf, Center + D1 * BarbHalf, 1.4, 3, Color, Tangent);
		TNPlaygroundKit::AddRod(B, Center - D2 * BarbHalf, Center + D2 * BarbHalf, 1.4, 3, Color, Tangent);
		TNPlaygroundKit::AddEllipsoid(B, Center, Tangent, N, M, FVector(5.0, 4.2, 4.2), 6, 3, Color);
	}

	/** Rollos, pinchos, hilo de arriba y estacas. */
	void BuildWire(FBuffers& B, double HalfLen, double CoilR, uint32 Seed)
	{
		const FLinearColor Steel = TNPlaygroundKit::Rgb(0x9AA3AB, 0.55f);
		const FLinearColor SteelDark = TNPlaygroundKit::Rgb(0x6E767E, 0.45f);
		const FLinearColor Rust = TNPlaygroundKit::Rgb(0x9A5B34, 0.1f);
		const FLinearColor Wood = TNPlaygroundKit::Rgb(0x9C6A3C);
		const FLinearColor WoodDark = TNPlaygroundKit::Rgb(0x7A4E2B);
		const FLinearColor Rope = TNPlaygroundKit::Rgb(0xE9D2A2);

		// Hélice de rollos.
		const int32 Loops = FMath::Max(4, FMath::FloorToInt32(2.0 * HalfLen / CoilPitch));
		const double Advance = 2.0 * HalfLen / Loops;
		TArray<FVector> Path;
		TArray<double> Radii;
		TArray<FLinearColor> Colors;
		const int32 NumSamples = Loops * SamplesPerLoop + 1;
		Path.Reserve(NumSamples);
		Radii.Reserve(NumSamples);
		Colors.Reserve(NumSamples);
		for (int32 i = 0; i < NumSamples; ++i)
		{
			const int32 Loop = FMath::Min(i / SamplesPerLoop, Loops - 1);
			const double T = static_cast<double>(i) / SamplesPerLoop;
			const double Ang = TNPlaygroundKit::KitTwoPi * T;
			const double R = CoilR * (1.0 + 0.1 * TNProcMesh::TNProcHashNoise(Loop, 3, Seed));
			const double Lean = ((Loop % 2) == 0 ? 1.0 : -1.0) * 0.22 * R;
			const double Along = FMath::Clamp(-HalfLen + T * Advance + Lean * FMath::Sin(Ang), -HalfLen, HalfLen);
			const double Across = 0.95 * R * FMath::Sin(Ang);
			// Abajo (ángulo 0) toca la arena y se aplasta un poco; arriba (pi), el alto del rollo.
			const double Z = FMath::Max(3.0, CoilR - R * FMath::Cos(Ang));
			Path.Add(FVector(Along, Across, Z));
			Radii.Add(WireRadius);
			const double Patch = TNPlaygroundKit::Hash01(i / 3, 5, Seed);
			Colors.Add(Patch > 0.84 ? Rust : (Patch > 0.6 ? SteelDark : Steel));
		}
		TNPlaygroundKit::AddTube(B, Path, Radii, 4, Colors, FVector::ForwardVector, false);
		// Pinchos cada tercio de rollo.
		for (int32 i = 2; i + 1 < Path.Num(); i += SamplesPerLoop / 3)
		{
			const FVector Tangent = (Path[i + 1] - Path[i - 1]).GetSafeNormal();
			if (!Tangent.IsNearlyZero())
			{
				AddBarb(B, Path[i], Tangent, TNPlaygroundKit::KitTwoPi * TNPlaygroundKit::Hash01(i, 7, Seed), Colors[i]);
			}
		}

		// Estacas (la primera y la última en los extremos) con el hilo tenso de arriba entre ellas.
		const int32 Stakes = FMath::Max(2, FMath::RoundToInt32(2.0 * HalfLen / StakeSpacing) + 1);
		const double TopZ = 2.0 * CoilR + 8.0;
		TArray<FVector> Heads;
		for (int32 s = 0; s < Stakes; ++s)
		{
			const double Along = FMath::Lerp(-HalfLen + 15.0, HalfLen - 15.0, static_cast<double>(s) / (Stakes - 1));
			const double Offset = ((s % 2) == 0 ? 1.0 : -1.0) * CoilR * 0.2;
			const FVector Foot(Along, Offset, -35.0);
			const FVector Head(Along + 10.0 * (TNPlaygroundKit::Hash01(s, 2, Seed) - 0.5), Offset + 12.0 * (TNPlaygroundKit::Hash01(s, 1, Seed) - 0.5), TopZ + 26.0);
			TNPlaygroundKit::AddFrustum(B, Foot, Head, 8.0, 6.0, 7, Wood, WoodDark, false, true);
			const FVector Knot = FMath::Lerp(Foot, Head, (TopZ + 35.0) / (Head.Z - Foot.Z));
			TNPlaygroundKit::AddFrustum(B, Knot - FVector(0.0, 0.0, 5.0), Knot + FVector(0.0, 0.0, 5.0), 9.5, 9.5, 7, Rope, Rope, true, true);
			Heads.Add(FVector(Knot.X, Knot.Y, TopZ));
		}
		for (int32 s = 0; s + 1 < Heads.Num(); ++s)
		{
			// Hilo con un poco de comba y pinchos cada 40 cm.
			const FVector A = Heads[s];
			const FVector C = Heads[s + 1];
			const int32 Steps = FMath::Max(2, FMath::RoundToInt32(FVector::Dist(A, C) / 60.0));
			TArray<FVector> Line;
			for (int32 k = 0; k <= Steps; ++k)
			{
				const double U = static_cast<double>(k) / Steps;
				Line.Add(FMath::Lerp(A, C, U) - FVector(0.0, 0.0, 12.0 * FMath::Sin(TNPlaygroundKit::KitPi * U)));
			}
			const TArray<double> LineRadii = { 2.4 };
			const TArray<FLinearColor> LineColors = { SteelDark };
			TNPlaygroundKit::AddTube(B, Line, LineRadii, 4, LineColors, FVector::UpVector, false);
			const FVector Along = (C - A).GetSafeNormal();
			const int32 Barbs = FMath::Max(1, FMath::RoundToInt32(FVector::Dist(A, C) / 40.0));
			for (int32 k = 1; k < Barbs; ++k)
			{
				const double U = static_cast<double>(k) / Barbs;
				const FVector At = FMath::Lerp(A, C, U) - FVector(0.0, 0.0, 12.0 * FMath::Sin(TNPlaygroundKit::KitPi * U));
				AddBarb(B, At, Along, 1.3 * k, Steel);
			}
		}
		// Un par de trozos de alambre suelto tirados en la arena junto a los extremos.
		for (const double End : { -1.0, 1.0 })
		{
			const FVector Base(End * (HalfLen - 40.0), CoilR * 0.9 * End, 3.0);
			TArray<FVector> Loose;
			for (int32 k = 0; k <= 8; ++k)
			{
				const double U = static_cast<double>(k) / 8.0;
				Loose.Add(Base + FVector(25.0 * FMath::Sin(U * 7.0), End * 60.0 * U, 3.0 + 4.0 * FMath::Sin(U * 11.0)));
			}
			const TArray<double> LooseRadii = { WireRadius };
			const TArray<FLinearColor> LooseColors = { Rust };
			TNPlaygroundKit::AddTube(B, Loose, LooseRadii, 4, LooseColors, FVector::UpVector, false);
		}
	}

	/** Cajas de colisión por tramos a lo largo de X. */
	void BuildHulls(FHulls& Hulls, double HalfLen, double CoilR)
	{
		const int32 Chunks = FMath::Max(1, FMath::RoundToInt32(2.0 * HalfLen / 420.0));
		const double ChunkHalf = HalfLen / Chunks;
		for (int32 c = 0; c < Chunks; ++c)
		{
			const double Xc = -HalfLen + (2 * c + 1) * ChunkHalf;
			Hulls.Add(TNPlaygroundKit::HullAxisBox(FVector(Xc, 0.0, CoilR), FVector(ChunkHalf, CoilR * 0.85, CoilR + CollisionTopPad)));
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// ATN_BeachBarbedWire
// ─────────────────────────────────────────────────────────────────────────────

ATN_BeachBarbedWire::ATN_BeachBarbedWire()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(2.f);

	WireMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WireMesh"));
	WireMesh->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureVisual(WireMesh);

	WireCollision = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WireCollision"));
	WireCollision->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureSolid(WireCollision, false);
	WireCollision->CanCharacterStepUpOn = ECB_No;
}

float ATN_BeachBarbedWire::GetWireLength() const
{
	return Spec.Extent > 1.f ? Spec.Extent : DefaultLength;
}

void ATN_BeachBarbedWire::ApplySpec()
{
	const double Size = FMath::Clamp(static_cast<double>(Spec.SizeScale), 0.5, 1.6);
	HalfLength = FMath::Max(150.0, 0.5 * static_cast<double>(GetWireLength()));
	CoilRadius = FMath::Clamp(66.0 * Size, 52.0, 84.0);
	const uint32 Seed = TNBeachTrapKit::SeedOf(Spec.Seed, 11u);

	TNBeachTrapKit::FBuffers Wire;
	TNBeachBarbedWireDetail::BuildWire(Wire, HalfLength, CoilRadius, Seed);
	TNBeachTrapKit::SetMesh(WireMesh, this, Wire, TN_ART("Beach.BarbedWire.Wire"));

	TNBeachTrapKit::FHulls Hulls;
	TNBeachBarbedWireDetail::BuildHulls(Hulls, HalfLength, CoilRadius);
	WireCollision->SetCollisionConvexMeshes(Hulls);
	UE_LOG(LogTortunabo, Verbose, TEXT("[Playa] Alambre %s: %.0f cm de largo, rollos de %.0f cm."), *GetName(), 2.0 * HalfLength, CoilRadius);
}

void ATN_BeachBarbedWire::BeginPlay()
{
	Super::BeginPlay();
	Voice = UTN_BeachTrapSynthComponent::AttachTo(this, GetActorTransform().TransformPosition(FVector(0.0, 0.0, CoilRadius)), 600.f, 3200.f);
	Sparks.Init(this, ETNTrapBurstShape::Spark, TNPlaygroundKit::Rgb(0xFFF1A8, 0.6f), 28);
	Sparks.SetMotion(-600.f, 2.5f, 18.f, 4.f, 0.12f, 0.32f);
	Bits.Init(this, ETNTrapBurstShape::Chip, TNPlaygroundKit::Rgb(0x8A8F96, 0.4f), 10);
	Bits.SetMotion(-980.f, 0.5f, 14.f, 6.f, 0.4f, 0.7f);
}

void ATN_BeachBarbedWire::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		CheckTouches();
	}
	Sparks.Tick(DeltaSeconds);
	Bits.Tick(DeltaSeconds);
	Ouch.Tick(DeltaSeconds, GetWorld());
}

void ATN_BeachBarbedWire::CheckTouches()
{
	using namespace TNBeachBarbedWireDetail;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	const FTransform ActorXf = GetActorTransform();
	// El alambre va a lo largo de X; se toca por los lados (Y) o desde arriba.
	const double ReachY = CoilRadius * 0.85;
	const double TopZ = 2.0 * CoilRadius + 10.0;
	// Solo quien está al alcance del rollo (en planta, de punta a punta y por los lados).
	const double Reach = (FMath::Sqrt(FMath::Square(HalfLength) + FMath::Square(ReachY + TouchMargin)) + GatherMargin) * ActorXf.GetMaximumAxisScale();
	TArray<ACharacter*> Near;
	TNBeachNearby::Gather(World, ActorXf.GetLocation(), Reach, Near);
	for (ACharacter* Walker : Near)
	{
		if (!TNBeachTrapKit::IsFreeTurtle(Walker))
		{
			continue;
		}
		const UCapsuleComponent* Capsule = Walker->GetCapsuleComponent();
		if (!Capsule)
		{
			continue;
		}
		const FVector Local = ActorXf.InverseTransformPosition(Capsule->GetComponentLocation());
		const double CapR = Capsule->GetScaledCapsuleRadius();
		const double CapHalf = Capsule->GetScaledCapsuleHalfHeight();
		if (FMath::Abs(Local.X) > HalfLength + CapR * 0.5 || FMath::Abs(Local.Y) > ReachY + CapR + TouchMargin
			|| Local.Z - CapHalf > TopZ || Local.Z + CapHalf < -30.0)
		{
			continue;
		}
		const TWeakObjectPtr<ACharacter> Key(Walker);
		if (const double* Last = LastHit.Find(Key))
		{
			if (Now - *Last < StunSeconds + HitCooldown)
			{
				continue;
			}
		}
		LastHit.Add(Key, Now);

		// Hacia el lado del que venía: el de su posición o, si ya está encima, el contrario a su marcha.
		double Side = Local.Y >= 0.0 ? 1.0 : -1.0;
		if (FMath::Abs(Local.Y) < ReachY * 0.35)
		{
			const FVector LocalVel = ActorXf.InverseTransformVectorNoScale(Walker->GetVelocity());
			Side = LocalVel.Y > 0.0 ? -1.0 : 1.0;
		}
		const FVector Away = ActorXf.TransformVectorNoScale(FVector(0.0, Side, 0.0)).GetSafeNormal2D();
		TNBeach::StunTurtle(Walker, StunSeconds, Away * PushSpeed + FVector::UpVector * PushUp);
		const FVector Contact = ActorXf.TransformPosition(FVector(Local.X, Side * ReachY, FMath::Clamp(Local.Z, 20.0, TopZ - 10.0)));
		MulticastZapFX(Contact, Walker);
		ForceNetUpdate();
		UE_LOG(LogTortunabo, Verbose, TEXT("[Playa] Alambre %s pincha a %s."), *GetName(), *Walker->GetName());
	}
	for (auto It = LastHit.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || Now - It.Value() > 30.0)
		{
			It.RemoveCurrent();
		}
	}
}

void ATN_BeachBarbedWire::MulticastZapFX_Implementation(FVector_NetQuantize WorldAt, APawn* Victim)
{
	PlayZapFX(WorldAt, Victim);
}

void ATN_BeachBarbedWire::PlayZapFX(const FVector& WorldAt, const APawn* Victim)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	Sparks.Burst(WorldAt, 22, FVector::UpVector, 650.f, 1.3f, 12.f);
	Bits.Burst(WorldAt, 5, FVector::UpVector, 350.f, 1.f, 8.f);
	if (Voice)
	{
		Voice->TriggerSoundAt(ETNBeachTrapSound::Zap, WorldAt, FMath::FRandRange(0.9f, 1.15f), 1.f);
		Voice->TriggerSound(ETNBeachTrapSound::Ouch, FMath::FRandRange(0.92f, 1.12f), 0.9f);
	}
	const FVector TextAt = Victim ? Victim->GetActorLocation() + FVector(0.0, 0.0, 150.0) : WorldAt + FVector(0.0, 0.0, 120.0);
	Ouch.Show(this, NSLOCTEXT("TNBeach", "BarbedWireOuch", "¡AY!"), FColor(255, 86, 60), TextAt, 110.f);
}
