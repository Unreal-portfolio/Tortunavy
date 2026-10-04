#include "World/Beach/TN_BeachTankTrap.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TN_Log.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_ShellBody.h"
#include "Player/TN_ShellComponent.h"
#include "ProceduralMeshComponent.h"
#include "TN_BeachTrapKit.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_RallyCombatLogic.h"
#include "World/Beach/TN_BeachCreatureRules.h"

namespace TNBeachTankTrapDetail
{
	/** Distancia (cm) entre la cápsula y el radio del sitio a la que cuenta como choque. */
	constexpr double ContactSlack = 60.0;
	constexpr double CooldownSeconds = 1.5;
	/** Radio (cm) de la bola de caparazón para el choque (el de ATN_BeachEnemy::ServerPushShellBalls). */
	constexpr double BallRadiusCm = 25.0;

	/** Las tres vigas de un erizo checo (un «jack»): cruzadas en el centro, a la altura del radio. */
	TArray<FTransform> BeamFrames(double HalfLen)
	{
		const double Z = HalfLen * 0.62;
		return {
			FTransform(FRotator(35.0, 0.0, 0.0), FVector(0.0, 0.0, Z)),
			FTransform(FRotator(35.0, 120.0, 0.0), FVector(0.0, 0.0, Z)),
			FTransform(FRotator(35.0, 240.0, 0.0), FVector(0.0, 0.0, Z)),
		};
	}

	void BuildHog(TNBeachTrapKit::FBuffers& B, TNBeachTrapKit::FHulls& Hulls, double HalfLen)
	{
		const FLinearColor Steel = TNPlaygroundKit::Rgb(0x5A5F63, 0.35f);
		const FLinearColor Rust = TNPlaygroundKit::Rgb(0x8A4B2A, 0.15f);
		const double Half = FMath::Max(9.0, HalfLen * 0.09);
		for (const FTransform& Xf : BeamFrames(HalfLen))
		{
			TNPlaygroundKit::AddXfBox(B, Xf, FVector::ZeroVector, FVector(HalfLen, Half, Half), Steel);
			// Remaches oxidados en las puntas.
			TNPlaygroundKit::AddXfBox(B, Xf, FVector(HalfLen * 0.92, 0.0, Half), FVector(HalfLen * 0.06, Half * 1.1, Half * 0.25), Rust);
			TNPlaygroundKit::AddXfBox(B, Xf, FVector(-HalfLen * 0.92, 0.0, Half), FVector(HalfLen * 0.06, Half * 1.1, Half * 0.25), Rust);
			Hulls.Add(TNPlaygroundKit::HullBox(Xf, FVector::ZeroVector, FVector(HalfLen, Half, Half)));
		}
	}
}

ATN_BeachTankTrap::ATN_BeachTankTrap()
{
	PrimaryActorTick.bCanEverTick = true;

	HogMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HogMesh"));
	HogMesh->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureVisual(HogMesh);

	HogCollision = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("HogCollision"));
	HogCollision->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureSolid(HogCollision, false);
}

ATN_BeachTankTrap* ATN_BeachTankTrap::SpawnGuard(UWorld* World, const TArray<FVector4>& InSpots)
{
	if (!World || InSpots.Num() == 0)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	ATN_BeachTankTrap* Guard = World->SpawnActor<ATN_BeachTankTrap>(ATN_BeachTankTrap::StaticClass(), FTransform::Identity, Params);
	if (!Guard)
	{
		return nullptr;
	}
	// Solo del servidor: ni malla ni réplica (el decorado de los erizos ya lo monta cada máquina).
	Guard->bGuard = true;
	Guard->Spots = InSpots;
	Guard->Spec.Element = ETNBeachElement::TankTrap;
	Guard->SetReplicates(false);
	Guard->FinishSpawning(FTransform::Identity);
	UE_LOG(LogTortunabo, Log, TEXT("[Playa] Erizos checos: vigilante con %d erizos."), InSpots.Num());
	return Guard;
}

ATN_BeachTankTrap* ATN_BeachTankTrap::SpawnStandalone(UWorld* World, const FTransform& Transform, float Radius)
{
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	ATN_BeachTankTrap* Hog = World->SpawnActor<ATN_BeachTankTrap>(ATN_BeachTankTrap::StaticClass(), Transform, Params);
	if (!Hog)
	{
		return nullptr;
	}
	Hog->Spec.Element = ETNBeachElement::TankTrap;
	Hog->Spec.SizeScale = static_cast<float>(Radius / TNBeach::FootprintRadius(ETNBeachElement::TankTrap));
	Hog->ApplyRoundNetProfile();
	Hog->FinishSpawning(Transform);
	return Hog;
}

void ATN_BeachTankTrap::ApplySpec()
{
	if (bGuard)
	{
		return;
	}
	StandaloneRadius = static_cast<float>(TNBeachTrapKit::FitRadius(Spec.Element, Spec.SizeScale));
	TNBeachTrapKit::FBuffers B;
	TNBeachTrapKit::FHulls Hulls;
	TNBeachTankTrapDetail::BuildHog(B, Hulls, StandaloneRadius);
	TNBeachTrapKit::SetMesh(HogMesh, this, B);
	HogCollision->SetCollisionConvexMeshes(Hulls);
}

void ATN_BeachTankTrap::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UWorld* World = GetWorld();
	if (!World || GetNetMode() == NM_Client)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	TArray<FImpactor> Impactors;
	GatherImpactors(Impactors);
	for (const FImpactor& Who : Impactors)
	{
		if (bGuard)
		{
			for (const FVector4& Spot : Spots)
			{
				CheckImpact(Who, FVector(Spot.X, Spot.Y, Spot.Z), static_cast<float>(Spot.W), Now);
			}
		}
		else
		{
			// La colisión es de las vigas: el choque cuenta en la mitad de su largo.
			CheckImpact(Who, GetActorLocation(), StandaloneRadius * 0.55f, Now);
		}
		LastVelocity.Add(Who.Actor, Who.Velocity);
	}
}

void ATN_BeachTankTrap::GatherImpactors(TArray<FImpactor>& Out) const
{
	using TNBeachCreatureRules::TankTrap::EBody;
	const FVector Flat(1.0, 1.0, 0.0);
	TArray<ATortugaCharacter*> Turtles;
	ATN_BeachEnemy::GatherTurtles(this, Turtles);
	for (ATortugaCharacter* Turtle : Turtles)
	{
		if (TNBeachTrapKit::IsFreeTurtle(Turtle))
		{
			Out.Add({ Turtle, EBody::Walker, Turtle->GetActorLocation(), Turtle->GetVelocity() * Flat });
			continue;
		}
		// En su bola: cuenta su cuerpo físico, suelto (ni mareada ya, ni sujeta, ni en una boca, ni recolocada, ni en brazos).
		if (Turtle->IsDead() || Turtle->IsKnockedDown() || !Turtle->IsInShell() || TNBeach::IsTurtleStunned(Turtle)
			|| TNBeach::IsTurtleRelocating(Turtle) || ATN_BeachEnemy::IsTurtleHeld(Turtle) || ATN_BeachSandWorm::IsBeingEaten(Turtle))
		{
			continue;
		}
		const UTN_CarryComponent* Carry = Turtle->GetCarryComponent();
		const UTN_ShellComponent* Shell = Turtle->GetShellComponent();
		const ATN_ShellBody* Body = Shell ? Shell->GetBody() : nullptr;
		const UBoxComponent* Box = Body ? Body->GetBox() : nullptr;
		if ((Carry && (Carry->GetCarrier() != nullptr || Carry->IsCarrying())) || !Box || !Box->IsSimulatingPhysics())
		{
			continue;
		}
		Out.Add({ Turtle, EBody::Ball, Box->GetComponentLocation(), Box->GetComponentVelocity() * Flat });
	}
	// Los buggies, también los de la IA (no tienen estado de jugador).
	for (TActorIterator<ATN_Buggy> It(GetWorld()); It; ++It)
	{
		ATN_Buggy* Buggy = *It;
		if (IsValid(Buggy) && !Buggy->IsActorBeingDestroyed())
		{
			Out.Add({ Buggy, EBody::Buggy, Buggy->GetActorLocation(), Buggy->GetVelocity() * Flat });
		}
	}
}

double ATN_BeachTankTrap::ReachOf(const FImpactor& Who, const FVector& Dir)
{
	using TNBeachCreatureRules::TankTrap::EBody;
	if (Who.Body == EBody::Buggy)
	{
		const FVector Local = Who.Actor->GetActorTransform().InverseTransformVectorNoScale(Dir);
		return TNBeachCreatureRules::TankTrap::BuggyReachCm(Local, TNRallyCombat::DefaultHalfLengthCm, TNRallyCombat::DefaultHalfWidthCm);
	}
	if (Who.Body == EBody::Ball)
	{
		return TNBeachTankTrapDetail::BallRadiusCm;
	}
	return Who.Actor->GetSimpleCollisionRadius();
}

void ATN_BeachTankTrap::CheckImpact(const FImpactor& Who, const FVector& Center, float Radius, double Now)
{
	using namespace TNBeachCreatureRules::TankTrap;
	const FVector Delta = Center - Who.Location;
	const FVector Dir = Delta.GetSafeNormal2D();
	const double Reach = Radius + ReachOf(Who, Dir) + TNBeachTankTrapDetail::ContactSlack;
	if (FVector2D(Delta.X, Delta.Y).SizeSquared() > Reach * Reach || FMath::Abs(Delta.Z) > 400.0)
	{
		return;
	}
	if (const double* Until = CooldownUntil.Find(Who.Actor); Until && Now < *Until)
	{
		return;
	}
	const FVector* Prev = LastVelocity.Find(Who.Actor);
	const float Toward = static_cast<float>(FMath::Max(FVector::DotProduct(Who.Velocity, Dir), Prev ? FVector::DotProduct(*Prev, Dir) : 0.0));
	const EResponse Response = ResponseFor(Who.Body, Toward, KnockSpeed);
	if (Response == EResponse::None)
	{
		return;
	}
	CooldownUntil.Add(Who.Actor, Now + TNBeachTankTrapDetail::CooldownSeconds);
	ApplyResponse(Who, Response, Dir);
	if (!bGuard)
	{
		MulticastClang(Who.Location);
	}
	else if (ClangSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, ClangSound, Who.Location);
	}
}

void ATN_BeachTankTrap::ApplyResponse(const FImpactor& Who, TNBeachCreatureRules::TankTrap::EResponse Response, const FVector& Dir)
{
	using TNBeachCreatureRules::TankTrap::EResponse;
	switch (Response)
	{
	case EResponse::KnockDownWalker:
		TNBeach::KnockDownTurtle(Cast<ACharacter>(Who.Actor), KnockSeconds, -Dir * BounceBack + FVector(0.0, 0.0, BounceUp));
		break;
	case EResponse::StunBall:
		// La bola sale rebotada y la tortuga se queda mareada dentro, como la que lanza una ola o un enemigo.
		TNBeach::StunTurtle(Cast<ACharacter>(Who.Actor), KnockSeconds, -Dir * BounceBack + FVector(0.0, 0.0, BounceUp));
		break;
	case EResponse::BounceBuggy:
		if (ATN_Buggy* Buggy = Cast<ATN_Buggy>(Who.Actor))
		{
			// El chasis rebota (la colisión ya lo ha parado) y la dirección se bambolea (el escudo de burbuja se lo ahorra).
			Buggy->ApplyVelocityImpulse(-Dir * BounceBack + FVector(0.0, 0.0, BuggyBounceUp));
			Buggy->ApplyCocoHit(FVector::ZeroVector);
		}
		break;
	default:
		break;
	}
}

void ATN_BeachTankTrap::MulticastClang_Implementation(FVector_NetQuantize At)
{
	if (ClangSound && GetNetMode() != NM_DedicatedServer)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, ClangSound, At);
	}
}
