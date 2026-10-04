#include "World/Beach/TN_BeachTrapCommon.h"
#include "Multiplayer/TN_LocalViews.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/Package.h"
#include "TN_BeachTrapKit.h"

/** Mallas de las partículas y azar barato de los estallidos. */
namespace TNBeachTrapCommonDetail
{
	uint32 NextRand(uint32& State)
	{
		State ^= State << 13;
		State ^= State >> 17;
		State ^= State << 5;
		return State;
	}

	float Rand01(uint32& State)
	{
		return static_cast<float>(NextRand(State) & 0xFFFFFF) / 16777215.f;
	}

	/**
	 * Malla de 100 cm de una forma de partícula con su color, en caché por forma y color. Las mallas de la caché quedan en
	 * la raíz del recolector (se reutilizan entre rondas y partidas; el motor las libera al cerrarse).
	 */
	UStaticMesh* ShapeMesh(ETNTrapBurstShape InShape, const FLinearColor& Color)
	{
		static TMap<uint64, UStaticMesh*> Cache;
		const FColor Q = Color.ToFColor(false);
		const uint64 Key = (static_cast<uint64>(InShape) << 40) ^ (static_cast<uint64>(Q.R) << 16) ^ (static_cast<uint64>(Q.G) << 8) ^ Q.B
			^ (static_cast<uint64>(FMath::RoundToInt32(Color.A * 63.f)) << 24);
		if (UStaticMesh** Found = Cache.Find(Key))
		{
			return *Found;
		}
		TNBeachTrapKit::FBuffers B;
		switch (InShape)
		{
		case ETNTrapBurstShape::Spark:
		{
			// Rombo alargado a lo largo de X (vuela de punta).
			const FVector P[6] = { FVector(50, 0, 0), FVector(-50, 0, 0), FVector(0, 16, 0), FVector(0, -16, 0), FVector(0, 0, 16), FVector(0, 0, -16) };
			const int32 F[8][3] = { { 0, 2, 4 }, { 2, 1, 4 }, { 1, 3, 4 }, { 3, 0, 4 }, { 2, 0, 5 }, { 1, 2, 5 }, { 3, 1, 5 }, { 0, 3, 5 } };
			for (const auto& Tri : F)
			{
				B.AddTri(P[Tri[0]], P[Tri[1]], P[Tri[2]], (P[Tri[0]] + P[Tri[1]] + P[Tri[2]]) / 3.0, Color);
			}
			break;
		}
		case ETNTrapBurstShape::Chip:
		{
			// Esquirla: lámina irregular con las dos caras.
			const FVector A(-50, -22, 0), Bp(38, -30, 0), C(50, 18, 0), D(-30, 30, 0);
			B.AddQuad(A, Bp, C, D, FVector::UpVector, Color);
			B.AddQuad(A, Bp, C, D, -FVector::UpVector, TNPlaygroundKit::Shade(Color, 0.8));
			break;
		}
		default:
			TNPlaygroundKit::AddBall(B, FVector::ZeroVector, 50.0, 8, Color);
			break;
		}
		UStaticMesh* Mesh = TNPlaygroundKit::BuildMesh(GetTransientPackage(), B, TNPlaygroundKit::VertexColorMaterial());
		if (Mesh)
		{
			Mesh->AddToRoot();
		}
		Cache.Add(Key, Mesh);
		return Mesh;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// FTNTrapClock
// ─────────────────────────────────────────────────────────────────────────────

double FTNTrapClock::Advance(const UWorld* World, float DeltaSeconds)
{
	return AdvanceTo(TNBeachTrapKit::ServerNow(World), DeltaSeconds);
}

double FTNTrapClock::AdvanceTo(double Target, float DeltaSeconds)
{
	// Avanza con el fotograma y se acerca poco a poco a la hora del servidor: sin saltos cuando esta se corrige.
	if (!bValid || FMath::Abs(Target - Clock) > 1.0)
	{
		Clock = Target;
		bValid = true;
	}
	else
	{
		Clock += DeltaSeconds;
		Clock += (Target - Clock) * FMath::Min(1.0, static_cast<double>(DeltaSeconds) * 1.5);
	}
	return Clock;
}

// ─────────────────────────────────────────────────────────────────────────────
// FTNTrapBurst
// ─────────────────────────────────────────────────────────────────────────────

void FTNTrapBurst::Init(AActor* InOwner, ETNTrapBurstShape InShape, const FLinearColor& InColor, int32 InMaxParticles)
{
	Shape = InShape;
	const int32 MaxCount = FMath::Clamp(InMaxParticles, 1, 128);
	Particles.SetNum(MaxCount);
	Xf.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), MaxCount);
	UWorld* World = InOwner ? InOwner->GetWorld() : nullptr;
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer || ISM.IsValid())
	{
		return;
	}
	UInstancedStaticMeshComponent* Comp = NewObject<UInstancedStaticMeshComponent>(InOwner, NAME_None, RF_Transient);
	Comp->SetStaticMesh(TNBeachTrapCommonDetail::ShapeMesh(InShape, InColor));
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetCastShadow(false);
	Comp->bEvaluateWorldPositionOffset = false;
	Comp->SetMobility(EComponentMobility::Movable);
	if (USceneComponent* OwnerRoot = InOwner->GetRootComponent())
	{
		Comp->SetupAttachment(OwnerRoot);
	}
	Comp->RegisterComponent();
	Comp->SetAbsolute(true, true, true);
	Comp->SetWorldTransform(FTransform::Identity);
	Comp->AddInstances(Xf, false, false);
	ISM = Comp;
	Rng ^= (GetTypeHash(InOwner->GetFName()) * 2654435761u) | 1u;
}

void FTNTrapBurst::SetMotion(float InGravity, float InDrag, float InSizeStart, float InSizeEnd, float InLifeMin, float InLifeMax)
{
	Gravity = InGravity;
	Drag = FMath::Max(0.f, InDrag);
	SizeStart = InSizeStart;
	SizeEnd = InSizeEnd;
	LifeMin = FMath::Max(0.05f, InLifeMin);
	LifeMax = FMath::Max(LifeMin, InLifeMax);
}

void FTNTrapBurst::Burst(const FVector& WorldAt, int32 Count, const FVector& Dir, float Speed, float Spread, float Radius)
{
	if (!ISM.IsValid() || Count <= 0)
	{
		return;
	}
	FVector Axis = Dir.GetSafeNormal();
	if (Axis.IsNearlyZero())
	{
		Axis = FVector::UpVector;
	}
	const FVector U = FVector::CrossProduct(Axis, FMath::Abs(Axis.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
	const FVector W = FVector::CrossProduct(Axis, U);
	int32 Spawned = 0;
	for (FParticle& Pt : Particles)
	{
		if (Spawned >= Count)
		{
			break;
		}
		if (Pt.bAlive)
		{
			continue;
		}
		const float A = TNBeachTrapCommonDetail::Rand01(Rng) * 6.2831853f;
		const float R = Radius * FMath::Sqrt(TNBeachTrapCommonDetail::Rand01(Rng));
		Pt.P = WorldAt + (U * FMath::Cos(A) + W * FMath::Sin(A)) * R;
		const float Around = TNBeachTrapCommonDetail::Rand01(Rng) * 6.2831853f;
		const float Open = Spread * FMath::Sqrt(TNBeachTrapCommonDetail::Rand01(Rng));
		const FVector V = (Axis + (U * FMath::Cos(Around) + W * FMath::Sin(Around)) * Open).GetSafeNormal();
		Pt.V = V * Speed * FMath::Lerp(0.55f, 1.2f, TNBeachTrapCommonDetail::Rand01(Rng));
		Pt.Age = 0.f;
		Pt.Life = FMath::Lerp(LifeMin, LifeMax, TNBeachTrapCommonDetail::Rand01(Rng));
		Pt.Spin = TNBeachTrapCommonDetail::Rand01(Rng) * 360.f;
		Pt.bAlive = true;
		++Spawned;
	}
	bLive = true;
}

bool FTNTrapBurst::Tick(float DeltaSeconds)
{
	UInstancedStaticMeshComponent* Comp = ISM.Get();
	if (!bLive || !Comp)
	{
		return false;
	}
	bool bAny = false;
	const float DragK = FMath::Max(0.f, 1.f - Drag * DeltaSeconds);
	for (int32 i = 0; i < Particles.Num(); ++i)
	{
		FParticle& Pt = Particles[i];
		if (Pt.bAlive)
		{
			Pt.Age += DeltaSeconds;
			if (Pt.Age >= Pt.Life)
			{
				Pt.bAlive = false;
			}
		}
		if (!Pt.bAlive)
		{
			Xf[i].SetScale3D(FVector::ZeroVector);
			continue;
		}
		bAny = true;
		Pt.V.Z += Gravity * DeltaSeconds;
		Pt.V *= DragK;
		Pt.P += Pt.V * DeltaSeconds;
		const float T = Pt.Age / Pt.Life;
		const float Scale = FMath::Lerp(SizeStart, SizeEnd, T) / 100.f;
		FQuat Rot = FQuat(FVector::UpVector, FMath::DegreesToRadians(Pt.Spin + Pt.Age * 90.f));
		FVector Scale3D(Scale);
		if (Shape == ETNTrapBurstShape::Spark && !Pt.V.IsNearlyZero())
		{
			// Las chispas vuelan de punta y se estiran con la velocidad.
			Rot = FQuat::FindBetweenNormals(FVector::ForwardVector, Pt.V.GetSafeNormal());
			Scale3D.X *= FMath::Clamp(static_cast<float>(Pt.V.Size()) / 400.f, 0.8f, 2.6f);
		}
		else if (Shape == ETNTrapBurstShape::Chip)
		{
			Rot = FQuat(FVector(1.f, 0.3f, 0.2f).GetSafeNormal(), FMath::DegreesToRadians(Pt.Spin + Pt.Age * 520.f));
		}
		Xf[i] = FTransform(Rot, Pt.P, Scale3D);
	}
	// Cada fotograma, sin MarkRenderStateDirty: TransformChanged ya actualiza instancias y límites al final del fotograma sin rehacer el proxy (#566).
	Comp->BatchUpdateInstancesTransforms(0, Xf, true, false, false);
	bLive = bAny;
	return bAny;
}

// ─────────────────────────────────────────────────────────────────────────────
// FTNTrapPopText
// ─────────────────────────────────────────────────────────────────────────────

void FTNTrapPopText::Show(AActor* InOwner, const FText& InText, const FColor& InColor, const FVector& WorldAt, float WorldSize)
{
	UWorld* World = InOwner ? InOwner->GetWorld() : nullptr;
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	UTextRenderComponent* Text = Comp.Get();
	if (!Text)
	{
		Text = NewObject<UTextRenderComponent>(InOwner, NAME_None, RF_Transient);
		Text->SetHorizontalAlignment(EHTA_Center);
		Text->SetVerticalAlignment(EVRTA_TextCenter);
		Text->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Text->SetCastShadow(false);
		if (USceneComponent* OwnerRoot = InOwner->GetRootComponent())
		{
			Text->SetupAttachment(OwnerRoot);
		}
		Text->RegisterComponent();
		Text->SetAbsolute(true, true, true);
		Comp = Text;
	}
	Text->SetText(InText);
	Text->SetTextRenderColor(InColor);
	Text->SetWorldSize(WorldSize);
	Text->SetWorldLocation(WorldAt);
	Text->SetVisibility(true);
	Start = WorldAt;
	Size = WorldSize;
	Age = 0.f;
}

bool FTNTrapPopText::Tick(float DeltaSeconds, const UWorld* World)
{
	UTextRenderComponent* Text = Comp.Get();
	if (!Text || Age > 5.f)
	{
		return false;
	}
	Age += DeltaSeconds;
	constexpr float Life = 0.95f;
	if (Age >= Life)
	{
		Text->SetVisibility(false);
		Age = 10.f;
		return false;
	}
	// Sale de golpe algo más grande, se asienta y sube frenando; al final encoge.
	const float Pop = Age < 0.1f ? FMath::Lerp(0.3f, 1.3f, Age / 0.1f) : FMath::Lerp(1.3f, 1.f, FMath::Min(1.f, (Age - 0.1f) / 0.15f));
	const float Shrink = FMath::Clamp((Life - Age) / 0.2f, 0.f, 1.f);
	const FVector At = Start + FVector(0.0, 0.0, 110.0 * (1.0 - FMath::Exp(-Age * 3.0)));
	FRotator Facing = FRotator::ZeroRotator;
	FVector CameraAt = At;
	if (TNLocalViews::ClosestCamera(World, At, CameraAt))
	{
		Facing = (CameraAt - At).Rotation();
	}
	Facing.Roll = 8.f * FMath::Sin(Age * 22.f) * FMath::Exp(-Age * 3.f);
	Text->SetWorldLocationAndRotation(At, Facing);
	Text->SetWorldScale3D(FVector(Pop * Shrink));
	return true;
}
