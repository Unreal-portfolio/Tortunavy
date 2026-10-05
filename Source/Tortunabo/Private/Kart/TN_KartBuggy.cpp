#include "Kart/TN_KartBuggy.h"

#include "Camera/CameraComponent.h"
#include "ChaosVehicleWheel.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputActionValue.h"
#include "Kart/TN_KartGunnerPawn.h"
#include "Kart/TN_KartInput.h"
#include "Kart/TN_KartItemComponent.h"
#include "Kart/TN_KartTraversalComponent.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsEngine/BodySetup.h"
#include "Rally/TN_RallyLogic.h"
#include "Vehicles/TN_BuggyData.h"
#include "Vehicles/TN_BuggyTurretComponent.h"
#include "Vehicles/TN_RallyTurretLogic.h"
#include "VR/TN_VRSeatComponent.h"

namespace TNKart
{
	float LeanSteerMultiplier(float Lean, float Steer)
	{
		const float L = FMath::Clamp(Lean, -1.f, 1.f);
		const float S = FMath::Clamp(Steer, -1.f, 1.f);
		// Mismo signo: inclinada hacia dentro de la curva. El efecto crece con lo que se gira (sin girar, nada).
		return 1.f + LeanSteerGain * L * FMath::Sign(S) * FMath::Min(1.f, FMath::Abs(S) * 2.f);
	}

	float RecenterLook(float Degrees, float RecenterDegPerSecond, float DeltaSeconds)
	{
		const float Step = FMath::Max(0.f, RecenterDegPerSecond) * FMath::Max(0.f, DeltaSeconds);
		return FMath::Abs(Degrees) <= Step ? 0.f : Degrees - FMath::Sign(Degrees) * Step;
	}
}

namespace TNKartBuggyDetail
{
	/** Envíos del apuntado de la conductora sola al servidor por segundo, y cambio mínimo que se envía (grados). */
	constexpr float AimSendRate = 10.f;
	constexpr float AimSendMinDeltaDeg = 1.5f;
	/** Límites de la cámara de la conductora al mirar arriba y abajo (grados). */
	constexpr float LookPitchMin = -25.f;
	constexpr float LookPitchMax = 35.f;
	/** Pulsaciones del disparo de la conductora sola que se mandan como mucho por segundo (la cadencia la pone la torreta). */
	constexpr double FireRequestSeconds = 0.1;
	/** Ayuda al apuntar: el blanco más cercano en este cono de la mirada (grados) y alcance (cm). */
	constexpr float FireAssistHalfAngleDeg = 8.f;
	constexpr float FireAssistRangeCm = 6000.f;
	constexpr float TargetAimUpCm = 60.f;
	/** El servidor acepta la dirección del cliente si no se separa más de esto del apuntado de la torreta (grados). */
	constexpr float MaxSoloFireErrorDeg = 25.f;
	/** Por debajo de esto el multiplicador de giro cuenta como sin inclinación. */
	constexpr float LeanSteerEpsilon = 0.02f;
	/** Tope del giro de la rueda con la inclinación hacia dentro (grados): el máximo que admite UTN_BuggyData. */
	constexpr float LeanSteerMaxDeg = 45.f;
	/** Holgura sobre el suelo al reaparecer (cm): cae un palmo y se asienta. */
	constexpr float TeleportClearanceCm = 15.f;
}

ATN_KartBuggy::ATN_KartBuggy()
{
	Items = CreateDefaultSubobject<UTN_KartItemComponent>(TEXT("KartItems"));
	Traversal = CreateDefaultSubobject<UTN_KartTraversalComponent>(TEXT("KartTraversal"));
	GunnerPawnClass = ATN_KartGunnerPawn::StaticClass();
}

void ATN_KartBuggy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_KartBuggy, GunnerLeanQ);
}

void ATN_KartBuggy::BeginPlay()
{
	Super::BeginPlay();
	DisableDetachedBodies();
	CameraArm = FindComponentByClass<USpringArmComponent>();
	if (CameraArm)
	{
		CameraArmBaseRotation = CameraArm->GetRelativeRotation();
	}
}

void ATN_KartBuggy::DisableDetachedBodies()
{
	// El PhysicsAsset del chasis trae un cuerpo por rueda (PhysWheel_*), sin simular y con colisión: no siguen al chasis
	// (se quedan donde nació el kart, a metro y medio del suelo) y los que vienen detrás chocan con ellos en la parrilla. Las
	// ruedas de Chaos no los usan: solo queda con colisión el cuerpo del chasis.
	USkeletalMeshComponent* Chassis = GetMesh();
	const FBodyInstance* Root = Chassis ? Chassis->GetBodyInstance() : nullptr;
	int32 Disabled = 0;
	for (FBodyInstance* PhysBody : Chassis ? Chassis->Bodies : TArray<FBodyInstance*>())
	{
		// La colisión de los cuerpos de un esqueleto la decide su componente: se apaga forma a forma (anulación por forma).
		const UBodySetup* Setup = PhysBody ? PhysBody->GetBodySetup() : nullptr;
		const int32 Shapes = Setup ? Setup->AggGeom.GetElementCount() : 0;
		if (PhysBody && PhysBody != Root && PhysBody->IsValidBodyInstance() && Shapes > 0
			&& PhysBody->GetShapeCollisionEnabled(0) != ECollisionEnabled::NoCollision)
		{
			for (int32 Shape = 0; Shape < Shapes; ++Shape)
			{
				PhysBody->SetShapeCollisionEnabled(Shape, ECollisionEnabled::NoCollision, Shape == Shapes - 1);
			}
			++Disabled;
		}
	}
	if (Disabled > 0)
	{
		UE_LOG(LogTNRally, Verbose, TEXT("[Karts] %s: %d cuerpos sueltos del chasis sin colisión."), *GetName(), Disabled);
	}
}

UTN_KartInputSet* ATN_KartBuggy::GetKartInput()
{
	if (!KartInput)
	{
		KartInput = UTN_KartInputSet::Create(this, bDriverItems);
	}
	return KartInput;
}

void ATN_KartBuggy::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		return;
	}
	const UTN_KartInputSet* Set = GetKartInput();
	Input->BindAction(Set->Backward, ETriggerEvent::Started, this, &ATN_KartBuggy::OnBackwardPressed);
	Input->BindAction(Set->Backward, ETriggerEvent::Completed, this, &ATN_KartBuggy::OnBackwardReleased);
	Input->BindAction(Set->LookMouse, ETriggerEvent::Triggered, this, &ATN_KartBuggy::OnLookMouse);
	Input->BindAction(Set->LookStick, ETriggerEvent::Triggered, this, &ATN_KartBuggy::OnLookStick);
	if (bDriverItems)
	{
		Input->BindAction(Set->UseItem, ETriggerEvent::Started, this, &ATN_KartBuggy::OnUseItem);
		Input->BindAction(Set->Fire, ETriggerEvent::Triggered, this, &ATN_KartBuggy::OnFire);
	}
}

void ATN_KartBuggy::NotifyControllerChanged()
{
	if (KartInput)
	{
		UTN_KartInputSet::RemoveContext(Cast<APlayerController>(PreviousController), KartInput->DriverContext);
	}
	if (const APlayerController* PC = Cast<APlayerController>(Controller); PC && PC->IsLocalController())
	{
		UTN_KartInputSet::AddContext(PC, GetKartInput()->DriverContext);
	}
	bBackwardHeld = false;
	LookYaw = 0.f;
	LookPitch = 0.f;
	Super::NotifyControllerChanged();
}

bool ATN_KartBuggy::MayUseItems(const AController* Requester) const
{
	if (!Requester || !bDriverItems)
	{
		return false;
	}
	// Con una tortuga de artillera, los objetos son suyos; sin ella (o con el piloto IA de la artillera), de la conductora.
	// Se decide por los PlayerState de las plazas, que se replican: los controladores de los asientos solo existen en el
	// servidor, y en un cliente la conductora sin artillera nunca pedía usar el objeto (#295, #304).
	const APlayerState* Requesting = Requester->PlayerState;
	if (const APlayerState* Gunner = GetSeatPlayerState(ETNRallySeat::Gunner))
	{
		return Requesting && Requesting == Gunner;
	}
	if (const APlayerState* Driver = GetSeatPlayerState(ETNRallySeat::Driver))
	{
		return Requesting && Requesting == Driver;
	}
	// Sin jugadoras sentadas (kart de la IA): solo el servidor sabe quién conduce.
	return Requester == GetSeatController(ETNRallySeat::Driver);
}

void ATN_KartBuggy::OnUseItem(const FInputActionValue& Value)
{
	if (Items && Items->CanUseItem() && MayUseItems(GetController()))
	{
		ServerUseItem(bBackwardHeld);
	}
}

bool ATN_KartBuggy::ServerUseItem_Validate(bool bBackward)
{
	return true;
}

void ATN_KartBuggy::ServerUseItem_Implementation(bool bBackward)
{
	if (Items && MayUseItems(GetController()))
	{
		Items->UseItem(bBackward);
	}
}

void ATN_KartBuggy::OnBackwardPressed(const FInputActionValue& Value)
{
	bBackwardHeld = true;
}

void ATN_KartBuggy::OnBackwardReleased(const FInputActionValue& Value)
{
	bBackwardHeld = false;
}

void ATN_KartBuggy::OnLookMouse(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddLook(static_cast<float>(Axis.X) * LookMouseDegreesPerUnit, static_cast<float>(Axis.Y) * LookMouseDegreesPerUnit);
}

void ATN_KartBuggy::OnLookStick(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	const float Step = LookStickDegreesPerSecond * (GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f);
	AddLook(static_cast<float>(Axis.X) * Step, static_cast<float>(Axis.Y) * Step);
}

void ATN_KartBuggy::AddLook(float DeltaYaw, float DeltaPitch)
{
	if (FMath::IsNearlyZero(DeltaYaw) && FMath::IsNearlyZero(DeltaPitch))
	{
		return;
	}
	LookYaw = FRotator::NormalizeAxis(LookYaw + DeltaYaw);
	LookPitch = FMath::Clamp(LookPitch + DeltaPitch, TNKartBuggyDetail::LookPitchMin, TNKartBuggyDetail::LookPitchMax);
	LookIdleSeconds = 0.f;
}

FVector ATN_KartBuggy::GetLookWorldDirection() const
{
	const float Yaw = LookYaw + (bBackwardHeld ? 180.f : 0.f);
	return GetActorTransform().TransformVectorNoScale(FRotator(LookPitch, Yaw, 0.f).Vector()).GetSafeNormal();
}

void ATN_KartBuggy::OnFire(const FInputActionValue& Value)
{
	UWorld* World = GetWorld();
	if (!World || HasGunner() || World->GetTimeSeconds() - LastFireRequest < TNKartBuggyDetail::FireRequestSeconds)
	{
		return;
	}
	LastFireRequest = World->GetTimeSeconds();
	ServerSoloFire(GetLookWorldDirection());
}

bool ATN_KartBuggy::ServerSoloFire_Validate(FVector_NetQuantizeNormal Dir)
{
	return !Dir.ContainsNaN();
}

void ATN_KartBuggy::ServerSoloFire_Implementation(FVector_NetQuantizeNormal Dir)
{
	using namespace TNKartBuggyDetail;
	UTN_BuggyTurretComponent* Gun = GetTurret();
	UWorld* World = GetWorld();
	if (!Gun || !World || !bDriverItems || HasGunner() || Dir.IsNearlyZero())
	{
		return;
	}
	// La dirección del cliente vale si cae cerca de lo que apunta la torreta (que sigue a su cámara); si no, la de la torreta.
	const FVector TurretDir = Gun->GetAimWorldDirection();
	FVector Shot = FVector(Dir).GetSafeNormal();
	if (FVector::DotProduct(Shot, TurretDir) < FMath::Cos(FMath::DegreesToRadians(MaxSoloFireErrorDeg)))
	{
		Shot = TurretDir;
	}
	// Un poco de ayuda: el kart más cercano en un cono estrecho de la mirada.
	const FVector Origin = Gun->GetComponentLocation();
	TArray<FVector> Candidates;
	for (TActorIterator<ATN_Buggy> It(World); It; ++It)
	{
		if (*It != this)
		{
			Candidates.Add(It->GetActorLocation());
		}
	}
	const int32 Target = TNRallyTurret::PickAutoAimTarget(Origin, Shot, Candidates, FireAssistRangeCm, FireAssistHalfAngleDeg);
	if (Target != INDEX_NONE)
	{
		Shot = (Candidates[Target] + FVector(0.f, 0.f, TargetAimUpCm) - Origin).GetSafeNormal();
	}
	Gun->TryFire(false, Shot);
}

bool ATN_KartBuggy::ServerSetSoloAim_Validate(float Yaw, float Pitch)
{
	return FMath::IsFinite(Yaw) && FMath::IsFinite(Pitch) && FMath::Abs(Yaw) <= 360.f && FMath::Abs(Pitch) <= 90.f;
}

void ATN_KartBuggy::ServerSetSoloAim_Implementation(float Yaw, float Pitch)
{
	// Solo sin artillera: la torreta es de la conductora.
	if (!HasGunner() && GetTurret())
	{
		GetTurret()->SetAimRelative(FRotator(Pitch, Yaw, 0.f));
	}
}

void ATN_KartBuggy::SetGunnerLean(float Lean)
{
	if (!HasAuthority())
	{
		return;
	}
	const int8 Quantized = static_cast<int8>(FMath::RoundToInt(FMath::Clamp(Lean, -1.f, 1.f) * 100.f));
	if (Quantized != GunnerLeanQ)
	{
		GunnerLeanQ = Quantized;
	}
}

void ATN_KartBuggy::RallyTeleport(const FTransform& Where, float LockSeconds, float GhostSeconds)
{
	FTransform Grounded = Where;
	UWorld* World = GetWorld();
	if (HasAuthority() && World)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TNKartTeleport), false, this);
		for (TActorIterator<ATN_Buggy> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		FHitResult Hit;
		const FVector At = Where.GetLocation();
		if (World->LineTraceSingleByObjectType(Hit, At + FVector(0.0, 0.0, 400.0), At - FVector(0.0, 0.0, 1500.0),
			FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			Grounded.SetLocation(FVector(At.X, At.Y, Hit.ImpactPoint.Z + RideHeightCm + TNKartBuggyDetail::TeleportClearanceCm));
		}
	}
	Super::RallyTeleport(Grounded, LockSeconds, GhostSeconds);
}

void ATN_KartBuggy::MeasureRideHeight()
{
	UChaosWheeledVehicleMovementComponent* Move = GetWheeledMovement();
	UWorld* World = GetWorld();
	if (bRideHeightMeasured || !Move || !World || Move->Wheels.Num() < 4 || GetVelocity().SizeSquared() > FMath::Square(50.f)
		|| GetActorUpVector().Z < 0.98f)
	{
		return;
	}
	for (int32 Wheel = 0; Wheel < Move->Wheels.Num(); ++Wheel)
	{
		if (!Move->GetWheelState(Wheel).bInContact)
		{
			return;
		}
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TNKartRideHeight), false, this);
	FHitResult Hit;
	const FVector At = GetActorLocation();
	if (World->LineTraceSingleByObjectType(Hit, At + FVector(0.0, 0.0, 50.0), At - FVector(0.0, 0.0, 600.0),
		FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		RideHeightCm = FMath::Clamp(static_cast<float>(At.Z - Hit.ImpactPoint.Z), 20.f, 250.f);
		bRideHeightMeasured = true;
		UE_LOG(LogTNRally, Verbose, TEXT("[Karts] %s: origen a %.0f cm del suelo con las ruedas apoyadas."), *GetName(), RideHeightCm);
	}
}

bool ATN_KartBuggy::FindWaterSurfaceZ(const FVector& Location, double& OutZ) const
{
	float SurfaceZ = 0.f;
	if (Traversal && Traversal->FindWaterSurfaceAt(Location, 0.f, SurfaceZ))
	{
		OutZ = SurfaceZ;
		return true;
	}
	return Super::FindWaterSurfaceZ(Location, OutZ);
}

void ATN_KartBuggy::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Los cuerpos del chasis se pueden volver a crear (cambio de estado físico): se repasa cada fotograma (son cinco).
	DisableDetachedBodies();
	if (HasAuthority())
	{
		MeasureRideHeight();
	}
	if (HasAuthority() && !HasGunner() && GunnerLeanQ != 0)
	{
		// Sin artillera no hay quien se incline.
		GunnerLeanQ = 0;
	}
	if (HasAuthority() || IsLocallyControlled())
	{
		ApplyLeanSteering();
	}
	if (IsLocallyControlled() && IsPlayerControlled())
	{
		UpdateLook(DeltaSeconds);
	}
}

void ATN_KartBuggy::ApplyLeanSteering()
{
	UChaosWheeledVehicleMovementComponent* Move = GetWheeledMovement();
	const UTN_BuggyData* Tuning = GetData();
	if (!Move || !Move->HasValidPhysicsState() || !Tuning)
	{
		return;
	}
	const float Wanted = TNKart::LeanSteerMultiplier(GetGunnerLean(), Move->GetSteeringInput());
	const bool bNeutral = FMath::Abs(Wanted - 1.f) < TNKartBuggyDetail::LeanSteerEpsilon;
	// ATN_Buggy::ApplyWheelFriction vuelve a poner el ángulo del ajuste cada vez que cambia la fricción (freno de mano,
	// charco): con la inclinación se reaplica cada fotograma; sin ella basta con devolverlo una vez.
	if (bNeutral && AppliedLeanSteer == 1.f)
	{
		return;
	}
	AppliedLeanSteer = bNeutral ? 1.f : Wanted;
	const float Angle = FMath::Min(Tuning->MaxSteerAngleDeg * AppliedLeanSteer, TNKartBuggyDetail::LeanSteerMaxDeg);
	for (int32 Wheel = 0; Wheel < 2; ++Wheel)
	{
		Move->SetWheelMaxSteerAngle(Wheel, Angle);
	}
}

void ATN_KartBuggy::UpdateLook(float DeltaSeconds)
{
	using namespace TNKartBuggyDetail;
	LookIdleSeconds += DeltaSeconds;
	// Con gafas se mira con la cabeza (#529): hacia donde mira respecto del kart, sin volver al centro. Sin gafas
	// (simulado), la cámara del asiento mira hacia donde lleve el ratón.
	UTN_VRSeatComponent* Seat = GetVRSeat(ETNRallySeat::Driver);
	if (Seat && Seat->IsHeadsetView())
	{
		const FRotator Head = Seat->GetHeadRelativeRotation();
		LookYaw = FRotator::NormalizeAxis(static_cast<float>(Head.Yaw));
		LookPitch = FMath::Clamp(static_cast<float>(Head.Pitch), LookPitchMin, LookPitchMax);
		LookIdleSeconds = 0.f;
	}
	if (LookIdleSeconds > LookRecenterDelaySeconds)
	{
		LookYaw = TNKart::RecenterLook(LookYaw, LookRecenterDegPerSecond, DeltaSeconds);
		LookPitch = TNKart::RecenterLook(LookPitch, LookRecenterDegPerSecond, DeltaSeconds);
	}
	const float ViewYaw = LookYaw + (bBackwardHeld ? 180.f : 0.f);
	if (CameraArm)
	{
		CameraArm->SetRelativeRotation(FRotator(CameraArmBaseRotation.Pitch + LookPitch, CameraArmBaseRotation.Yaw + ViewYaw, 0.f));
	}
	if (Seat && Seat->IsVRView() && !Seat->IsHeadsetView())
	{
		Seat->SetSimulatedLook(FRotator(LookPitch, ViewYaw, 0.f));
	}
	// Sola, la torreta sigue a la cámara (con artillera, la maneja ella).
	if (HasGunner())
	{
		return;
	}
	AimSendAccumulator += DeltaSeconds;
	const FRotator Aim = TNRallyTurret::ClampAim(FRotator(LookPitch * 0.5f, FRotator::NormalizeAxis(ViewYaw), 0.f));
	if (AimSendAccumulator >= 1.f / AimSendRate
		&& (FMath::Abs(FRotator::NormalizeAxis(Aim.Yaw - LastSentAim.Yaw)) > AimSendMinDeltaDeg || FMath::Abs(Aim.Pitch - LastSentAim.Pitch) > AimSendMinDeltaDeg))
	{
		AimSendAccumulator = 0.f;
		LastSentAim = Aim;
		ServerSetSoloAim(static_cast<float>(Aim.Yaw), static_cast<float>(Aim.Pitch));
	}
}
