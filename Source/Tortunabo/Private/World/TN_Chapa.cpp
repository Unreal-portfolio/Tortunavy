// La chapa: moneda física de la partida que se lanza como un frisbee (#858). Ver TN_Chapa.h.

#include "World/TN_Chapa.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TN_Log.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/TN_ChapaRules.h"
#include "Game/TN_ItemRuntime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Player/TN_InventoryComponent.h"
#include "Player/TortugaCharacter.h"
#include "Settings/TN_EconomySettings.h"
#include "UObject/ConstructorHelpers.h"

namespace TNChapaDetail
{
	/** Medio grosor (cm) de la chapa tumbada: lo que queda por encima del suelo. */
	constexpr double RestLift = 1.5;
	/** Cada cuánto (s) mira el servidor si alguien pasa por encima. */
	constexpr float CollectScanSeconds = 0.2f;
	/** Diferencia de altura (cm) entre la chapa y el centro de la tortuga para recogerla. */
	constexpr double CollectHeight = 150.0;
	/** Salto de las chapas que se sueltan (SpawnChapas): hacia arriba y hacia fuera (cm/s). */
	constexpr float PopUpMin = 380.f;
	constexpr float PopUpMax = 520.f;
	constexpr float PopOutMin = 120.f;
	constexpr float PopOutMax = 260.f;
	/** Color de la chapa mientras no tenga arte: latón. */
	const FLinearColor CapColor(0.86f, 0.62f, 0.12f);
}

ATN_Chapa::ATN_Chapa()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	// Cada máquina pone el arco desde el vuelo replicado: sin movimiento replicado.
	SetReplicateMovement(false);
	SetNetCullDistanceSquared(FMath::Square(15000.f));

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetGenerateOverlapEvents(false);
	RootComponent = Mesh;
	// Falta el arte: cilindro del motor aplastado (12 cm de diámetro y 2,5 de grosor).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		Mesh->SetStaticMesh(Cylinder.Object);
	}
	Mesh->SetRelativeScale3D(FVector(0.12, 0.12, 0.025));
}

void ATN_Chapa::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_Chapa, Flight);
}

void ATN_Chapa::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() != NM_DedicatedServer && Mesh)
	{
		if (UMaterialInstanceDynamic* Material = Mesh->CreateDynamicMaterialInstance(0))
		{
			Material->SetVectorParameterValue(TEXT("Color"), TNChapaDetail::CapColor);
		}
	}
	LastClientPoint = Flight.Origin;
	ApplyFlightPose(FlightSeconds());
}

double ATN_Chapa::FlightSeconds() const
{
	return FMath::Max(0.0, TNItemRuntime::ServerNow(GetWorld()) - static_cast<double>(Flight.StartTime));
}

void ATN_Chapa::ApplyFlightPose(double Seconds)
{
	if (Flight.bResting)
	{
		SetActorLocationAndRotation(Flight.RestLocation, FRotator(0.f, Flight.RestYaw, 0.f));
		return;
	}
	const double T = FMath::Min(Seconds, static_cast<double>(MaxFlightSeconds));
	const FVector Point = TNChapaRules::FlightPoint(Flight.Origin, Flight.Velocity, Flight.Gravity, T);
	const FVector Velocity = TNChapaRules::FlightVelocity(Flight.Velocity, Flight.Gravity, T);
	SetActorLocationAndRotation(Point, TNChapaRules::DiscRotation(Velocity, SpinDegPerSecond * T));
}

void ATN_Chapa::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bConsumed)
	{
		return;
	}
	if (Flight.bResting)
	{
		if (!HasAuthority())
		{
			SetActorTickEnabled(false);
			return;
		}
		CollectScanClock -= DeltaSeconds;
		if (CollectScanClock <= 0.f)
		{
			CollectScanClock = TNChapaDetail::CollectScanSeconds;
			ServerScanCollectors();
		}
		return;
	}
	const double T = FlightSeconds();
	if (HasAuthority())
	{
		ServerStepFlight(SteppedSeconds, T);
		return;
	}
	if (bClientFrozen)
	{
		return;
	}
	// Cliente: si el arco choca antes de que llegue dónde queda, se para ahí (solo se ve; lo que cuenta es del servidor).
	const FVector Next = TNChapaRules::FlightPoint(Flight.Origin, Flight.Velocity, Flight.Gravity, FMath::Min(T, static_cast<double>(MaxFlightSeconds)));
	FHitResult Hit;
	if (SweepWorld(LastClientPoint, Next, Hit))
	{
		bClientFrozen = true;
		SetActorLocation(Hit.Location);
		return;
	}
	LastClientPoint = Next;
	ApplyFlightPose(T);
}

void ATN_Chapa::ServerStepFlight(double FromSeconds, double ToSeconds)
{
	if (!HasAuthority() || Flight.bResting || bConsumed)
	{
		return;
	}
	const double To = FMath::Min(ToSeconds, static_cast<double>(MaxFlightSeconds));
	const double From = FMath::Clamp(FromSeconds, 0.0, To);
	const FVector A = TNChapaRules::FlightPoint(Flight.Origin, Flight.Velocity, Flight.Gravity, From);
	const FVector B = TNChapaRules::FlightPoint(Flight.Origin, Flight.Velocity, Flight.Gravity, To);
	SteppedSeconds = To;

	FHitResult Hit;
	if (SweepWorld(A, B, Hit))
	{
		// En el suelo se queda donde cae; contra una pared, resbala hasta el suelo de debajo.
		const bool bFloor = TNChapaRules::IsRestingSurface(Hit.ImpactNormal);
		ServerRestAt(bFloor ? FVector(Hit.Location) : FVector(Hit.Location) + Hit.ImpactNormal * CollisionRadius);
		return;
	}
	ApplyFlightPose(To);
	if (To >= MaxFlightSeconds)
	{
		ServerRestAt(B);
	}
}

bool ATN_Chapa::SweepWorld(const FVector& A, const FVector& B, FHitResult& OutHit) const
{
	const UWorld* World = GetWorld();
	if (!World || A.Equals(B, 0.01))
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ChapaFlight), false, this);
	if (const AActor* ThrowerActor = Thrower.Get())
	{
		Params.AddIgnoredActor(ThrowerActor);
	}
	// Solo el escenario y lo que hace de pared: atraviesa tortugas y cuerpos sueltos.
	FCollisionResponseParams Response;
	Response.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
	Response.CollisionResponse.SetResponse(ECC_PhysicsBody, ECR_Ignore);
	return World->SweepSingleByChannel(OutHit, A, B, FQuat::Identity, ECC_WorldDynamic, FCollisionShape::MakeSphere(CollisionRadius), Params, Response);
}

FVector ATN_Chapa::GroundUnder(const FVector& Where) const
{
	const UWorld* World = GetWorld();
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ChapaGround), false, this);
	FCollisionResponseParams Response;
	Response.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
	Response.CollisionResponse.SetResponse(ECC_PhysicsBody, ECR_Ignore);
	if (World && World->LineTraceSingleByChannel(Hit, Where + FVector(0.0, 0.0, 30.0), Where - FVector(0.0, 0.0, 5000.0), ECC_WorldStatic, Params, Response))
	{
		return Hit.ImpactPoint;
	}
	return Where;
}

void ATN_Chapa::ServerRestAt(const FVector& Where)
{
	if (!HasAuthority() || bConsumed)
	{
		return;
	}
	Flight.bResting = true;
	Flight.RestLocation = GroundUnder(Where) + FVector(0.0, 0.0, TNChapaDetail::RestLift);
	Flight.RestYaw = static_cast<float>(GetActorRotation().Yaw);
	RestServerTime = TNItemRuntime::ServerNow(GetWorld());
	CollectScanClock = 0.f;
	ApplyFlightPose(0.0);
	ForceNetUpdate();
}

void ATN_Chapa::OnRep_Flight()
{
	if (Flight.bResting)
	{
		bClientFrozen = false;
		ApplyFlightPose(0.0);
		SetActorTickEnabled(false);
		return;
	}
	LastClientPoint = Flight.Origin;
	ApplyFlightPose(FlightSeconds());
}

void ATN_Chapa::MulticastConsumed_Implementation()
{
	bConsumed = true;
	SetActorHiddenInGame(true);
	if (HasAuthority())
	{
		SetLifeSpan(0.2f);
	}
}

void ATN_Chapa::ServerScanCollectors()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const FVector Here = Flight.RestLocation;
	for (TActorIterator<ATortugaCharacter> It(World); It; ++It)
	{
		ATortugaCharacter* Turtle = *It;
		const FVector Delta = Turtle->GetActorLocation() - Here;
		if (Delta.Size2D() <= CollectRadius && FMath::Abs(Delta.Z) <= TNChapaDetail::CollectHeight && ServerTryCollect(Turtle))
		{
			return;
		}
	}
}

bool ATN_Chapa::ServerTryCollect(ATortugaCharacter* Turtle)
{
	if (!HasAuthority() || !Flight.bResting || bConsumed || !IsValid(Turtle) || Turtle->IsDead() || Turtle->IsKnockedDown())
	{
		return false;
	}
	if (Turtle == Thrower.Get() && TNItemRuntime::ServerNow(GetWorld()) - RestServerTime < ThrowerPickupDelaySeconds)
	{
		return false;
	}
	UTN_InventoryComponent* Inventory = Turtle->GetInventoryComponent();
	if (!Inventory || TNChapaRules::AcceptedChapas(Inventory->GetChapaCount(), Value, UTN_EconomySettings::Get().MaxChapas) < Value)
	{
		return false;
	}
	Inventory->AddChapas(Value);
	bConsumed = true;
	PlayChapaSound(Turtle, CollectSound, false);
	UE_LOG(LogTortunabo, Log, TEXT("[Chapas] %s recoge una chapa (lleva %d)."), *GetNameSafe(Turtle), Inventory->GetChapaCount());
	Destroy();
	return true;
}

void ATN_Chapa::PlayChapaSound(ATortugaCharacter* Turtle, USoundBase* Sound, bool bThrow) const
{
	if (!Turtle)
	{
		return;
	}
	if (Sound)
	{
		Turtle->MulticastPlaySfx(Sound);
		return;
	}
	TNItemRuntime::PlayCue(Turtle, bThrow ? ETNRaceSound::Throw : ETNRaceSound::Catch, bThrow ? 1.4f : 1.8f);
}

ATN_Chapa* ATN_Chapa::SpawnFlying(UWorld* World, const FVector& Origin, const FVector& Velocity, ATortugaCharacter* InThrower)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.Owner = InThrower;
	Params.Instigator = InThrower;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	const FTransform Start(FRotator::ZeroRotator, Origin);
	ATN_Chapa* Chapa = World->SpawnActor<ATN_Chapa>(ATN_Chapa::StaticClass(), Start, Params);
	if (!Chapa)
	{
		return nullptr;
	}
	// En el primer paquete: los clientes nacen ya con el vuelo.
	Chapa->Flight.Origin = Origin;
	Chapa->Flight.Velocity = Velocity;
	Chapa->Flight.StartTime = static_cast<float>(TNItemRuntime::ServerNow(World));
	Chapa->Flight.Gravity = Chapa->FlightGravity;
	Chapa->Thrower = InThrower;
	Chapa->Value = UTN_EconomySettings::Get().ChapaValue;
	Chapa->FinishSpawning(Start);
	return Chapa;
}

ATN_Chapa* ATN_Chapa::ServerThrowFrom(ATortugaCharacter* InThrower)
{
	UWorld* World = InThrower ? InThrower->GetWorld() : nullptr;
	if (!World || !InThrower->HasAuthority())
	{
		return nullptr;
	}
	UTN_InventoryComponent* Inventory = InThrower->GetInventoryComponent();
	if (!Inventory || Inventory->GetChapaCount() <= 0 || !TNItemRuntime::CanUseNow(InThrower))
	{
		TNItemRuntime::PlayCue(InThrower, ETNRaceSound::Nope);
		return nullptr;
	}
	const ATN_Chapa* Defaults = GetDefault<ATN_Chapa>();
	const FVector Origin = InThrower->GetItemSpawnLocation();
	// Al centro de la pantalla con el arco justo para llegar (con la gravedad de la chapa): el arco es predecible.
	const FVector Direction = InThrower->GetThrowDirectionToCrosshair(Origin, InThrower->GetTurtleAimRotation(), Defaults->ThrowSpeed,
		Defaults->FlightGravity);
	if (!Inventory->TrySpendChapas(1))
	{
		return nullptr;
	}
	ATN_Chapa* Chapa = SpawnFlying(World, Origin, Direction * Defaults->ThrowSpeed, InThrower);
	if (!Chapa)
	{
		Inventory->AddChapas(1);
		return nullptr;
	}
	Chapa->PlayChapaSound(InThrower, Chapa->ThrowSound, true);
	InThrower->MulticastItemThrowAnim();
	UE_LOG(LogTortunabo, Log, TEXT("[Chapas] %s lanza una chapa (le quedan %d)."), *GetNameSafe(InThrower), Inventory->GetChapaCount());
	return Chapa;
}

int32 ATN_Chapa::SpawnChapas(UObject* WorldContextObject, FVector Where, int32 Count)
{
	using namespace TNChapaDetail;
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World || World->GetNetMode() == NM_Client || Count <= 0)
	{
		return 0;
	}
	int32 Spawned = 0;
	const float StartAngle = FMath::FRandRange(0.f, 360.f);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		// Repartidas en corona para que no caigan una encima de otra.
		const float Angle = FMath::DegreesToRadians(StartAngle + 360.f * Index / Count + FMath::FRandRange(-15.f, 15.f));
		const float Out = FMath::FRandRange(PopOutMin, PopOutMax);
		const FVector Velocity(FMath::Cos(Angle) * Out, FMath::Sin(Angle) * Out, FMath::FRandRange(PopUpMin, PopUpMax));
		if (SpawnFlying(World, Where + FVector(0.0, 0.0, 20.0), Velocity, nullptr))
		{
			++Spawned;
		}
	}
	return Spawned;
}
