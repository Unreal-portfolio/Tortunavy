#include "Kart/TN_KartTraversalComponent.h"

#include "../World/ProcMap/TN_ProcMapAmbientFX.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Rally/TN_RallyLogic.h"
#include "Vehicles/TN_Buggy.h"
#include "World/ProcMap/TN_ProcMapGenerator.h"
#include "World/ProcMap/TN_ProcTraversalActors.h"

namespace TNKart
{
	FVector GeyserLaunchVelocity(const FVector& Start, const FVector& Target, float ApexExtra, float GravityCms2)
	{
		const double Gravity = FMath::Max(1.0, static_cast<double>(GravityCms2));
		const double Apex = FMath::Max(Start.Z, Target.Z) + FMath::Max(0.0, static_cast<double>(ApexExtra));
		const double Vz = FMath::Sqrt(2.0 * Gravity * FMath::Max(0.0, Apex - Start.Z));
		const double TimeUp = Vz / Gravity;
		const double TimeDown = FMath::Sqrt(FMath::Max(0.0, 2.0 * (Apex - Target.Z) / Gravity));
		const FVector Flat(Target.X - Start.X, Target.Y - Start.Y, 0.0);
		return Flat / FMath::Max(0.1, TimeUp + TimeDown) + FVector(0.0, 0.0, Vz);
	}

	float GeyserFlightSeconds(const FVector& Start, const FVector& Target, float ApexExtra, float GravityCms2)
	{
		const double Gravity = FMath::Max(1.0, static_cast<double>(GravityCms2));
		const double Apex = FMath::Max(Start.Z, Target.Z) + FMath::Max(0.0, static_cast<double>(ApexExtra));
		const double TimeUp = FMath::Sqrt(2.0 * FMath::Max(0.0, Apex - Start.Z) / Gravity);
		const double TimeDown = FMath::Sqrt(FMath::Max(0.0, 2.0 * (Apex - Target.Z) / Gravity));
		return static_cast<float>(FMath::Max(0.1, TimeUp + TimeDown));
	}

	float BuoyancyAccel(float SubmersionCm, float VerticalSpeedCms, float GravityCms2)
	{
		// A ras de agua sostiene justo el peso; 40 cm más hondo, 2,5 veces; 40 cm por encima, nada. Amortigua el rebote.
		const float Lift = GravityCms2 * (1.f + FMath::Clamp(SubmersionCm / 40.f, -1.f, 1.5f));
		return Lift - 2.5f * VerticalSpeedCms;
	}

	float AdvanceFold(float Fold01, bool bTarget, float RatePerSecond, float DeltaSeconds)
	{
		const float Step = FMath::Max(0.f, RatePerSecond) * FMath::Max(0.f, DeltaSeconds);
		return FMath::Clamp(Fold01 + (bTarget ? Step : -Step), 0.f, 1.f);
	}
}

namespace TNKartTraversalDetail
{
	/** Por encima de esto sobre la línea de flotación se deja de flotar; por debajo de lo otro se empieza (cm). */
	constexpr float LeaveWaterAboveCm = 120.f;
	constexpr float EnterWaterAboveCm = 30.f;
	/** Ruedas: plegado por segundo, giro hacia abajo (grados) y cuánto suben y se meten hacia dentro (cm). */
	constexpr float FoldRate = 3.f;
	constexpr float FoldAngleDeg = 85.f;
	constexpr float FoldUpCm = 28.f;
	constexpr float FoldInCm = 22.f;
	/** Salida del agua: con tierra a menos de esto por debajo del agua delante del kart, ayuda a subir la orilla. */
	constexpr float ShoreProbeAheadCm = 260.f;
	constexpr float ShoreClimbUpCms2 = 520.f;
	constexpr float ShoreClimbForwardCms2 = 320.f;
	/** El aterrizaje del géiser, algo por encima de la cima (cm): el kart cae sobre sus ruedas. */
	constexpr float GeyserLandingLiftCm = 80.f;
	/** Segundos tras el lanzamiento del géiser en que el kart se mantiene derecho en el aire. */
	constexpr double GeyserLevelSeconds = 2.5;

	/** Índices de los emisores de efectos del kart (TNAmbientFX). */
	enum EKartFx : int32 { FxSpray = 0, FxWake = 1, FxRing = 2 };
}

UTN_KartTraversalComponent::UTN_KartTraversalComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UTN_KartTraversalComponent::BeginPlay()
{
	Super::BeginPlay();
	CacheMapActors();
}

void UTN_KartTraversalComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bFxBuilt && GetOwner())
	{
		TNAmbientFX::RemoveOwner(GetOwner());
		bFxBuilt = false;
	}
	Super::EndPlay(EndPlayReason);
}

ATN_Buggy* UTN_KartTraversalComponent::GetKart() const
{
	return Cast<ATN_Buggy>(GetOwner());
}

void UTN_KartTraversalComponent::CacheMapActors()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!Generator.IsValid())
	{
		for (TActorIterator<ATN_ProcMapGenerator> It(World); It; ++It)
		{
			Generator = *It;
			break;
		}
	}
	const ATN_ProcMapGenerator* Map = Generator.Get();
	if (!Map || !Map->IsMapReady() || Map->GetBuiltGeneration() == CachedGeneration)
	{
		return;
	}
	// El generador crea los géiseres y los toboganes en cada máquina al generar: se buscan de nuevo con cada mapa.
	CachedGeneration = Map->GetBuiltGeneration();
	Geysers.Reset();
	Slides.Reset();
	for (TActorIterator<ATN_ProcGeyser> It(World); It; ++It)
	{
		Geysers.Add(*It);
	}
	for (TActorIterator<ATN_ProcSlideZone> It(World); It; ++It)
	{
		Slides.Add(*It);
	}
}

bool UTN_KartTraversalComponent::FindWaterSurface(const FVector& Location, float& OutSurfaceZ) const
{
	// Pozas de las cascadas (agua por encima del mar).
	for (const TWeakObjectPtr<ATN_ProcSlideZone>& Weak : Slides)
	{
		FVector Center;
		float Radius = 0.f;
		const ATN_ProcSlideZone* Slide = Weak.Get();
		if (Slide && Slide->GetPool(Center, Radius) && FVector::Dist2D(Center, Location) < Radius
			&& Location.Z < Center.Z + 300.f && Location.Z > Center.Z - 400.f)
		{
			OutSurfaceZ = static_cast<float>(Center.Z);
			return true;
		}
	}
	// El mar (y sus canales y lagunas): agua a la cota del mapa donde el fondo queda lo bastante hondo.
	const ATN_ProcMapGenerator* Map = Generator.Get();
	if (!Map || !Map->IsMapReady())
	{
		return false;
	}
	const float SeaZ = Map->GetSeaLevelWorldZ();
	if (SeaZ - Map->GetTerrainHeightAt(Location) < MinWaterDepthCm)
	{
		return false;
	}
	OutSurfaceZ = SeaZ;
	return true;
}

void UTN_KartTraversalComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ATN_Buggy* Kart = GetKart();
	if (!Kart || DeltaTime <= 0.f)
	{
		return;
	}
	CacheMapActors();
	const FVector Location = Kart->GetActorLocation();
	float SurfaceZ = 0.f;
	const bool bWater = FindWaterSurface(Location, SurfaceZ);
	const bool bWasFloating = bFloating;
	const float AboveFloatLine = static_cast<float>(Location.Z) - (SurfaceZ + FloatLineCm);
	bFloating = bWater && AboveFloatLine < (bFloating ? TNKartTraversalDetail::LeaveWaterAboveCm : TNKartTraversalDetail::EnterWaterAboveCm);
	if (bFloating != bWasFloating && Kart->HasAuthority())
	{
		UE_LOG(LogTNRally, Verbose, TEXT("[Karts] %s %s en (%.0f, %.0f, %.0f)."), *Kart->GetName(), bFloating ? TEXT("flota") : TEXT("sale del agua"),
			Location.X, Location.Y, Location.Z);
	}

	// La física, donde se simula el chasis: el servidor y la conductora local.
	if (Kart->HasAuthority() || Kart->IsLocallyControlled())
	{
		if (bFloating)
		{
			ApplyRaft(DeltaTime, SurfaceZ);
		}
		ApplySlide(DeltaTime);
		GuideGeyserFlight();
		LevelAfterGeyser(DeltaTime);
	}
	TryGeyserLaunch();
	if (GetNetMode() != NM_DedicatedServer)
	{
		UpdateWaterVisuals(DeltaTime, bWasFloating, SurfaceZ);
	}
}

void UTN_KartTraversalComponent::ApplyRaft(float DeltaSeconds, float SurfaceZ)
{
	using namespace TNKartTraversalDetail;
	ATN_Buggy* Kart = GetKart();
	USkeletalMeshComponent* Chassis = Kart->GetMesh();
	UChaosWheeledVehicleMovementComponent* Move = Kart->GetWheeledMovement();
	UWorld* World = GetWorld();
	if (!Chassis || !Chassis->IsSimulatingPhysics() || !Move || !World)
	{
		return;
	}
	const float Gravity = FMath::Max(1.f, -World->GetGravityZ());
	const FVector Location = Kart->GetActorLocation();
	const FVector Velocity = Chassis->GetPhysicsLinearVelocity();
	const FVector Forward = Kart->GetActorForwardVector().GetSafeNormal2D();
	const FVector Right(-Forward.Y, Forward.X, 0.f);
	const float ForwardSpeed = static_cast<float>(FVector::DotProduct(Velocity, Forward));
	const float SideSpeed = static_cast<float>(FVector::DotProduct(Velocity, Right));

	// Flotación, resistencia del agua (mucha de lado: la balsa no derrapa) y remo con el acelerador.
	FVector Accel(0.f, 0.f, TNKart::BuoyancyAccel(SurfaceZ + FloatLineCm - static_cast<float>(Location.Z), static_cast<float>(Velocity.Z), Gravity));
	Accel -= Right * (SideSpeed * 2.5f) + Forward * (ForwardSpeed * 0.6f);
	const bool bLocked = Kart->IsEngineLocked() || Kart->IsRaceBrakeHeld();
	const float Drive = bLocked ? 0.f : Move->GetThrottleInput() - 0.6f * Move->GetBrakeInput();
	if ((Drive > 0.f && ForwardSpeed < MaxFloatSpeedCms) || (Drive < 0.f && ForwardSpeed > -0.4f * MaxFloatSpeedCms))
	{
		Accel += Forward * (Drive * PaddleAccelCms2);
	}
	// En la orilla (tierra justo delante y a poca profundidad): ayuda a subir la rampa sobre las ruedas.
	if (Drive > 0.f)
	{
		FHitResult Shore;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TNKartShore), false, Kart);
		const FVector Probe = Location + Forward * ShoreProbeAheadCm;
		if (World->LineTraceSingleByChannel(Shore, Probe + FVector(0.f, 0.f, 150.f), Probe - FVector(0.f, 0.f, 250.f), ECC_WorldStatic, Params)
			&& Shore.ImpactPoint.Z > SurfaceZ - 60.f)
		{
			Accel += FVector::UpVector * ShoreClimbUpCms2 + Forward * ShoreClimbForwardCms2;
		}
	}
	Chassis->AddForce(Accel, NAME_None, true);

	// Rumbo con la dirección (menos parado) y la balsa derecha: el agua no la deja volcar.
	FVector Spin = Chassis->GetPhysicsAngularVelocityInDegrees();
	const float SpeedFactor = 0.35f + 0.65f * FMath::Clamp(FMath::Abs(ForwardSpeed) / 400.f, 0.f, 1.f);
	const float TargetYawRate = Move->GetSteeringInput() * FloatYawDegPerSecond * SpeedFactor * (ForwardSpeed < -50.f ? -1.f : 1.f);
	const FVector Tilt = FVector::CrossProduct(Kart->GetActorUpVector(), FVector::UpVector);
	Spin.X = FMath::FInterpTo(Spin.X, static_cast<float>(Tilt.X) * 180.f, DeltaSeconds, 3.f);
	Spin.Y = FMath::FInterpTo(Spin.Y, static_cast<float>(Tilt.Y) * 180.f, DeltaSeconds, 3.f);
	Spin.Z = FMath::FInterpTo(Spin.Z, TargetYawRate, DeltaSeconds, 4.f);
	Chassis->SetPhysicsAngularVelocityInDegrees(Spin);
}

void UTN_KartTraversalComponent::TryGeyserLaunch()
{
	using namespace TNKartTraversalDetail;
	ATN_Buggy* Kart = GetKart();
	UWorld* World = GetWorld();
	if (!World || World->GetTimeSeconds() - LastGeyserLaunch < GeyserCooldownSeconds)
	{
		return;
	}
	const FVector Location = Kart->GetActorLocation();
	for (const TWeakObjectPtr<ATN_ProcGeyser>& Weak : Geysers)
	{
		ATN_ProcGeyser* Geyser = Weak.Get();
		if (!Geyser || Geyser->IsShaft())
		{
			continue;
		}
		const FVector Mouth = Geyser->GetActorLocation();
		if (FVector::Dist2D(Mouth, Location) > GeyserRadiusCm || Location.Z < Mouth.Z - 200.f || Location.Z > Mouth.Z + 400.f)
		{
			continue;
		}
		LastGeyserLaunch = World->GetTimeSeconds();
		// En cada máquina: el chorro del géiser. El lanzamiento, donde se simula el chasis.
		if (GetNetMode() != NM_DedicatedServer)
		{
			Geyser->PlayLaunchBurst();
		}
		USkeletalMeshComponent* Chassis = Kart->GetMesh();
		if ((Kart->HasAuthority() || Kart->IsLocallyControlled()) && Chassis && Chassis->IsSimulatingPhysics())
		{
			const float Gravity = FMath::Max(1.f, -World->GetGravityZ());
			const FVector Landing = Geyser->GetTarget() + FVector(0.f, 0.f, GeyserLandingLiftCm);
			FlightOrigin = Location;
			FlightVelocity = TNKart::GeyserLaunchVelocity(Location, Landing, Geyser->GetApexExtra(), Gravity);
			FlightGravity = Gravity;
			FlightSeconds = TNKart::GeyserFlightSeconds(Location, Landing, Geyser->GetApexExtra(), Gravity);
			Chassis->SetPhysicsLinearVelocity(FlightVelocity);
			Chassis->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
			UE_LOG(LogTNRally, Verbose, TEXT("[Karts] %s sube en el géiser de (%.0f, %.0f, %.0f)."), *Kart->GetName(), Mouth.X, Mouth.Y, Mouth.Z);
		}
		return;
	}
}

void UTN_KartTraversalComponent::GuideGeyserFlight()
{
	ATN_Buggy* Kart = GetKart();
	USkeletalMeshComponent* Chassis = Kart->GetMesh();
	UWorld* World = GetWorld();
	if (FlightSeconds <= 0.f || !Chassis || !Chassis->IsSimulatingPhysics() || !World)
	{
		return;
	}
	const float Time = static_cast<float>(World->GetTimeSeconds() - LastGeyserLaunch);
	if (Time >= FlightSeconds)
	{
		FlightSeconds = 0.f;
		return;
	}
	// Donde tendría que estar en la parábola y a qué velocidad; se corrige lo que se haya desviado.
	const FVector Wanted = FlightOrigin + FlightVelocity * Time + FVector(0.f, 0.f, -0.5f * FlightGravity * Time * Time);
	const FVector Velocity = FlightVelocity + FVector(0.f, 0.f, -FlightGravity * Time);
	Chassis->SetPhysicsLinearVelocity(Velocity + (Wanted - Kart->GetActorLocation()) * 3.f);
}

void UTN_KartTraversalComponent::LevelAfterGeyser(float DeltaSeconds)
{
	using namespace TNKartTraversalDetail;
	ATN_Buggy* Kart = GetKart();
	USkeletalMeshComponent* Chassis = Kart->GetMesh();
	UWorld* World = GetWorld();
	if (!Chassis || !Chassis->IsSimulatingPhysics() || !World || World->GetTimeSeconds() - LastGeyserLaunch > GeyserLevelSeconds)
	{
		return;
	}
	// En el vuelo del géiser el kart no da vueltas: se mantiene derecho (cabeceo y alabeo) y cae sobre sus ruedas.
	FVector Spin = Chassis->GetPhysicsAngularVelocityInDegrees();
	const FVector Tilt = FVector::CrossProduct(Kart->GetActorUpVector(), FVector::UpVector);
	Spin.X = FMath::FInterpTo(Spin.X, static_cast<float>(Tilt.X) * 240.f, DeltaSeconds, 5.f);
	Spin.Y = FMath::FInterpTo(Spin.Y, static_cast<float>(Tilt.Y) * 240.f, DeltaSeconds, 5.f);
	Chassis->SetPhysicsAngularVelocityInDegrees(Spin);
}

void UTN_KartTraversalComponent::ApplySlide(float DeltaSeconds)
{
	ATN_Buggy* Kart = GetKart();
	USkeletalMeshComponent* Chassis = Kart->GetMesh();
	if (!Chassis || !Chassis->IsSimulatingPhysics())
	{
		return;
	}
	const FVector Location = Kart->GetActorLocation();
	for (const TWeakObjectPtr<ATN_ProcSlideZone>& Weak : Slides)
	{
		FVector Flow;
		const ATN_ProcSlideZone* Slide = Weak.Get();
		if (!Slide || !Slide->FindFlowAt(Location, Flow))
		{
			continue;
		}
		// Por la cascada: empujón ladera abajo, el morro hacia donde baja el agua y sin dar vueltas de campana.
		if (Kart->HasAuthority() && GetWorld()->GetTimeSeconds() - LastSlideLog > 3.0)
		{
			UE_LOG(LogTNRally, Verbose, TEXT("[Karts] %s baja por la cascada en (%.0f, %.0f, %.0f)."), *Kart->GetName(), Location.X, Location.Y, Location.Z);
		}
		LastSlideLog = GetWorld()->GetTimeSeconds();
		Chassis->AddForce(Flow * SlideAccelCms2, NAME_None, true);
		const FVector Forward = Kart->GetActorForwardVector().GetSafeNormal2D();
		const FVector FlowFlat = Flow.GetSafeNormal2D();
		const float YawError = FMath::RadiansToDegrees(FMath::Atan2(
			static_cast<float>(FVector::CrossProduct(Forward, FlowFlat).Z), static_cast<float>(FVector::DotProduct(Forward, FlowFlat))));
		FVector Spin = Chassis->GetPhysicsAngularVelocityInDegrees();
		const FVector Roll = Kart->GetActorForwardVector() * FVector::DotProduct(Spin, Kart->GetActorForwardVector());
		Spin -= Roll * FMath::Clamp(6.f * DeltaSeconds, 0.f, 1.f);
		Spin.Z = FMath::FInterpTo(Spin.Z, FMath::Clamp(YawError * 2.f, -90.f, 90.f), DeltaSeconds, 3.f);
		Chassis->SetPhysicsAngularVelocityInDegrees(Spin);
		return;
	}
}

void UTN_KartTraversalComponent::UpdateWaterVisuals(float DeltaSeconds, bool bWasFloating, float SurfaceZ)
{
	using namespace TNKartTraversalDetail;
	AActor* Kart = GetOwner();
	const float PreviousFold = Fold01;
	Fold01 = TNKart::AdvanceFold(Fold01, bFloating, FoldRate, DeltaSeconds);
	if (Fold01 > 0.f || PreviousFold > 0.f)
	{
		FoldTires();
	}
	if (bFloating && !bFxBuilt)
	{
		bFxBuilt = true;
		TNAmbientFX::FEmitterDesc Spray;
		Spray.Shape = TNAmbientFX::EShape::Drop;
		Spray.Color = FLinearColor(0.85f, 0.95f, 1.f);
		Spray.MaxParticles = 60;
		Spray.Rate = 50.f;
		Spray.SpawnRadius = 70.f;
		Spray.Direction = FVector::UpVector;
		Spray.Speed = 380.f;
		Spray.Spread = 0.7f;
		Spray.LifeMin = 0.4f;
		Spray.LifeMax = 0.8f;
		Spray.SizeStart = 9.f;
		Spray.SizeEnd = 6.f;
		TNAmbientFX::AddEmitter(Kart, Spray, Kart->GetActorLocation());
		TNAmbientFX::FEmitterDesc Wake;
		Wake.Shape = TNAmbientFX::EShape::Puff;
		Wake.bSoft = true;
		Wake.bCloud = true;
		Wake.Color = FLinearColor::White;
		Wake.Alpha = 0.45f;
		Wake.MaxParticles = 50;
		Wake.Rate = 22.f;
		Wake.SpawnRadius = 90.f;
		Wake.Direction = FVector::UpVector;
		Wake.Speed = 25.f;
		Wake.Spread = 0.4f;
		Wake.Gravity = 0.f;
		Wake.Drag = 1.f;
		Wake.LifeMin = 1.2f;
		Wake.LifeMax = 2.f;
		Wake.SizeStart = 60.f;
		Wake.SizeEnd = 170.f;
		TNAmbientFX::AddEmitter(Kart, Wake, Kart->GetActorLocation());
		TNAmbientFX::FEmitterDesc Ring;
		Ring.Shape = TNAmbientFX::EShape::Ring;
		Ring.bSoft = true;
		Ring.Color = FLinearColor(0.9f, 0.97f, 1.f);
		Ring.Alpha = 0.6f;
		Ring.MaxParticles = 8;
		Ring.Rate = 0.f;
		Ring.SpawnRadius = 40.f;
		Ring.Speed = 0.f;
		Ring.Gravity = 0.f;
		Ring.LifeMin = 0.7f;
		Ring.LifeMax = 1.f;
		Ring.SizeStart = 120.f;
		Ring.SizeEnd = 420.f;
		TNAmbientFX::AddEmitter(Kart, Ring, Kart->GetActorLocation());
	}
	if (!bFxBuilt)
	{
		return;
	}
	const FVector Location = Kart->GetActorLocation();
	const FVector Forward = Kart->GetActorForwardVector().GetSafeNormal2D();
	const float Speed = static_cast<float>(Kart->GetVelocity().Size2D());
	if (TNAmbientFX::FEmitter* E = TNAmbientFX::GetEmitter(Kart, FxSpray))
	{
		E->Origin = FVector(Location.X, Location.Y, SurfaceZ + 10.f) + Forward * 180.f;
		E->RateScale = bFloating ? FMath::Clamp(Speed / 500.f, 0.f, 1.5f) : 0.f;
		if (bFloating && !bWasFloating)
		{
			TNAmbientFX::Burst(*E, 45);
		}
	}
	if (TNAmbientFX::FEmitter* E = TNAmbientFX::GetEmitter(Kart, FxWake))
	{
		E->Origin = FVector(Location.X, Location.Y, SurfaceZ + 5.f) - Forward * 220.f;
		E->RateScale = bFloating ? FMath::Clamp(Speed / 400.f, 0.15f, 1.5f) : 0.f;
	}
	if (TNAmbientFX::FEmitter* E = TNAmbientFX::GetEmitter(Kart, FxRing))
	{
		E->Origin = FVector(Location.X, Location.Y, SurfaceZ + 3.f);
		if (bFloating && !bWasFloating)
		{
			TNAmbientFX::Burst(*E, 3);
		}
	}
	TNAmbientFX::TickOwner(Kart, DeltaSeconds);
}

void UTN_KartTraversalComponent::FoldTires()
{
	using namespace TNKartTraversalDetail;
	if (Tires.Num() == 0)
	{
		TArray<UStaticMeshComponent*> Meshes;
		GetOwner()->GetComponents(Meshes);
		for (UStaticMeshComponent* Mesh : Meshes)
		{
			if (Mesh && Mesh->GetName().StartsWith(TEXT("Tire_")))
			{
				Tires.Add(Mesh);
				TireBase.Add(Mesh->GetRelativeTransform());
				TireWritten.Add(FTransform::Identity);
			}
		}
	}
	for (int32 Index = 0; Index < Tires.Num(); ++Index)
	{
		UStaticMeshComponent* Tire = Tires[Index].Get();
		if (!Tire)
		{
			continue;
		}
		// Lo que le pone el buggy este fotograma (suspensión, dirección y rodadura) es la base; si no la ha tocado, la de antes.
		const FTransform Current = Tire->GetRelativeTransform();
		if (!Current.Equals(TireWritten[Index], 0.01))
		{
			TireBase[Index] = Current;
		}
		const FTransform& Base = TireBase[Index];
		const float Side = Base.GetLocation().Y >= 0.0 ? 1.f : -1.f;
		const FQuat Fold(FVector::ForwardVector, FMath::DegreesToRadians(Side * FoldAngleDeg * Fold01));
		const FTransform Folded(Fold * Base.GetRotation(), Base.GetLocation() + FVector(0.0, -Side * FoldInCm * Fold01, FoldUpCm * Fold01),
			Base.GetScale3D());
		Tire->SetRelativeTransform(Folded);
		TireWritten[Index] = Folded;
	}
}
