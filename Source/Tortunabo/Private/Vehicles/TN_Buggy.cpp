// ATN_Buggy: construcción, física de conducción (fricción, derrape, golpe de rueda, charco, motor cortado),
// enderezado, tinte y contrato con la carrera. Asientos y tortugas en TN_Buggy_Seats.cpp; input en TN_Buggy_Input.cpp;
// impactos en TN_Buggy_Effects.cpp; estabilidad y turbo en TN_Buggy_Drive.cpp; cámara en TN_Buggy_Camera.cpp; modelo, skins y
// neumáticos en TN_Buggy_Visuals.cpp.

#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyData.h"
#include "Vehicles/TN_BuggyEngineAudioComponent.h"
#include "Vehicles/TN_BuggyGunnerPawn.h"
#include "Vehicles/TN_BuggyHealthComponent.h"
#include "Vehicles/TN_BuggyRiderAnimComponent.h"
#include "Vehicles/TN_BuggyTurretComponent.h"
#include "Vehicles/TN_BuggyWheel.h"
#include "Vehicles/TN_RallyTurretLogic.h"
#include "Camera/CameraComponent.h"
#include "ChaosVehicleWheel.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

const FName ATN_Buggy::TintParameterName(TEXT("PaintColor"));
const FName ATN_Buggy::DriverSeatSocket(TEXT("Seat_Driver"));
const FName ATN_Buggy::GunnerSeatSocket(TEXT("Seat_Gunner"));
const FName ATN_Buggy::MuzzleSocket(TEXT("Muzzle_Gunner"));
// Art/Source/Vehicles/Buggy/manifest.json (sockets_cm).
const FVector ATN_Buggy::GunnerSeatLocal(-80.f, 0.f, 127.38f);
const FVector ATN_Buggy::DriverSeatLocal(22.f, 0.f, 92.38f);
const FVector ATN_Buggy::MuzzleLocal(-15.42f, 0.f, 169.38f);
const FName ATN_Buggy::WheelBoneNames[4] = {
	FName(TEXT("PhysWheel_FL")), FName(TEXT("PhysWheel_FR")), FName(TEXT("PhysWheel_BL")), FName(TEXT("PhysWheel_BR")) };

namespace TNBuggyDetail
{
	const TCHAR* const ChassisMeshPath = TEXT("/Game/Art/Source/Vehicles/Buggy/export/SK_TN_BuggyChassis.SK_TN_BuggyChassis");
	const TCHAR* const BodyMeshPath = TEXT("/Game/Art/Source/Vehicles/Buggy/export/SM_TN_BuggyBody.SM_TN_BuggyBody");
	const TCHAR* const TireMeshPath = TEXT("/Game/Art/Source/Vehicles/Buggy/export/SM_TN_BuggyTire.SM_TN_BuggyTire");
	const TCHAR* const TurtleMeshPath = TEXT("/Game/Meshses/Characters/Player/TotugaDemo_Rig.TotugaDemo_Rig");
	const TCHAR* const SkinPaths[] = {
		TEXT("/Game/Art/Source/Vehicles/Buggy/export/MI_TN_Buggy_Mar.MI_TN_Buggy_Mar"),
		TEXT("/Game/Art/Source/Vehicles/Buggy/export/MI_TN_Buggy_Alga.MI_TN_Buggy_Alga"),
		TEXT("/Game/Art/Source/Vehicles/Buggy/export/MI_TN_Buggy_Medusa.MI_TN_Buggy_Medusa"),
	};

	/** Ejes de las ruedas en SK_TN_BuggyChassis (los de SKM_Offroad), para el constructor; en juego se leen los huesos. */
	const FVector WheelRestLocal[] = {
		FVector(168.3f, -124.1f, 51.1f), FVector(168.3f, 124.1f, 51.1f), FVector(-135.2f, -139.8f, 50.8f), FVector(-135.2f, 139.8f, 50.8f) };

	/** Ruedas en el orden de WheelSetups: delanteras 0 y 1, traseras 2 y 3. */
	constexpr int32 FirstRearWheel = 2;
	constexpr int32 WheelCount = 4;

	/** Cadera (hueso Hips) de TotugaDemo_Rig a escala 2,5 respecto al origen de la tortuga sentada, ya girada al morro. */
	const FVector DefaultHipsAboveTurtleOrigin(-1.f, 0.f, 61.4f);

	/** Cámara de persecución de HellYeah: pivote sobre el centro, cabeceo fijo en mundo, se abre con la velocidad. */
	const FVector CameraPivotLocal(0.f, 0.f, 120.f);
	constexpr float CameraPitchDeg = -18.f;
	constexpr float CameraLagSpeed = 15.f;
	constexpr float CameraRotationLagSpeed = 6.f;
	constexpr float CameraLagMaxDistance = 100.f;

	/** Velocidad en cm/s a mph: la curva de dirección de Chaos se evalúa en mph. */
	constexpr float CmsToMph = 0.0223694f;

	/** Copia Keys en Curve con interpolación lineal (como la evalúan los tests de TNBuggy), escalando X e Y. */
	void FillLinearCurve(FRichCurve& Curve, TConstArrayView<TNBuggy::FCurveKey> Keys, float ScaleX, float ScaleY)
	{
		Curve.Reset();
		for (const TNBuggy::FCurveKey& Key : Keys)
		{
			const FKeyHandle Handle = Curve.AddKey(Key.X * ScaleX, Key.Y * ScaleY);
			Curve.SetKeyInterpMode(Handle, RCIM_Linear);
		}
	}

	/** Curva de par (TNBuggy::TorqueCurveKeys) en rpm. Se lee al crear la simulación. */
	void BuildTorqueCurve(FVehicleEngineConfig& Engine, float MaxRPM, float MaxTorque)
	{
		const TArray<TNBuggy::FCurveKey> Keys = TNBuggy::TorqueCurveKeys(MaxTorque);
		FillLinearCurve(*Engine.TorqueCurve.GetRichCurve(), Keys, MaxRPM, 1.f);
	}

	/** Curva de dirección (TNBuggy::SteerCurveKeys) en mph y fracción del ángulo máximo. Se lee al crear la simulación. */
	void BuildSteeringCurve(FVehicleSteeringConfig& Steering)
	{
		FillLinearCurve(*Steering.SteeringCurve.GetRichCurve(), TNBuggy::SteerCurveKeys(), CmsToMph, 1.f);
	}

	/**
	 * Reparto del par (#294): tracción total con UTN_BuggyData::DriveRearShare al eje trasero; con 1, solo trasera. Se lee al
	 * crear la simulación.
	 */
	void ApplyDifferential(FVehicleDifferentialConfig& Differential, const UTN_BuggyData& Tuning)
	{
		const float RearShare = FMath::Clamp(Tuning.DriveRearShare, 0.5f, 1.f);
		Differential.DifferentialType = RearShare >= 0.999f ? EVehicleDifferential::RearWheelDrive : EVehicleDifferential::AllWheelDrive;
		Differential.FrontRearSplit = RearShare;
	}

	/** Rapidez y respuesta del volante de Chaos (#606): se usan en el hilo de juego al procesar la entrada. */
	void ApplySteeringResponse(UChaosWheeledVehicleMovementComponent& Move, const UTN_BuggyData& Tuning)
	{
		Move.SteeringInputRate.RiseRate = Tuning.SteerRiseRate;
		Move.SteeringInputRate.FallRate = Tuning.SteerFallRate;
		Move.SteeringInputRate.InputCurveFunction = Tuning.bLinearSteerResponse ? EInputFunctionType::LinearFunction
			: EInputFunctionType::SquaredFunction;
	}
}

ATN_Buggy::ATN_Buggy()
{
	using namespace TNBuggyDetail;
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	ChassisMeshAsset = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(ChassisMeshPath));
	BodyMeshAsset = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(BodyMeshPath));
	TireMeshAsset = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TireMeshPath));
	TurtleMeshAsset = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TurtleMeshPath));
	for (const TCHAR* SkinPath : SkinPaths)
	{
		SkinMaterials.Add(TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SkinPath)));
	}
	GunnerPawnClass = ATN_BuggyGunnerPawn::StaticClass();

	// Mallas pequeñas (la de física es una caja de 12 triángulos): se cargan con la clase, como el resto del buggy.
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> ChassisFinder(ChassisMeshPath);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BodyFinder(BodyMeshPath);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> TireFinder(TireMeshPath);
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> TurtleFinder(TurtleMeshPath);

	USkeletalMeshComponent* Chassis = GetMesh();
	Chassis->SetSkeletalMesh(ChassisFinder.Object);
	Chassis->SetSimulatePhysics(true);
	// El esqueleto solo aporta el cuerpo físico y los huesos de rueda (sin animación): se oculta sin ocultar a sus hijos.
	Chassis->SetVisibility(false, false);
	Chassis->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Chassis);
	Body->SetStaticMesh(BodyFinder.Object);
	Body->SetCollisionProfileName(TEXT("NoCollision"));

	const TCHAR* const TireNames[] = { TEXT("Tire_FL"), TEXT("Tire_FR"), TEXT("Tire_BL"), TEXT("Tire_BR") };
	for (int32 Index = 0; Index < WheelCount; ++Index)
	{
		UStaticMeshComponent* Tire = CreateDefaultSubobject<UStaticMeshComponent>(TireNames[Index]);
		// En el chasis, no en un hueso: UpdateWheelVisuals los pone cada fotograma en el eje de su rueda Chaos.
		Tire->SetupAttachment(Chassis);
		Tire->SetStaticMesh(TireFinder.Object);
		Tire->SetCollisionProfileName(TEXT("NoCollision"));
		// Neumáticos derechos girados para que la cara exterior mire afuera.
		Tire->SetRelativeLocationAndRotation(WheelRestLocal[Index], FRotator(0.f, Index % 2 == 1 ? 180.f : 0.f, 0.f));
		Tires.Add(Tire);
	}

	HealthComponent = CreateDefaultSubobject<UTN_BuggyHealthComponent>(TEXT("Health"));
	EngineAudio = CreateDefaultSubobject<UTN_BuggyEngineAudioComponent>(TEXT("EngineAudio"));
	static ConstructorHelpers::FObjectFinder<USoundBase> BoostLoopFinder(TEXT("/Game/Audio/Rally/SFX_Buggy_Turbo_Loop.SFX_Buggy_Turbo_Loop"));
	static ConstructorHelpers::FObjectFinder<USoundBase> BoostStartFinder(TEXT("/Game/Audio/Rally/SFX_Buggy_Turbo_Start.SFX_Buggy_Turbo_Start"));
	BoostSound = BoostLoopFinder.Object;
	BoostStartSound = BoostStartFinder.Object;
	// Llama del turbo sin Niagara (#294): BoostFlameMesh vacía = cono emisivo construido en ejecución (GetBoostFlameMesh).
	Turret = CreateDefaultSubobject<UTN_BuggyTurretComponent>(TEXT("Turret"));
	Turret->SetupAttachment(Chassis);
	// Pivote PivotRaiseCm por encima de Muzzle_Gunner: el cañón pasa sobre la cabeza de la artillera y el arco trasero.
	Turret->SetRelativeLocation(GunnerSeatLocal + FVector(0.f, 0.f, UTN_BuggyTurretComponent::PivotAboveSeatCm));

	// Torreta con forma propia (#435; el modelo de Art/Source no la trae): las mallas las construye BuildTurretVisuals en
	// ejecución. Caña y cuerpo cuelgan de la torreta (guiñada y cabeceo); carro y aro, del chasis en el pivote.
	const FVector TurretPivot = GunnerSeatLocal + FVector(0.f, 0.f, UTN_BuggyTurretComponent::PivotAboveSeatCm);
	auto MakeTurretPart = [this](const TCHAR* Name, USceneComponent* Parent, const FVector& Location)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Parent);
		Part->SetRelativeLocation(Location);
		Part->SetCollisionProfileName(TEXT("NoCollision"));
		Part->SetGenerateOverlapEvents(false);
		return Part;
	};
	TurretBarrel = MakeTurretPart(TEXT("TurretBarrel"), Turret, FVector::ZeroVector);
	TurretBarrel->ComponentTags.Add(UTN_BuggyTurretComponent::TintTag);
	TurretGun = MakeTurretPart(TEXT("TurretGun"), Turret, FVector::ZeroVector);
	TurretMount = MakeTurretPart(TEXT("TurretMount"), Chassis, TurretPivot);
	TurretRing = MakeTurretPart(TEXT("TurretRing"), Chassis, TurretPivot);
	Turret->SetYawFollower(TurretMount);

	const TCHAR* const SeatNames[] = { TEXT("DriverTurtle"), TEXT("GunnerTurtle") };
	const FVector SeatLocations[] = { DriverSeatLocal, GunnerSeatLocal };
	for (int32 Seat = 0; Seat < 2; ++Seat)
	{
		USkeletalMeshComponent* Turtle = CreateDefaultSubobject<USkeletalMeshComponent>(SeatNames[Seat]);
		Turtle->SetupAttachment(Chassis);
		// Provisional: FitTurtle la recoloca con la cadera medida en la malla de la tortuga y el socket de la carrocería.
		SeatTurtleBase[Seat] = SeatLocations[Seat] - DefaultHipsAboveTurtleOrigin;
		Turtle->SetRelativeLocation(SeatTurtleBase[Seat]);
		Turtle->SetRelativeScale3D(FVector(SeatedTurtleScale));
		// La malla de la tortuga mira a su +Y (como en BP_TortugaCharacter): -90 la pone mirando al morro.
		Turtle->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
		Turtle->SetCollisionProfileName(TEXT("NoCollision"));
		Turtle->SetGenerateOverlapEvents(false);
		Turtle->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		Turtle->SetSkeletalMesh(TurtleFinder.Object);
		Turtle->SetHiddenInGame(true);
		SeatTurtles.Add(Turtle);
		CreateDefaultSubobject<UTN_BuggyRiderAnimComponent>(*FString::Printf(TEXT("%sRiderAnim"), SeatNames[Seat]))
			->Setup(Turtle, Seat == 0 ? ETNBuggyRiderRole::Driver : ETNBuggyRiderRole::Gunner);

		UStaticMeshComponent* Helmet = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("%sHelmet"), SeatNames[Seat]));
		Helmet->SetupAttachment(Turtle);
		Helmet->SetCollisionProfileName(TEXT("NoCollision"));
		SeatHelmets.Add(Helmet);
	}

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(Chassis);
	const TNBuggy::FDriverCameraTuning& CameraTuning = TNBuggy::DefaultDriverCamera();
	SpringArm->SetRelativeLocationAndRotation(CameraPivotLocal, FRotator(CameraPitchDeg, 0.f, 0.f));
	SpringArm->TargetArmLength = CameraTuning.BaseArmCm;
	SpringArm->SocketOffset = FVector(0.f, 0.f, CameraTuning.SocketHeightCm);
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bInheritPitch = false;
	SpringArm->bInheritRoll = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = CameraLagSpeed;
	SpringArm->CameraLagMaxDistance = CameraLagMaxDistance;
	SpringArm->bEnableCameraRotationLag = true;
	SpringArm->CameraRotationLagSpeed = CameraRotationLagSpeed;
	SpringArm->bDoCollisionTest = true;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm);
	Camera->SetFieldOfView(CameraTuning.BaseFov);

	UChaosWheeledVehicleMovementComponent* Move = CastChecked<UChaosWheeledVehicleMovementComponent>(GetVehicleMovementComponent());
	Move->ChassisHeight = 160.f;
	Move->DragCoefficient = 0.1f;
	Move->bEnableCenterOfMassOverride = true;
	Move->CenterOfMassOverride = FVector(0.f, 0.f, 40.f);
	Move->bLegacyWheelFrictionPosition = false;
	Move->WheelSetups.SetNum(WheelCount);
	for (int32 Index = 0; Index < WheelCount; ++Index)
	{
		Move->WheelSetups[Index].WheelClass = Index < FirstRearWheel
			? UTN_BuggyWheelFront::StaticClass()
			: UTN_BuggyWheelRear::StaticClass();
		Move->WheelSetups[Index].BoneName = WheelBoneNames[Index];
	}
	// Provisional: PostInitializeComponents recrea el motor con el ajuste de Data.
	const UTN_BuggyData* Defaults = GetDefault<UTN_BuggyData>();
	Move->EngineSetup.MaxTorque = Defaults->MaxTorque;
	Move->EngineSetup.MaxRPM = Defaults->MaxRPM;
	BuildTorqueCurve(Move->EngineSetup, Defaults->MaxRPM, Defaults->MaxTorque);
	ApplyDifferential(Move->DifferentialSetup, *Defaults);
	Move->SteeringSetup.SteeringType = ESteeringType::AngleRatio;
	Move->SteeringSetup.AngleRatio = TNBuggy::SteerAngleRatio;
	BuildSteeringCurve(Move->SteeringSetup);
	ApplySteeringResponse(*Move, *Defaults);

	SetPhysicsReplicationMode(EPhysicsReplicationMode::PredictiveInterpolation);
}

const UTN_BuggyData* ATN_Buggy::GetData() const
{
	return Data ? Data.Get() : GetDefault<UTN_BuggyData>();
}

UChaosWheeledVehicleMovementComponent* ATN_Buggy::GetWheeledMovement() const
{
	return Cast<UChaosWheeledVehicleMovementComponent>(GetVehicleMovementComponent());
}

double ATN_Buggy::GetServerNow() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

void ATN_Buggy::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	ApplyModelAssets();

	UChaosWheeledVehicleMovementComponent* Move = GetWheeledMovement();
	if (!Move)
	{
		UE_LOG(LogTNBuggy, Error, TEXT("%s: sin UChaosWheeledVehicleMovementComponent"), *GetName());
		return;
	}
	// El motor y la dirección se leen al crear la simulación: se recrean con el ajuste de Data.
	const UTN_BuggyData* Tuning = GetData();
	Move->EngineSetup.MaxTorque = Tuning->MaxTorque;
	Move->EngineSetup.MaxRPM = Tuning->MaxRPM;
	TNBuggyDetail::BuildTorqueCurve(Move->EngineSetup, Tuning->MaxRPM, Tuning->MaxTorque);
	TNBuggyDetail::BuildSteeringCurve(Move->SteeringSetup);
	TNBuggyDetail::ApplySteeringResponse(*Move, *Tuning);
	Move->TransmissionSetup.FinalRatio = Tuning->FinalDriveRatio;
	TNBuggyDetail::ApplyDifferential(Move->DifferentialSetup, *Tuning);
	Move->RecreatePhysicsState();
	ApplyWheelFriction();
}

void ATN_Buggy::BeginPlay()
{
	Super::BeginPlay();
	ApplyWheelFriction();
	ApplyTint();
	BuildTurretVisuals();
	RefreshSeatVisuals(true);
	if (HasAuthority())
	{
		BoostCharge01 = FMath::Clamp(GetData()->BoostStartCharge, 0.f, 1.f);
	}
}

void ATN_Buggy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		DestroyGunnerPawn();
	}
	bBoostActive = false;
	bBoostHeld = false;
	RefreshBoostEffects();
	Super::EndPlay(EndPlayReason);
}

void ATN_Buggy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_Buggy, TeamIndex);
	// La conductora ya conoce su freno de mano: solo lo reciben los demás.
	DOREPLIFETIME_CONDITION(ATN_Buggy, bHandbrakeHeld, COND_SkipOwner);
	DOREPLIFETIME(ATN_Buggy, bDriverSeated);
	DOREPLIFETIME(ATN_Buggy, bGunnerSeated);
	DOREPLIFETIME(ATN_Buggy, DriverPlayerState);
	DOREPLIFETIME(ATN_Buggy, GunnerPlayerState);
	DOREPLIFETIME(ATN_Buggy, GunnerPawn);
	DOREPLIFETIME(ATN_Buggy, bEngineLockedByRace);
	DOREPLIFETIME(ATN_Buggy, bWeaponsLockedByRace);
	DOREPLIFETIME(ATN_Buggy, bRaceBrakeHeld);
	DOREPLIFETIME(ATN_Buggy, LockEndServerTime);
	DOREPLIFETIME(ATN_Buggy, bGhost);
	DOREPLIFETIME(ATN_Buggy, ShieldEndServerTime);
	DOREPLIFETIME(ATN_Buggy, InkEndServerTime);
	DOREPLIFETIME(ATN_Buggy, WobbleEndServerTime);
	DOREPLIFETIME(ATN_Buggy, bInPuddle);
	DOREPLIFETIME(ATN_Buggy, BoostCharge01);
	// La conductora predice su turbo con su botón (IsBoosting): solo lo reciben los demás.
	DOREPLIFETIME_CONDITION(ATN_Buggy, bBoostActive, COND_SkipOwner);
}

void ATN_Buggy::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	FlippedSeconds = TNBuggy::AdvanceFlipped(FlippedSeconds, GetActorUpVector().Z, DeltaSeconds);
	UpdateAirborne();
	if (HasAuthority())
	{
		UpdateServerTimers();
		UpdateSelfRight(DeltaSeconds);
		UpdateBoost(DeltaSeconds);
	}

	TickDrivePhysics();
	if (IsLocallyControlled() && IsPlayerControlled())
	{
		UpdateCamera(DeltaSeconds);
		if (bSelfRightHeld && TNBuggy::AdvanceHold(RespawnHold, true, DeltaSeconds, GetData()->RespawnHoldSeconds))
		{
			ServerRequestRespawn();
		}
	}

	if (GetNetMode() != NM_DedicatedServer)
	{
		UpdateGunnerKnockPose(DeltaSeconds);
		UpdateWheelVisuals();
	}

	SeatLookCheckAccumulator += DeltaSeconds;
	if (SeatLookCheckAccumulator >= 0.5f)
	{
		SeatLookCheckAccumulator = 0.f;
		RefreshSeatVisuals(false);
	}
}

void ATN_Buggy::TickDrivePhysics()
{
	// Reintenta si la simulación no estaba lista; la fricción cambia con el freno de mano y el charco.
	const float Grip = TNRallyTurret::PuddleGripMultiplier(bInPuddle);
	if (!bWheelFrictionApplied || bHandbrakeHeld != bHandbrakeFrictionApplied || !FMath::IsNearlyEqual(Grip, AppliedGripMultiplier))
	{
		ApplyWheelFriction();
	}
	if (IsEngineLocked() != bEngineTorqueLockedApplied || IsBoosting() != bBoostTorqueApplied)
	{
		ApplyEngineTorque();
	}
	if (IsEngineLocked() && (HasAuthority() || IsLocallyControlled()))
	{
		HoldLockedInPlace();
	}
	if (bRaceBrakeHeld && (HasAuthority() || IsLocallyControlled()))
	{
		ApplyRaceBrake();
		HoldOnGrid();
	}
	ApplyBumpKicks();
	ApplyPuddleSpeedCap();
	ApplyAntiRoll();
	ApplyStability();
	ApplyBoostPush();
	RefreshBoostEffects();
	UpdateBoostFlames();
	if (IsLocallyControlled() || (HasAuthority() && !IsPlayerControlled()))
	{
		ApplySteeringAssist();
	}
}

void ATN_Buggy::UpdateServerTimers()
{
	const float Now = static_cast<float>(GetServerNow());
	const bool bPuddleNow = PuddleUntilServerTime > Now;
	if (bPuddleNow != bInPuddle)
	{
		bInPuddle = bPuddleNow;
		ForceNetUpdate();
	}
	const bool bGhostNow = GhostEndServerTime > Now;
	if (bGhostNow != bGhost)
	{
		bGhost = bGhostNow;
		ApplyGhost();
		ForceNetUpdate();
	}
}

bool ATN_Buggy::IsEngineLocked() const
{
	return bEngineLockedByRace || LockEndServerTime > GetServerNow();
}

void ATN_Buggy::ApplyWheelFriction()
{
	UChaosWheeledVehicleMovementComponent* Move = GetWheeledMovement();
	if (!Move || !Move->HasValidPhysicsState())
	{
		return;
	}
	const UTN_BuggyData* Tuning = GetData();
	const float Grip = TNRallyTurret::PuddleGripMultiplier(bInPuddle);
	const float RearFriction = bHandbrakeHeld ? Tuning->HandbrakeRearFriction : Tuning->RearFriction;
	const int32 Wheels = FMath::Min(TNBuggyDetail::WheelCount, Move->WheelSetups.Num());
	for (int32 Index = 0; Index < Wheels; ++Index)
	{
		const bool bFront = Index < TNBuggyDetail::FirstRearWheel;
		Move->SetWheelFrictionMultiplier(Index, (bFront ? Tuning->FrontFriction : RearFriction) * Grip);
		if (bFront)
		{
			// El ángulo del asset de ajuste (#606): la rueda Chaos sale de UTN_BuggyWheelFront con el de serie.
			Move->SetWheelMaxSteerAngle(Index, Tuning->MaxSteerAngleDeg);
		}
	}
	bHandbrakeFrictionApplied = bHandbrakeHeld;
	AppliedGripMultiplier = Grip;
	bWheelFrictionApplied = true;
}

void ATN_Buggy::ApplyEngineTorque()
{
	UChaosWheeledVehicleMovementComponent* Move = GetWheeledMovement();
	if (!Move || !Move->HasValidPhysicsState())
	{
		return;
	}
	const bool bLocked = IsEngineLocked();
	const bool bBoost = IsBoosting();
	const UTN_BuggyData* Tuning = GetData();
	// El par es parte de la simulación (no de la entrada): así el corte y el turbo valen también en el servidor.
	const float Torque = Tuning->MaxTorque * (bBoost ? Tuning->BoostTorqueMultiplier : 1.f);
	Move->SetMaxEngineTorque(bLocked ? 0.f : Torque);
	bEngineTorqueLockedApplied = bLocked;
	bBoostTorqueApplied = bBoost;
}

void ATN_Buggy::HoldLockedInPlace()
{
	// Solo la reaparición (LockEndServerTime) inmoviliza; el semáforo y la salida anticipada solo cortan el motor.
	if (LockEndServerTime <= GetServerNow())
	{
		return;
	}
	USkeletalMeshComponent* Chassis = GetMesh();
	if (Chassis->IsSimulatingPhysics())
	{
		Chassis->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Chassis->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
}

void ATN_Buggy::ApplySteeringAssist()
{
	UChaosWheeledVehicleMovementComponent* Move = GetWheeledMovement();
	if (!Move)
	{
		return;
	}
	const UTN_BuggyData* Tuning = GetData();
	const float Slip = TNBuggy::SlipAngleDeg(GetActorForwardVector(), GetVelocity());
	const float WobbleLeft = WobbleEndServerTime - static_cast<float>(GetServerNow());
	const float Wobble = TNBuggy::SteerWobble(WobbleLeft, TNRallyTurret::CocoWobbleSeconds, Tuning->WobbleAmplitude, Tuning->WobbleFrequency);
	Move->SetSteeringInput(FMath::Clamp(
		TNBuggy::AssistSteer(SteerRequest, Slip, Tuning->CounterSteerAssist, Tuning->MaxAssistAngleDeg, Tuning->CounterSteerStartSlipDeg)
		+ Wobble, -1.f, 1.f));
}

void ATN_Buggy::ApplyBumpKicks()
{
	UChaosWheeledVehicleMovementComponent* Move = GetWheeledMovement();
	USkeletalMeshComponent* Chassis = GetMesh();
	if (!Move || !Move->HasValidPhysicsState() || !Chassis->IsSimulatingPhysics())
	{
		return;
	}
	const int32 Wheels = FMath::Min(Move->Wheels.Num(), Move->WheelSetups.Num());
	PrevContactPoint.SetNumZeroed(Wheels);
	bPrevWheelContact.SetNumZeroed(Wheels);
	BumpTracks.SetNum(Wheels);
	const UTN_BuggyData* BuggyData = GetData();
	TNBuggy::FBumpTuning Tuning;
	Tuning.MinStepCm = BuggyData->BumpMinStepCm;
	Tuning.Scale = BuggyData->BumpKickScale;
	Tuning.MaxKick = BuggyData->BumpKickMax;
	const float Mass = Chassis->GetMass();
	const float ForwardSpeed = Move->GetForwardSpeed();
	for (int32 Index = 0; Index < Wheels; ++Index)
	{
		const UChaosVehicleWheel* Wheel = Move->Wheels[Index];
		if (!Wheel)
		{
			continue;
		}
		const FWheelStatus& State = Move->GetWheelState(Index);
		const FVector Delta = State.ContactPoint - PrevContactPoint[Index];
		const bool bBothInContact = bPrevWheelContact[Index] && State.bInContact;
		// El golpe de un escalón se aplica un frame después, al comprobar que no era una rampa.
		const TNBuggy::FBumpStep Step = TNBuggy::BumpStep(BumpTracks[Index], Delta.Z, ForwardSpeed, Wheel->GetWheelRadius(), bBothInContact, Tuning);
		BumpTracks[Index] = Step.Track;
		if (Step.Kick > 0.f)
		{
			const FVector WheelLocation = Chassis->GetSocketLocation(Move->WheelSetups[Index].BoneName);
			Chassis->AddImpulseAtLocation(GetActorUpVector() * Step.Kick * Mass, WheelLocation);
		}
		PrevContactPoint[Index] = State.ContactPoint;
		bPrevWheelContact[Index] = State.bInContact;
	}
}

void ATN_Buggy::ApplyPuddleSpeedCap()
{
	if (!bInPuddle)
	{
		return;
	}
	USkeletalMeshComponent* Chassis = GetMesh();
	if (!Chassis->IsSimulatingPhysics())
	{
		return;
	}
	const FVector Velocity = GetVelocity();
	const FVector Flat(Velocity.X, Velocity.Y, 0.f);
	const float Decel = TNBuggy::SpeedCapDecel(Flat.Size(), TNRallyTurret::PuddleSpeedCapCms(true), GetData()->PuddleBrakeGain);
	if (Decel > 0.f)
	{
		// Como aceleración (bAccelChange) en el centro de masas, en todas las máquinas que simulan el chasis.
		Chassis->AddForce(-Flat.GetSafeNormal() * Decel, NAME_None, true);
	}
}

void ATN_Buggy::ApplyAntiRoll()
{
	UChaosWheeledVehicleMovementComponent* Move = GetWheeledMovement();
	USkeletalMeshComponent* Chassis = GetMesh();
	if (!Move || !Move->HasValidPhysicsState() || !Chassis->IsSimulatingPhysics())
	{
		return;
	}
	const UTN_BuggyData* Tuning = GetData();
	TNBuggy::FAntiRollTuning AntiRoll;
	AntiRoll.GroundFreeRollDeg = Tuning->AntiRollGroundFreeRollDeg;
	AntiRoll.GroundFreePitchDeg = Tuning->AntiRollGroundFreePitchDeg;
	AntiRoll.Stiffness = Tuning->AntiRollStiffness;
	AntiRoll.Damping = Tuning->AntiRollDamping;
	AntiRoll.MaxAccel = Tuning->AntiRollMaxAccel;
	AntiRoll.GroundRollFullSpeedCms = Tuning->AntiRollGroundRollFullSpeedCms;
	AntiRoll.GroundRollZeroSpeedCms = Tuning->AntiRollGroundRollZeroSpeedCms;
	const FVector Velocity = GetVelocity();
	const float FlatSpeed = static_cast<float>(FVector(Velocity.X, Velocity.Y, 0.f).Size());
	const FVector Accel = TNBuggy::AntiRollAccel(GetActorForwardVector(), GetActorUpVector(),
		Chassis->GetPhysicsAngularVelocityInRadians(), bAirborne, AntiRoll, FlatSpeed);
	if (!Accel.IsNearlyZero())
	{
		// Como aceleración (bAccelChange): igual para cualquier masa e inercia del chasis.
		Chassis->AddTorqueInRadians(Accel, NAME_None, true);
	}
}

void ATN_Buggy::UpdateSelfRight(float DeltaSeconds)
{
	const UTN_BuggyData* Tuning = GetData();
	const TNBuggy::ESelfRight Decision = TNBuggy::DecideSelfRight(FlippedSeconds, bSelfRightRequested,
		Tuning->SelfRightManualDelay, Tuning->SelfRightAutoDelay);
	bSelfRightRequested = false;
	if (Decision != TNBuggy::ESelfRight::None)
	{
		UE_LOG(LogTNBuggy, Log, TEXT("%s: enderezado %s tras %.1f s volcado"), *GetName(),
			Decision == TNBuggy::ESelfRight::Auto ? TEXT("automático") : TEXT("pedido"), FlippedSeconds);
		DoSelfRight();
	}
}

void ATN_Buggy::DoSelfRight()
{
	const FTransform Target = TNBuggy::SelfRightTransform(GetActorTransform(), GetData()->SelfRightLiftCm);
	SetActorLocationAndRotation(Target.GetLocation(), Target.Rotator(), false, nullptr, ETeleportType::TeleportPhysics);
	GetMesh()->SetPhysicsLinearVelocity(FVector::ZeroVector);
	GetMesh()->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	FlippedSeconds = 0.f;
	ForceNetUpdate();
}

bool ATN_Buggy::ServerSelfRight_Validate()
{
	return true;
}

void ATN_Buggy::ServerSelfRight_Implementation()
{
	// Revalidado en UpdateSelfRight: el cliente no decide cuándo se puede enderezar.
	bSelfRightRequested = true;
}

bool ATN_Buggy::ServerRequestRespawn_Validate()
{
	return true;
}

void ATN_Buggy::ServerRequestRespawn_Implementation()
{
	UE_LOG(LogTNBuggy, Log, TEXT("%s: piden reaparecer"), *GetName());
	bRespawnRequested = true;
}

bool ATN_Buggy::ConsumeRespawnRequest()
{
	const bool bWas = bRespawnRequested;
	bRespawnRequested = false;
	return bWas;
}

bool ATN_Buggy::ConsumeDestroyed()
{
	const bool bWas = bDestroyedPending;
	bDestroyedPending = false;
	return bWas;
}

void ATN_Buggy::NotifyDestroyed()
{
	if (HasAuthority())
	{
		bDestroyedPending = true;
	}
}

bool ATN_Buggy::IsRespawnProtected() const
{
	return GhostEndServerTime > static_cast<float>(GetServerNow());
}

bool ATN_Buggy::ConsumeFellOutOfWorld()
{
	const bool bWas = bFellOutOfWorld;
	bFellOutOfWorld = false;
	return bWas;
}

void ATN_Buggy::FellOutOfWorld(const UDamageType& DmgType)
{
	// AActor::FellOutOfWorld destruiría el buggy (y con él el peón de la artillera) y la carrera perdería el equipo. Chaos lo
	// llama en cada paso mientras siga bajo el KillZ: se frena la caída y la carrera lo devuelve a la pista.
	if (!HasAuthority())
	{
		return;
	}
	if (!bFellOutOfWorld)
	{
		UE_LOG(LogTNBuggy, Warning, TEXT("%s: bajo el KillZ (Z %.0f); pide volver a la pista"), *GetName(), GetActorLocation().Z);
	}
	bFellOutOfWorld = true;
	if (USkeletalMeshComponent* Chassis = GetMesh(); Chassis && Chassis->IsSimulatingPhysics())
	{
		Chassis->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Chassis->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
}

void ATN_Buggy::SetRallyTeamIndex(int32 Index)
{
	if (!HasAuthority())
	{
		UE_LOG(LogTNBuggy, Warning, TEXT("%s: SetRallyTeamIndex solo se aplica en el servidor"), *GetName());
		return;
	}
	TeamIndex = Index;
	ApplyTint();
	ForceNetUpdate();
}

void ATN_Buggy::OnRep_TeamIndex()
{
	ApplyTint();
}

void ATN_Buggy::OnRep_Ghost()
{
	ApplyGhost();
}

void ATN_Buggy::ApplyGhost()
{
	// Fantasma: no choca con otros vehículos (la respuesta Ignore de un lado basta para el par).
	GetMesh()->SetCollisionResponseToChannel(ECC_Vehicle, bGhost ? ECR_Ignore : ECR_Block);
	GetMesh()->SetCollisionResponseToChannel(ECC_Pawn, bGhost ? ECR_Ignore : ECR_Block);
}

void ATN_Buggy::RallyTeleport(const FTransform& Where, float LockSeconds, float GhostSeconds)
{
	if (!HasAuthority())
	{
		return;
	}
	// TeleportPhysics: con ResetPhysics la física de Chaos devuelve el chasis a donde estaba en el siguiente paso (medido con
	// TN.Rally.DebugTeleport en UE 5.6) y la reaparición no movía el buggy.
	SetActorLocationAndRotation(Where.GetLocation(), Where.Rotator(), false, nullptr, ETeleportType::TeleportPhysics);
	USkeletalMeshComponent* Chassis = GetMesh();
	Chassis->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Chassis->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	if (UChaosWheeledVehicleMovementComponent* Move = GetWheeledMovement())
	{
		Move->StopMovementImmediately();
	}
	const float Now = static_cast<float>(GetServerNow());
	LockEndServerTime = LockSeconds > 0.f ? Now + LockSeconds : 0.f;
	GhostEndServerTime = GhostSeconds > 0.f ? Now + GhostSeconds : 0.f;
	FlippedSeconds = 0.f;
	UpdateServerTimers();
	ApplyEngineTorque();
	ForceNetUpdate();
}

void ATN_Buggy::SetEngineLocked(bool bLocked)
{
	if (!HasAuthority())
	{
		return;
	}
	bEngineLockedByRace = bLocked;
	ApplyEngineTorque();
	ForceNetUpdate();
}

void ATN_Buggy::SetWeaponsLocked(bool bLocked)
{
	if (!HasAuthority() || bWeaponsLockedByRace == bLocked)
	{
		return;
	}
	bWeaponsLockedByRace = bLocked;
	ForceNetUpdate();
}

float ATN_Buggy::GetForwardSpeedCms() const
{
	const UChaosWheeledVehicleMovementComponent* Move = GetWheeledMovement();
	return Move ? Move->GetForwardSpeed() : 0.f;
}

bool ATN_Buggy::IsFlipped() const
{
	return TNBuggy::IsFlipped(GetActorUpVector().Z);
}

void ATN_Buggy::SetAIDriveInput(float Throttle, float Brake, float Steer, bool bHandbrake)
{
	UChaosWheeledVehicleMovementComponent* Move = GetWheeledMovement();
	if (!Move)
	{
		return;
	}
	// Sin controlador local, Chaos solo procesa las entradas con esto desactivado.
	Move->SetRequiresControllerForInputs(false);
	const bool bLocked = IsEngineLocked() || bRaceBrakeHeld;
	Move->SetThrottleInput(bLocked ? 0.f : FMath::Clamp(Throttle, 0.f, 1.f));
	// Con el freno de carrera manda el freno de estacionamiento (ApplyRaceBrake): el pedal, parado, metería la marcha atrás.
	Move->SetBrakeInput(bRaceBrakeHeld ? 0.f : FMath::Clamp(Brake, 0.f, 1.f));
	SteerRequest = FMath::Clamp(Steer, -1.f, 1.f);
	// El freno de mano baja la fricción trasera (derrape): en la parrilla haría resbalar el buggy cuesta abajo (#611).
	const bool bWantHandbrake = bHandbrake && !bRaceBrakeHeld;
	if (bWantHandbrake != bHandbrakeHeld)
	{
		SetHandbrakeHeld(bWantHandbrake);
	}
	ApplySteeringAssist();
}
