#include "World/Beach/TN_BeachBunker.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"
#include "TN_BeachTrapKit.h"
#include "World/Beach/TN_BeachCreatureRules.h"
#include "World/Beach/TN_BeachShelterVolume.h"

ATN_BeachBunker::ATN_BeachBunker()
{
	PrimaryActorTick.bCanEverTick = false;

	BunkerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BunkerMesh"));
	BunkerMesh->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureVisual(BunkerMesh);

	BunkerCollision = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BunkerCollision"));
	BunkerCollision->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureSolid(BunkerCollision, true);
}

void ATN_BeachBunker::ApplySpec()
{
	const double Fit = TNBeachTrapKit::FitRadius(Spec.Element, Spec.SizeScale);
	constexpr double Wall = 40.0;
	constexpr double Roof = 45.0;
	InteriorHalf = FVector(FMath::Max(200.0, 0.45 * Fit), FMath::Max(160.0, 0.33 * Fit), 240.0);
	DoorHalfWidth = 95.f;
	DoorHeight = 190.f;

	TNBeachCreatureRules::Shelter::FBunkerDims Dims;
	Dims.Interior = InteriorHalf;
	Dims.Wall = Wall;
	Dims.DoorHalf = DoorHalfWidth;
	Dims.DoorHeight = DoorHeight;
	Dims.Roof = Roof;
	const TArray<TNBeachCreatureRules::Shelter::FBunkerBox> Boxes = TNBeachCreatureRules::Shelter::BunkerBoxes(Dims);
	const FLinearColor Concrete = TNPlaygroundKit::Rgb(0x9A968C, 0.08f);
	const FLinearColor Dark = TNPlaygroundKit::Rgb(0x26231F);
	TNBeachTrapKit::FBuffers B;
	TNBeachTrapKit::FHulls Hulls;
	for (int32 i = 0; i < Boxes.Num(); ++i)
	{
		const TNBeachCreatureRules::Shelter::FBunkerBox& Box = Boxes[i];
		TNPlaygroundKit::AddAxisBox(B, Box.Center, Box.Half, i == Boxes.Num() - 1 ? TNPlaygroundKit::Shade(Concrete, 1.08) : Concrete);
		Hulls.Add(TNPlaygroundKit::HullAxisBox(Box.Center, Box.Half));
	}
	// Tronera hacia el mar, suelo de hormigón y unas manchas de camuflaje en el techo.
	TNPlaygroundKit::AddAxisBox(B, FVector(InteriorHalf.X + Wall + 1.0, 0.0, InteriorHalf.Z * 0.62), FVector(2.0, InteriorHalf.Y * 0.5, 14.0), Dark);
	TNPlaygroundKit::AddAxisBox(B, FVector(0.0, 0.0, 1.5), FVector(InteriorHalf.X, InteriorHalf.Y, 1.5), TNPlaygroundKit::Shade(Concrete, 0.8));
	const uint32 Seed = TNBeachTrapKit::SeedOf(Spec.Seed, 689u);
	for (int32 k = 0; k < 3; ++k)
	{
		const FVector At((TNPlaygroundKit::Hash01(k, 1, Seed) - 0.5) * InteriorHalf.X, (TNPlaygroundKit::Hash01(k, 2, Seed) - 0.5) * InteriorHalf.Y,
			InteriorHalf.Z + Roof + 4.0);
		TNPlaygroundKit::AddEllipsoid(B, At, FVector::ForwardVector, FVector::RightVector, FVector::UpVector, FVector(90.0, 70.0, 18.0), 8, 3,
			TNPlaygroundKit::Rgb(k % 2 ? 0x4E5A2E : 0x6B5A3A, 0.05f));
	}
	TNBeachTrapKit::SetMesh(BunkerMesh, this, B);
	BunkerCollision->SetCollisionConvexMeshes(Hulls);

	UWorld* World = GetWorld();
	if (World && World->IsGameWorld())
	{
		if (!IsValid(Shelter))
		{
			Shelter = ATN_BeachShelterVolume::SpawnLocal(World, GetActorTransform(), FVector(InteriorHalf.X, InteriorHalf.Y, InteriorHalf.Z * 0.5), this);
		}
		else
		{
			Shelter->SetActorTransform(GetActorTransform());
			Shelter->SetShelterExtent(FVector(InteriorHalf.X, InteriorHalf.Y, InteriorHalf.Z * 0.5));
		}
	}
}

void ATN_BeachBunker::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(Shelter))
	{
		Shelter->Destroy();
	}
	Shelter = nullptr;
	Super::EndPlay(EndPlayReason);
}
