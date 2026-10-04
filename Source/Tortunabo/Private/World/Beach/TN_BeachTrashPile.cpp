#include "World/Beach/TN_BeachTrashPile.h"

#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "ProceduralMeshComponent.h"
#include "TN_BeachTrapKit.h"
#include "World/Beach/TN_BeachCreatureRules.h"

namespace TNBeachTrashDetail
{
	/** Montón de basura de radio R y alto H: bolsa, cajas, latas y una botella (colores de juguete desteñidos). */
	void BuildPile(TNBeachTrapKit::FBuffers& B, double R, double H, uint32 Seed)
	{
		const FLinearColor Bag = TNPlaygroundKit::Rgb(0x2F3A33, 0.4f);
		TNPlaygroundKit::AddEllipsoid(B, FVector(0.0, 0.0, H * 0.4), FVector::ForwardVector, FVector::RightVector, FVector::UpVector,
			FVector(R * 0.55, R * 0.45, H * 0.55), 10, 5, Bag);
		for (int32 k = 0; k < 4; ++k)
		{
			const double A = TNPlaygroundKit::KitTwoPi * (k + TNPlaygroundKit::Hash01(k, 1, Seed)) / 4.0;
			const FVector At(FMath::Cos(A) * R * 0.55, FMath::Sin(A) * R * 0.55, 0.0);
			const FLinearColor Toy = TNPlaygroundKit::ToyColor(static_cast<int32>(Seed + k), 0.2f);
			if (k % 2 == 0)
			{
				const FTransform Xf(FRotator(0.0, FMath::RadiansToDegrees(A) + 25.0, 0.0), At + FVector(0.0, 0.0, H * 0.25));
				TNPlaygroundKit::AddXfBox(B, Xf, FVector::ZeroVector, FVector(R * 0.22, R * 0.16, H * 0.25), TNPlaygroundKit::Shade(Toy, 0.85));
			}
			else
			{
				// Lata tumbada.
				const FVector Dir(FMath::Cos(A + 1.2), FMath::Sin(A + 1.2), 0.0);
				TNPlaygroundKit::AddFrustum(B, At - Dir * R * 0.16 + FVector(0.0, 0.0, R * 0.09), At + Dir * R * 0.16 + FVector(0.0, 0.0, R * 0.09),
					R * 0.09, R * 0.09, 10, Toy, TNPlaygroundKit::Rgb(0xC8CCD0, 0.6f), true, true);
			}
		}
		// Botella de cristal verde apoyada encima.
		TNPlaygroundKit::AddFrustum(B, FVector(-R * 0.2, R * 0.1, H * 0.75), FVector(R * 0.25, -R * 0.05, H * 0.9), R * 0.08, R * 0.035, 8,
			TNPlaygroundKit::Rgb(0x4E8F5A, 0.7f), TNPlaygroundKit::Rgb(0x4E8F5A, 0.7f), true, true);
	}

	/** Restos aplastados: manchas planas a ras de arena. */
	void BuildScraps(TNBeachTrapKit::FBuffers& B, double R, uint32 Seed)
	{
		for (int32 k = 0; k < 6; ++k)
		{
			const double A = TNPlaygroundKit::KitTwoPi * TNPlaygroundKit::Hash01(k, 4, Seed);
			const double D = R * (0.2 + 0.8 * TNPlaygroundKit::Hash01(k, 5, Seed));
			const FTransform Xf(FRotator(0.0, FMath::RadiansToDegrees(A), 0.0), FVector(FMath::Cos(A) * D, FMath::Sin(A) * D, 2.0));
			TNPlaygroundKit::AddXfBox(B, Xf, FVector::ZeroVector, FVector(R * 0.12, R * 0.08, 2.0), TNPlaygroundKit::ToyColor(static_cast<int32>(Seed + k), 0.1f));
		}
	}
}

ATN_BeachTrashPile::ATN_BeachTrashPile()
{
	bUsesMover = false;

	PileMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PileMesh"));
	PileMesh->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureVisual(PileMesh);

	ScrapMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ScrapMesh"));
	ScrapMesh->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureVisual(ScrapMesh);
	ScrapMesh->SetVisibility(false);

	PileCollision = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("PileCollision"));
	PileCollision->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureSolid(PileCollision, false);
}

void ATN_BeachTrashPile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_BeachTrashPile, bBroken);
}

void ATN_BeachTrashPile::ApplySpec()
{
	using namespace TNBeachTrashDetail;
	PileRadius = 0.8 * TNBeachTrapKit::FitRadius(Spec.Element, Spec.SizeScale);
	PileHeight = FMath::Clamp(PileRadius * 0.3, 45.0, 90.0);
	const uint32 Seed = TNBeachTrapKit::SeedOf(Spec.Seed, 690u);
	TNBeachTrapKit::FBuffers Pile;
	BuildPile(Pile, PileRadius, PileHeight, Seed);
	TNBeachTrapKit::SetMesh(PileMesh, this, Pile);
	TNBeachTrapKit::FBuffers Scraps;
	BuildScraps(Scraps, PileRadius, Seed);
	TNBeachTrapKit::SetMesh(ScrapMesh, this, Scraps);
	TNBeachTrapKit::FHulls Hulls;
	Hulls.Add(TNPlaygroundKit::HullCylinder(FVector::ZeroVector, PileHeight, PileRadius * 0.6, PileRadius * 0.3, 10));
	PileCollision->SetCollisionConvexMeshes(Hulls);
	if (bBroken)
	{
		ApplyBrokenLocal();
	}
}

bool ATN_BeachTrashPile::GetHitCapsule(FVector& OutA, FVector& OutB, float& OutRadius) const
{
	if (bBroken)
	{
		return false;
	}
	OutA = GetActorLocation() + FVector(0.0, 0.0, PileHeight * 0.4);
	OutB = OutA;
	OutRadius = static_cast<float>(PileRadius * 0.6);
	return true;
}

void ATN_BeachTrashPile::ApplyHitStun(float Seconds, AActor* InstigatorActor)
{
	// Un golpe (objeto lanzado o bola de caparazón deprisa: los únicos que llaman aquí) lo rompe.
	if (!HasAuthority() || bBroken || !TNBeachCreatureRules::TrashPile::BreaksFrom(true, false, 0.f))
	{
		return;
	}
	bBroken = true;
	ForceNetUpdate();
	OnRep_Broken();
}

void ATN_BeachTrashPile::OnRep_Broken()
{
	if (bBroken)
	{
		ApplyBrokenLocal();
	}
}

void ATN_BeachTrashPile::ApplyBrokenLocal()
{
	if (bBrokenApplied)
	{
		return;
	}
	bBrokenApplied = true;
	PileMesh->SetVisibility(false);
	ScrapMesh->SetVisibility(true);
	PileCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (GetNetMode() == NM_DedicatedServer || !HasActorBegunPlay())
	{
		return;
	}
	const FVector At = GetActorLocation() + FVector(0.0, 0.0, PileHeight * 0.5);
	if (BreakSound) { UGameplayStatics::SpawnSoundAtLocation(this, BreakSound, At); }
	if (BreakVFX) { UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, BreakVFX, At); }
	Chips.Init(this, ETNTrapBurstShape::Chip, TNPlaygroundKit::Rgb(0x8C9A7A, 0.2f), 24);
	Chips.SetMotion(-1600.f, 1.2f, 28.f, 8.f, 0.5f, 0.9f);
	Chips.Burst(At, 24, FVector::UpVector, 700.f, 1.2f, static_cast<float>(PileRadius * 0.4));
}

void ATN_BeachTrashPile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Chips.IsLive())
	{
		Chips.Tick(DeltaSeconds);
	}
}

void ATN_BeachTrashPile::ServerTick(float DeltaSeconds)
{
	if (bBroken)
	{
		return;
	}
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	TArray<ATortugaCharacter*> Turtles;
	GatherTurtles(this, Turtles);
	for (ATortugaCharacter* Turtle : Turtles)
	{
		const FVector Delta = GetActorLocation() - Turtle->GetActorLocation();
		const double Reach = PileRadius * 0.6 + Turtle->GetSimpleCollisionRadius() + 40.0;
		if (FVector2D(Delta.X, Delta.Y).SizeSquared() > Reach * Reach || FMath::Abs(Delta.Z) > 300.0)
		{
			continue;
		}
		if (const double* Until = TripCooldown.Find(Turtle); Until && Now < *Until)
		{
			continue;
		}
		const FVector Dir = Delta.GetSafeNormal2D();
		const float Toward = static_cast<float>(FVector::DotProduct(Turtle->GetVelocity(), Dir));
		if (!TNBeachCreatureRules::TrashPile::Trips(Toward, bBroken, TripSpeed) || !TNBeachTrapKit::IsFreeTurtle(Turtle))
		{
			continue;
		}
		TripCooldown.Add(Turtle, Now + 2.0);
		// Tropezón: cae hacia delante, por encima del montón.
		TNBeach::KnockDownTurtle(Turtle, TripSeconds, Dir * 300.f + FVector(0.0, 0.0, 250.f));
	}
}
