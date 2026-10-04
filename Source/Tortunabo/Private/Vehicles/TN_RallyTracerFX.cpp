#include "Vehicles/TN_RallyTracerFX.h"
#include "Vehicles/TN_RallyProjectile.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "UObject/ConstructorHelpers.h"

namespace TNRallyTracer
{
	/** Vida del trazador (s): lo justo para ver hacia dónde sale antes de que llegue el proyectil del servidor. */
	constexpr float LifeSeconds = 0.35f;
	/** Largo y grosor de la estela (cm); el largo crece con la velocidad hasta MaxLengthCm. */
	constexpr float ThicknessCm = 7.f;
	constexpr float LengthPerSpeed = 0.04f;
	constexpr float MinLengthCm = 40.f;
	constexpr float MaxLengthCm = 220.f;
	/** Las mallas básicas miden 100 cm. */
	constexpr float BasicShapeSizeCm = 100.f;
	/** Fracción final de la vida en que se adelgaza hasta desaparecer. */
	constexpr float FadeFraction = 0.4f;
}

ATN_RallyTracerFX::ATN_RallyTracerFX()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	SetCanBeDamaged(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetStaticMesh(SphereMesh.Object);
	Mesh->SetMaterial(0, ShapeMaterial.Object);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->SetGenerateOverlapEvents(false);
	RootComponent = Mesh;
}

ATN_RallyTracerFX* ATN_RallyTracerFX::Spawn(UWorld* World, ETNRallyAmmo Ammo, const FVector& Muzzle, const FVector& InVelocity, float InGravityZ)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer || !FApp::CanEverRender() || InVelocity.IsNearlyZero())
	{
		return nullptr;
	}
	const FTransform Where(InVelocity.Rotation(), Muzzle);
	ATN_RallyTracerFX* Tracer = World->SpawnActorDeferred<ATN_RallyTracerFX>(StaticClass(), Where, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Tracer)
	{
		return nullptr;
	}
	Tracer->Velocity = InVelocity;
	Tracer->GravityZ = InGravityZ;
	Tracer->FinishSpawning(Where);
	Tracer->SetLifeSpan(TNRallyTracer::LifeSeconds);
	TNRallyLook::Tint(Tracer->Mesh, TNRallyLook::AmmoColor(Ammo));
	Tracer->ApplyPose();
	return Tracer;
}

void ATN_RallyTracerFX::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	Velocity.Z += GravityZ * DeltaSeconds;
	SetActorLocation(GetActorLocation() + Velocity * DeltaSeconds);
	ApplyPose();
}

void ATN_RallyTracerFX::ApplyPose()
{
	using namespace TNRallyTracer;
	if (Velocity.IsNearlyZero())
	{
		return;
	}
	SetActorRotation(Velocity.Rotation());
	const float Length = FMath::Clamp(static_cast<float>(Velocity.Size()) * LengthPerSpeed, MinLengthCm, MaxLengthCm);
	const float FadeStart = LifeSeconds * (1.f - FadeFraction);
	const float Fade = Age <= FadeStart ? 1.f : FMath::Clamp(1.f - (Age - FadeStart) / (LifeSeconds - FadeStart), 0.05f, 1.f);
	const float Thick = ThicknessCm * Fade / BasicShapeSizeCm;
	Mesh->SetRelativeScale3D(FVector(Length / BasicShapeSizeCm, Thick, Thick));
}
