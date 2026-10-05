#include "Kart/TN_KartBuggy.h"

#include "Camera/CameraComponent.h"
#include "ChaosVehicleWheel.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "InputActionValue.h"
#include "Kart/TN_KartGunnerPawn.h"
#include "Kart/TN_KartInput.h"
#include "Kart/TN_KartItemComponent.h"
#include "Kart/TN_KartTraversalComponent.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsEngine/BodySetup.h"
#include "Rally/TN_RallyLogic.h"
#include "Vehicles/TN_BuggyData.h"
#include "Vehicles/TN_BuggyMath.h"
#include "Vehicles/TN_BuggyTurretComponent.h"
#include "Vehicles/TN_BuggyWheel.h"
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

	void ApplyKartTuning(UTN_BuggyData& Data, float SpeedScale, float TopEndTorqueScale)
	{
		const float Scale = FMath::Max(SpeedScale, 0.1f);
		// Punta y aceleración: el régimen y el par suben por Scale y el par de la parte alta de la curva (que es el que fija la
		// punta) por TopEndTorqueScale.
		Data.MaxRPM *= Scale;
		Data.MaxTorque *= Scale;
		Data.TopEndTorqueScale = TopEndTorqueScale;
		// El turbo parte de TNRallyTurret::BuggyTopSpeedCms, la punta del Rally: su múltiplo sigue a la del kart. Empuja más
		// fuerte (el mini-turbo del derrape dura poco) y sube y baja deprisa.
		Data.BoostTopSpeedMultiplier *= Scale;
		Data.BoostPushAccel *= Scale * 1.8f;
		Data.BoostPushFadeBandCms *= Scale;
		Data.BoostRampUpSeconds = 0.25f;
		Data.BoostRampDownSeconds = 0.35f;
		// A más velocidad, el alabeo se deja de corregir más tarde (igual de seguro que en el Rally a su velocidad).
		Data.AntiRollGroundRollFullSpeedCms *= Scale;
		Data.AntiRollGroundRollZeroSpeedCms *= Scale;
		// Derrape que se controla: el freno de mano suelta menos la trasera y el contravolante no pelea con un derrape corto.
		Data.HandbrakeRearFriction = 2.2f;
		Data.CounterSteerStartSlipDeg = 16.f;
	}

	float SpeedSteerMultiplier(float ForwardSpeedCms)
	{
		const float Ratio = FMath::Abs(ForwardSpeedCms) / SteerHalfSpeedCms;
		return FMath::Max(MinSteerFraction, 1.f / (1.f + FMath::Pow(Ratio, SteerFallExponent)));
	}

	float DriftBoostSeconds(float DriftSeconds)
	{
		if (DriftSeconds >= DriftTier3Seconds)
		{
			return DriftBoost3Seconds;
		}
		if (DriftSeconds >= DriftTier2Seconds)
		{
			return DriftBoost2Seconds;
		}
		return DriftSeconds >= DriftTier1Seconds ? DriftBoost1Seconds : 0.f;
	}

	FDriftStep AdvanceDrift(float DriftSeconds, bool bHandbrake, bool bGrounded, float ForwardSpeedCms, float Steer, float SlipDeg,
		float Dt)
	{
		FDriftStep Out;
		if (!bHandbrake)
		{
			// Al soltar el freno de mano se cobra el derrape, si lo hubo.
			Out.BoostSeconds = DriftBoostSeconds(DriftSeconds);
			return Out;
		}
		if (ForwardSpeedCms < DriftMinSpeedCms)
		{
			// Casi parado con el freno puesto: se frenó, no se derrapó.
			return Out;
		}
		const bool bDrifting = bGrounded && (FMath::Abs(Steer) >= DriftMinSteer || FMath::Abs(SlipDeg) >= DriftMinSlipDeg);
		Out.DriftSeconds = DriftSeconds + (bDrifting ? FMath::Max(Dt, 0.f) : 0.f);
		return Out;
	}
}

namespace TNKartBuggyDetail
{
	/**
	 * Mandos de prueba (se ponen con -ExecCmds o en la consola): se leen cuando aparece cada kart. TN.Kart.Tuning 0 deja el
	 * kart como el buggy del Rally para comparar.
	 */
	TAutoConsoleVariable<int32> CVarKartTuning(TEXT("TN.Kart.Tuning"), 1,
		TEXT("Karts (#742): 1 = conducción de los karts (más punta y aceleración, dirección que se cierra a velocidad, mini-turbo del derrape); 0 = como el buggy del Rally. Se lee al aparecer cada kart."),
		ECVF_Default);
	TAutoConsoleVariable<float> CVarKartSpeedScale(TEXT("TN.Kart.SpeedScale"), TNKart::DefaultSpeedScale,
		TEXT("Karts (#742): cuánto más rápido que el buggy del Rally (1,3 = un 30 % más). Se lee al aparecer cada kart."),
		ECVF_Default);
	TAutoConsoleVariable<float> CVarKartTopEndTorque(TEXT("TN.Kart.TopEndTorque"), TNKart::DefaultTopEndTorqueScale,
		TEXT("Karts (#742): multiplicador del par de la parte alta de la curva (el que fija la punta). Se lee al aparecer cada kart."),
		ECVF_Default);

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

void ATN_KartBuggy::PostInitializeComponents()
{
	using namespace TNKartBuggyDetail;
	// La conducción de los karts es un ajuste (UTN_BuggyData) propio de este kart, que lee ATN_Buggy al crear el motor: tiene que
	// estar puesto antes de Super. Un kart con ajuste asignado a mano se queda con el suyo.
	bKartTuned = bKartTuning && !Data && CVarKartTuning.GetValueOnGameThread() != 0;
	if (bKartTuned)
	{
		KartSpeedScale = FMath::Clamp(CVarKartSpeedScale.GetValueOnGameThread(), 0.5f, 3.f);
		UTN_BuggyData* Tuned = NewObject<UTN_BuggyData>(this, TEXT("KartTuning"));
		TNKart::ApplyKartTuning(*Tuned, KartSpeedScale, CVarKartTopEndTorque.GetValueOnGameThread());
		Data = Tuned;
		// Con más régimen el cambio automático de Chaos (sube a 4500 rpm) pasaría a 2.ª: se queda siempre en 1.ª, como el Rally.
		if (UChaosWheeledVehicleMovementComponent* Move = GetWheeledMovement())
		{
			Move->TransmissionSetup.ChangeUpRPM = FMath::Max(Move->TransmissionSetup.ChangeUpRPM, Tuned->MaxRPM + 1000.f);
		}
	}
	Super::PostInitializeComponents();
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
		if (bKartTuned)
		{
			ApplyKartHandbrake();
			ApplyDriftStability();
			UpdateDrift(DeltaSeconds);
		}
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
	// La dirección se cierra con la velocidad (#742), pero solo la de la conductora humana: la IA acota su giro por aceleración
	// lateral (ITN_RallyVehicle::GetMaxSteerAngleDeg) y con menos ángulo del que cuenta no sujetaría la pista.
	const bool bAIDriven = Controller && !Controller->IsPlayerController();
	const float SpeedFactor = bKartTuned && !bAIDriven ? TNKart::SpeedSteerMultiplier(Move->GetForwardSpeed()) : 1.f;
	const float Wanted = TNKart::LeanSteerMultiplier(GetGunnerLean(), Move->GetSteeringInput()) * SpeedFactor;
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

void ATN_KartBuggy::ApplyKartHandbrake()
{
	// Una vez por pulsación: si Chaos recrea la simulación, las ruedas vuelven al par de la clase.
	if (!IsHandbrakeHeld())
	{
		bKartHandbrakeApplied = false;
		return;
	}
	UChaosWheeledVehicleMovementComponent* Move = GetWheeledMovement();
	if (bKartHandbrakeApplied || !Move || !Move->HasValidPhysicsState() || Move->WheelSetups.Num() < 4)
	{
		return;
	}
	// Las traseras (2 y 3) son las del freno de mano; el par de serie es el de UTN_BuggyWheelRear.
	const float Torque = GetDefault<UTN_BuggyWheelRear>()->MaxHandBrakeTorque * TNKart::DriftHandbrakeTorqueFraction;
	for (int32 Wheel = 2; Wheel < 4; ++Wheel)
	{
		Move->SetWheelHandbrakeTorque(Wheel, Torque);
	}
	bKartHandbrakeApplied = true;
}

void ATN_KartBuggy::ApplyDriftStability()
{
	USkeletalMeshComponent* Chassis = GetMesh();
	if (!IsHandbrakeHeld() || IsAirborne() || !Chassis || !Chassis->IsSimulatingPhysics())
	{
		return;
	}
	TNBuggy::FStabilityTuning Tuning;
	Tuning.StartSlipDeg = TNKart::DriftHoldSlipDeg;
	Tuning.Stiffness = TNKart::DriftHoldStiffness;
	Tuning.Damping = TNKart::DriftHoldDamping;
	Tuning.MaxAccel = TNKart::DriftHoldMaxAccel;
	const FVector Up = GetActorUpVector();
	const FVector Velocity = GetVelocity();
	const float Slip = TNBuggy::SlipAngleDeg(GetActorForwardVector(), Velocity);
	const float YawRate = static_cast<float>(Chassis->GetPhysicsAngularVelocityInRadians() | Up);
	const float Flat = static_cast<float>(FVector(Velocity.X, Velocity.Y, 0.f).Size());
	// Como StabilityYawAccel del buggy, pero con el freno de mano (el derrape largo del Rally) y a partir de una deriva mayor.
	const float Accel = TNBuggy::StabilityYawAccel(Slip, YawRate, Flat, false, false, Tuning);
	if (Accel != 0.f)
	{
		Chassis->AddTorqueInRadians(Up * Accel, NAME_None, true);
	}
}

void ATN_KartBuggy::UpdateDrift(float DeltaSeconds)
{
	UChaosWheeledVehicleMovementComponent* Move = GetWheeledMovement();
	if (!Move || !Move->HasValidPhysicsState())
	{
		DriftSeconds = 0.f;
		return;
	}
	const float Slip = TNBuggy::SlipAngleDeg(GetActorForwardVector(), GetVelocity());
	const TNKart::FDriftStep Step = TNKart::AdvanceDrift(DriftSeconds, IsHandbrakeHeld(), !IsAirborne(), Move->GetForwardSpeed(),
		Move->GetSteeringInput(), Slip, DeltaSeconds);
	DriftSeconds = Step.DriftSeconds;
	if (Step.BoostSeconds > 0.f)
	{
		// El servidor y la conductora local lo piden a la vez; la hora de fin se replica al resto.
		GrantTimedBoost(Step.BoostSeconds);
		UE_LOG(LogTNRally, Verbose, TEXT("[Karts] %s: mini-turbo de %.1f s tras el derrape."), *GetName(), Step.BoostSeconds);
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
