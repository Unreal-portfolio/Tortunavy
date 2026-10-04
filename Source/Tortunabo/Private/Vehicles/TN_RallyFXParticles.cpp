#include "Vehicles/TN_RallyFXParticles.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

namespace TNRallyParticlesDetail
{
	using TNAmbientFX::EShape;
	using TNAmbientFX::FEmitterDesc;

	/** Radio de referencia de las ráfagas (cm): las partículas escalan con el radio de cada impacto. */
	constexpr float ReferenceRadiusCm = 100.f;

	const FLinearColor ShellBrown(0.42f, 0.25f, 0.11f);
	const FLinearColor Sand(0.9f, 0.8f, 0.6f);
	const FLinearColor Fire(1.f, 0.55f, 0.12f);
	const FLinearColor Smoke(0.35f, 0.32f, 0.3f);
	const FLinearColor InkDark(0.06f, 0.03f, 0.12f);
	const FLinearColor Bubble(0.6f, 0.88f, 1.f);
	const FLinearColor Spark(1.f, 0.8f, 0.3f);

	FEmitterDesc Base(EShape Shape, const FLinearColor& Color, int32 MaxParticles, float Speed, float SizeStart, float SizeEnd)
	{
		FEmitterDesc D;
		D.Shape = Shape;
		D.Color = Color;
		D.MaxParticles = MaxParticles;
		D.Rate = 0.f;
		D.Speed = Speed;
		D.SpeedJitter = 0.4f;
		D.Spread = 1.f;
		D.SizeStart = SizeStart;
		D.SizeEnd = SizeEnd;
		D.WakeDistance = 15000.f;
		return D;
	}

	/** Nube blanda (polvo, humo, arena) que se abre y frena. */
	FEmitterDesc Cloud(const FLinearColor& Color, int32 Count, float Speed, float SizeStart, float SizeEnd, float Alpha)
	{
		FEmitterDesc D = Base(EShape::Puff, Color, Count, Speed, SizeStart, SizeEnd);
		D.bSoft = true;
		D.bCloud = true;
		D.Alpha = Alpha;
		D.Gravity = -40.f;
		D.Buoyancy = 30.f;
		D.Drag = 2.2f;
		D.LifeMin = 0.6f;
		D.LifeMax = 1.1f;
		return D;
	}

	/** Trozos sólidos que caen (cáscara de coco, granos de arena). */
	FEmitterDesc Chunks(const FLinearColor& Color, int32 Count, float Speed, float Size)
	{
		FEmitterDesc D = Base(EShape::Ember, Color, Count, Speed, Size, Size * 0.8f);
		D.Gravity = -980.f;
		D.Drag = 0.4f;
		D.LifeMin = 0.5f;
		D.LifeMax = 0.9f;
		return D;
	}

	/** Gotas translúcidas estiradas en su vuelo (tinta, agua, burbuja). */
	FEmitterDesc Drops(const FLinearColor& Color, int32 Count, float Speed, float Size, float Alpha)
	{
		FEmitterDesc D = Base(EShape::Drop, Color, Count, Speed, Size, Size * 0.6f);
		D.bSoft = true;
		D.Alpha = Alpha;
		D.Gravity = -900.f;
		D.Drag = 0.3f;
		D.LifeMin = 0.45f;
		D.LifeMax = 0.8f;
		return D;
	}

	TNRallyParticles::FBurstLayer Layer(const FEmitterDesc& Desc, int32 Count)
	{
		TNRallyParticles::FBurstLayer Out;
		Out.Desc = Desc;
		Out.Count = Count;
		return Out;
	}
}

TArray<TNRallyParticles::FBurstLayer> TNRallyParticles::LayersFor(ETNRallyBurstKind Kind, float RadiusCm)
{
	using namespace TNRallyParticlesDetail;
	const float Scale = FMath::Clamp(RadiusCm / ReferenceRadiusCm, 0.3f, 4.f);
	TArray<FBurstLayer> Layers;
	switch (Kind)
	{
	case ETNRallyBurstKind::CocoHit:
		Layers.Add(Layer(Chunks(ShellBrown, 16, 520.f, 9.f), 14));
		Layers.Add(Layer(Cloud(Sand, 8, 160.f, 20.f * Scale, 70.f * Scale, 0.5f), 7));
		break;
	case ETNRallyBurstKind::Explosion:
	{
		FEmitterDesc Embers = Chunks(Fire, 28, 900.f, 10.f);
		Embers.bSoft = true;
		Embers.Alpha = 0.95f;
		Embers.Gravity = -500.f;
		Layers.Add(Layer(Embers, 26));
		Layers.Add(Layer(Cloud(Smoke, 14, 320.f * Scale, 60.f * Scale, 220.f * Scale, 0.55f), 14));
		Layers.Add(Layer(Chunks(Sand, 20, 700.f, 7.f), 18));
		break;
	}
	case ETNRallyBurstKind::Ink:
		Layers.Add(Layer(Drops(InkDark, 28, 520.f, 12.f, 0.9f), 26));
		Layers.Add(Layer(Cloud(InkDark, 6, 120.f, 30.f, 110.f, 0.6f), 6));
		break;
	case ETNRallyBurstKind::BubblePop:
	case ETNRallyBurstKind::Shield:
	{
		Layers.Add(Layer(Drops(Bubble, 18, 380.f, 7.f, 0.6f), 16));
		FEmitterDesc Ring = Base(EShape::Ring, Bubble, 2, 0.f, 40.f * Scale, 260.f * Scale);
		Ring.bSoft = true;
		Ring.Alpha = 0.6f;
		Ring.Gravity = 0.f;
		Ring.LifeMin = 0.45f;
		Ring.LifeMax = 0.55f;
		Layers.Add(Layer(Ring, 1));
		break;
	}
	case ETNRallyBurstKind::Sand:
		Layers.Add(Layer(Cloud(Sand, 22, 450.f, 60.f, 260.f, 0.6f), 22));
		Layers.Add(Layer(Chunks(Sand, 24, 800.f, 6.f), 24));
		break;
	case ETNRallyBurstKind::MuzzleFlash:
		Layers.Add(Layer(Cloud(Smoke, 6, 140.f, 8.f, 45.f, 0.35f), 5));
		break;
	case ETNRallyBurstKind::Sparks:
	{
		FEmitterDesc Streaks = Base(EShape::Streak, Spark, 20, 1100.f, 14.f, 6.f);
		Streaks.bSoft = true;
		Streaks.Alpha = 1.f;
		Streaks.Gravity = -1200.f;
		Streaks.Drag = 0.8f;
		Streaks.LifeMin = 0.2f;
		Streaks.LifeMax = 0.4f;
		Layers.Add(Layer(Streaks, 18));
		Layers.Add(Layer(Cloud(Sand, 6, 160.f, 15.f, 60.f, 0.45f), 5));
		break;
	}
	default:
		break;
	}
	return Layers;
}

bool TNRallyParticles::KeepsSphere(ETNRallyBurstKind Kind)
{
	return Kind == ETNRallyBurstKind::Shield || Kind == ETNRallyBurstKind::MuzzleFlash;
}

TNAmbientFX::FEmitter* TNRallyParticles::AddEmitter(AActor* Owner, FEmitterSet& Set, const TNAmbientFX::FEmitterDesc& Desc, const FVector& Origin)
{
	const UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	TNAmbientFX::FEmitter& Emitter = Set.Emitters.AddDefaulted_GetRef();
	Emitter.Desc = Desc;
	Emitter.Origin = Origin;
	Emitter.RateScale = 0.f;
	Emitter.Rng ^= (static_cast<uint32>(GetTypeHash(Origin)) + static_cast<uint32>(Set.Emitters.Num()) * 2654435761u) | 1u;
	Emitter.Particles.SetNum(Desc.MaxParticles);
	Emitter.Xf.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), Desc.MaxParticles);
	Emitter.ISM = TNAmbientFX::MakeISM(Owner, TNAmbientFX::ShapeMesh(Desc.Shape, Desc.Color, Desc.bSoft, Desc.Alpha, Desc.bCloud),
		Desc.MaxParticles, false);
	return &Emitter;
}

void TNRallyParticles::BurstAt(TNAmbientFX::FEmitter& Emitter, const FVector& Origin, const FVector& Direction, int32 Count)
{
	const FVector SavedOrigin = Emitter.Origin;
	const FVector SavedDirection = Emitter.Desc.Direction;
	Emitter.Origin = Origin;
	Emitter.Desc.Direction = Direction.IsNearlyZero() ? FVector::UpVector : Direction;
	TNAmbientFX::Burst(Emitter, Count);
	Emitter.Origin = SavedOrigin;
	Emitter.Desc.Direction = SavedDirection;
}

float TNRallyParticles::SpawnBurst(AActor* Owner, FEmitterSet& Set, ETNRallyBurstKind Kind, const FVector& Where, float RadiusCm)
{
	float MaxLife = 0.f;
	for (const FBurstLayer& BurstLayer : LayersFor(Kind, RadiusCm))
	{
		TNAmbientFX::FEmitter* Emitter = AddEmitter(Owner, Set, BurstLayer.Desc, Where);
		if (!Emitter)
		{
			return 0.f;
		}
		BurstAt(*Emitter, Where, FVector::UpVector, BurstLayer.Count);
		MaxLife = FMath::Max(MaxLife, BurstLayer.Desc.LifeMax);
	}
	return MaxLife;
}

bool TNRallyParticles::Tick(FEmitterSet& Set, float Dt, const FVector& View)
{
	bool bAnyAlive = false;
	for (TNAmbientFX::FEmitter& Emitter : Set.Emitters)
	{
		TNAmbientFX::TickEmitter(Emitter, Dt, View);
		for (const TNAmbientFX::FParticle& Particle : Emitter.Particles)
		{
			if (Particle.bAlive)
			{
				bAnyAlive = true;
				break;
			}
		}
	}
	return bAnyAlive;
}

FVector TNRallyParticles::LocalView(const UWorld* World, const FVector& Fallback)
{
	const APlayerCameraManager* Camera = World ? UGameplayStatics::GetPlayerCameraManager(World, 0) : nullptr;
	return Camera ? Camera->GetCameraLocation() : Fallback;
}
