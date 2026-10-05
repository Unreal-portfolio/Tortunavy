#include "Vehicles/TN_BuggyGunnerPawn.h"
#include "Rally/UI/TN_RallyCopilotTablet.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyData.h"
#include "Vehicles/TN_BuggyInput.h"
#include "Vehicles/TN_BuggyTurretComponent.h"
#include "Vehicles/TN_RallyTracerFX.h"
#include "Vehicles/TN_RallyTurretLogic.h"
#include "VR/TN_VRSeatComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Net/UnrealNetwork.h"

namespace TNGunnerDetail
{
	/** Por encima del hombro: cabeceo base (grados) más una fracción del de la torreta. */
	constexpr float BasePitchDeg = -8.f;
	constexpr float PitchFollow = 0.8f;
	/** Primera persona: sobre el cañón, por delante del pivote (fuera de la cabeza de la tortuga) y algo por encima. */
	const FVector FirstPersonOffset(60.f, 0.f, 22.f);
	/** Retardo de giro en primera persona: muy corto para no estorbar al apuntar. */
	constexpr float FirstPersonRotationLagSpeed = 30.f;
	/** Pasos máximos de un cambio de munición (la rueda puede sumar varios). */
	constexpr int32 MaxCycleSteps = 8;
	/** Empujón de cámara al quedar noqueada: cabeceo y alabeo (grados) y hacia atrás (cm). */
	constexpr float KnockKickPitchDeg = 14.f;
	constexpr float KnockKickRollDeg = 10.f;
	constexpr float KnockKickBackCm = 50.f;
	/** Tope del empujón acumulado. */
	constexpr float MaxKickDeg = 25.f;
	constexpr float MaxKickBackCm = 80.f;
}

ATN_BuggyGunnerPawn::ATN_BuggyGunnerPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	// La posición la da el enganche al buggy en cada máquina (OnRep_Buggy), no el movimiento replicado.
	SetReplicatingMovement(false);
	bAlwaysRelevant = false;
	AutoPossessAI = EAutoPossessAI::Disabled;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(Root);
	// A la altura de la cabeza de la artillera (Muzzle_Gunner), no del pivote, que va 35 cm más arriba (#435).
	SpringArm->SetRelativeLocation(FVector(0.f, 0.f, UTN_BuggyTurretComponent::MuzzleSocketAboveSeatCm));
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->SetUsingAbsoluteRotation(true);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm);
	Camera->SetFieldOfView(90.f);
	ApplyCameraMode();

	// Asiento VR: los ojos de la tortuga sentada (el peón va en la cadera, Seat_Gunner).
	VRSeat = CreateDefaultSubobject<UTN_VRSeatComponent>(TEXT("VRSeat"));
	VRSeat->SetupAttachment(Root);
	VRSeat->SetRelativeLocation(UTN_VRSeatComponent::EyeAboveHip);
}

void ATN_BuggyGunnerPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_BuggyGunnerPawn, Buggy);
}

void ATN_BuggyGunnerPawn::BeginPlay()
{
	Super::BeginPlay();
	// Los valores del Blueprint (EditDefaultsOnly) ya están puestos.
	ApplyCameraMode();
	AttachToBuggy();
}

void ATN_BuggyGunnerPawn::SetBuggy(ATN_Buggy* InBuggy)
{
	if (!HasAuthority())
	{
		return;
	}
	Buggy = InBuggy;
	AttachToBuggy();
	ForceNetUpdate();
}

void ATN_BuggyGunnerPawn::OnRep_Buggy()
{
	AttachToBuggy();
}

void ATN_BuggyGunnerPawn::AttachToBuggy()
{
	if (!Buggy || GetAttachParentActor() == Buggy)
	{
		return;
	}
	AttachToComponent(Buggy->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	SetActorRelativeLocation(ATN_Buggy::GunnerSeatLocal);
	SetActorRelativeRotation(FRotator::ZeroRotator);
}

bool ATN_BuggyGunnerPawn::IsSeatedGunner() const
{
	return Buggy && Controller && Buggy->GetSeatController(ETNRallySeat::Gunner) == Controller && Buggy->GetGunnerPawn() == this;
}

void ATN_BuggyGunnerPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsLocallyControlled() || !Buggy)
	{
		return;
	}
	WatchKnock();
	// Con la vista sentada (gafas o simulada) la cámara es la del asiento: sin brazo ni giro con el apuntado ni empujones.
	if (VRSeat && VRSeat->IsVRView())
	{
		UpdateVRGunner(DeltaSeconds);
	}
	else
	{
		UpdateCamera(DeltaSeconds);
	}
	AimSendAccumulator += DeltaSeconds;
	if (AimSendAccumulator >= 1.f / FMath::Max(AimSendRate, 1.f) && !LocalAim.Equals(LastSentAim, 0.5f))
	{
		AimSendAccumulator = 0.f;
		LastSentAim = LocalAim;
		ServerSetAim(static_cast<float>(LocalAim.Yaw), static_cast<float>(LocalAim.Pitch));
	}
	if (bSelfRightHeld && TNBuggy::AdvanceHold(RespawnHold, true, DeltaSeconds, Buggy->GetData()->RespawnHoldSeconds))
	{
		ServerRequestRespawn();
	}
}

// ── Cámara ────────────────────────────────────────────────────────────────────

void ATN_BuggyGunnerPawn::ApplyCameraMode()
{
	if (bFirstPerson)
	{
		SpringArm->TargetArmLength = 0.f;
		SpringArm->SocketOffset = TNGunnerDetail::FirstPersonOffset;
		SpringArm->bDoCollisionTest = false;
		SpringArm->bEnableCameraLag = false;
		SpringArm->bEnableCameraRotationLag = true;
		SpringArm->CameraRotationLagSpeed = TNGunnerDetail::FirstPersonRotationLagSpeed;
		return;
	}
	SpringArm->TargetArmLength = ShoulderArmLengthCm;
	SpringArm->SocketOffset = ShoulderSocketOffset;
	SpringArm->bDoCollisionTest = true;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = CameraLagSpeed;
	SpringArm->bEnableCameraRotationLag = true;
	SpringArm->CameraRotationLagSpeed = CameraRotationLagSpeed;
}

void ATN_BuggyGunnerPawn::ToggleFirstPerson()
{
	bFirstPerson = !bFirstPerson;
	ApplyCameraMode();
}

void ATN_BuggyGunnerPawn::UpdateCamera(float DeltaSeconds)
{
	// Guiñada del buggy más la del apuntado, sin alabeo. Por encima del hombro sigue parte del cabeceo; en primera persona,
	// todo (la cámara mira por el cañón).
	const float Yaw = static_cast<float>(Buggy->GetActorRotation().Yaw + LocalAim.Yaw);
	const float AimPitch = static_cast<float>(LocalAim.Pitch);
	const float Pitch = bFirstPerson ? AimPitch : TNGunnerDetail::BasePitchDeg + TNGunnerDetail::PitchFollow * AimPitch;
	SpringArm->SetWorldRotation(FRotator(Pitch, Yaw, 0.f));

	KickPitchDeg = FMath::FInterpTo(KickPitchDeg, 0.f, DeltaSeconds, CameraKickRecoverSpeed);
	KickRollDeg = FMath::FInterpTo(KickRollDeg, 0.f, DeltaSeconds, CameraKickRecoverSpeed);
	KickBackCm = FMath::FInterpTo(KickBackCm, 0.f, DeltaSeconds, CameraKickRecoverSpeed);
	Camera->SetRelativeLocationAndRotation(FVector(-KickBackCm, 0.f, 0.f), FRotator(KickPitchDeg, 0.f, KickRollDeg));
}

void ATN_BuggyGunnerPawn::AddCameraKick(float PitchDeg, float RollDeg, float BackCm)
{
	using namespace TNGunnerDetail;
	KickPitchDeg = FMath::Clamp(KickPitchDeg + PitchDeg, -MaxKickDeg, MaxKickDeg);
	// El alabeo cambia de lado al azar: un empujón, no siempre el mismo giro.
	KickRollDeg = FMath::Clamp(KickRollDeg + (FMath::RandBool() ? RollDeg : -RollDeg), -MaxKickDeg, MaxKickDeg);
	KickBackCm = FMath::Clamp(KickBackCm + BackCm, 0.f, MaxKickBackCm);
}

void ATN_BuggyGunnerPawn::WatchKnock()
{
	const UTN_BuggyTurretComponent* Turret = Buggy->GetTurret();
	const bool bKnocked = Turret && Turret->IsGunnerKnocked();
	if (bKnocked && !bWasKnocked)
	{
		AddCameraKick(TNGunnerDetail::KnockKickPitchDeg, TNGunnerDetail::KnockKickRollDeg, TNGunnerDetail::KnockKickBackCm);
	}
	bWasKnocked = bKnocked;
}

void ATN_BuggyGunnerPawn::AddAim(float DeltaYaw, float DeltaPitch)
{
	LocalAim = TNRallyTurret::ClampAim(FRotator(LocalAim.Pitch + DeltaPitch, LocalAim.Yaw + DeltaYaw, 0.f));
}

// ── Entrada ───────────────────────────────────────────────────────────────────

UTN_BuggyInputSet* ATN_BuggyGunnerPawn::GetInputSet()
{
	if (!InputSet)
	{
		InputSet = UTN_BuggyInputSet::Create(this);
	}
	return InputSet;
}

void ATN_BuggyGunnerPawn::EnsureCameraInput()
{
	if (CameraToggleAction)
	{
		return;
	}
	CameraToggleAction = NewObject<UInputAction>(this, TEXT("IA_GunnerCameraToggle"), RF_Transient);
	CameraToggleAction->ValueType = EInputActionValueType::Boolean;
	CameraContext = NewObject<UInputMappingContext>(this, TEXT("IMC_GunnerCamera"), RF_Transient);
	CameraContext->MapKey(CameraToggleAction, EKeys::V);
	CameraContext->MapKey(CameraToggleAction, EKeys::Gamepad_RightThumbstick);
}

void ATN_BuggyGunnerPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		UE_LOG(LogTNBuggy, Error, TEXT("%s: la artillera necesita un UEnhancedInputComponent"), *GetName());
		return;
	}
	const UTN_BuggyInputSet* Set = GetInputSet();
	EnsureCameraInput();
	Input->BindAction(Set->AimMouse, ETriggerEvent::Triggered, this, &ATN_BuggyGunnerPawn::OnAimMouse);
	Input->BindAction(Set->AimStick, ETriggerEvent::Triggered, this, &ATN_BuggyGunnerPawn::OnAimStick);
	Input->BindAction(Set->FireCoco, ETriggerEvent::Triggered, this, &ATN_BuggyGunnerPawn::OnFireCoco);
	Input->BindAction(Set->FireCoco, ETriggerEvent::Completed, this, &ATN_BuggyGunnerPawn::OnFireCocoReleased);
	Input->BindAction(Set->FireSpecial, ETriggerEvent::Started, this, &ATN_BuggyGunnerPawn::OnFireSpecial);
	Input->BindAction(Set->FireSpecial, ETriggerEvent::Triggered, this, &ATN_BuggyGunnerPawn::OnFireSpecialHeld);
	Input->BindAction(Set->CycleAmmo, ETriggerEvent::Started, this, &ATN_BuggyGunnerPawn::OnCycleAmmo);
	Input->BindAction(Set->SelfRight, ETriggerEvent::Started, this, &ATN_BuggyGunnerPawn::OnSelfRightPressed);
	Input->BindAction(Set->SelfRight, ETriggerEvent::Completed, this, &ATN_BuggyGunnerPawn::OnSelfRightReleased);
	Input->BindAction(Set->CallNote, ETriggerEvent::Started, this, &ATN_BuggyGunnerPawn::OnCallNote);
	Input->BindAction(Set->QuickCall, ETriggerEvent::Started, this, &ATN_BuggyGunnerPawn::OnQuickCall);
	// Tableta de copiloto (M o el botón Vista; el Tabulador en el editor abre la pausa).
	UTN_RallyCopilotTablet::BindToggleKeys(PlayerInputComponent, this);
	Input->BindAction(CameraToggleAction, ETriggerEvent::Started, this, &ATN_BuggyGunnerPawn::OnToggleCamera);
}

void ATN_BuggyGunnerPawn::NotifyControllerChanged()
{
	EnsureCameraInput();
	if (InputSet)
	{
		UTN_BuggyInputSet::RemoveContext(Cast<APlayerController>(PreviousController), InputSet->GunnerContext);
	}
	UTN_BuggyInputSet::RemoveContext(Cast<APlayerController>(PreviousController), CameraContext);
	if (const APlayerController* PC = Cast<APlayerController>(Controller); PC && PC->IsLocalController())
	{
		UTN_BuggyInputSet::AddContext(PC, GetInputSet()->GunnerContext);
		UTN_BuggyInputSet::AddContext(PC, CameraContext);
	}
	bSelfRightHeld = false;
	bMainFireLatched = false;
	RespawnHold = TNBuggy::FHold();
	bVRHandle[0] = false;
	bVRHandle[1] = false;
	Super::NotifyControllerChanged();
}

void ATN_BuggyGunnerPawn::OnAimMouse(const FInputActionValue& Value)
{
	const FVector2D Delta = Value.Get<FVector2D>();
	AddAim(Delta.X * MouseDegreesPerUnit, Delta.Y * MouseDegreesPerUnit);
}

void ATN_BuggyGunnerPawn::OnAimStick(const FInputActionValue& Value)
{
	const FVector2D Rate = Value.Get<FVector2D>();
	const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
	AddAim(Rate.X * StickDegreesPerSecond * Dt, Rate.Y * StickDegreesPerSecond * Dt);
}

void ATN_BuggyGunnerPawn::OnFireCoco(const FInputActionValue& Value)
{
	// Botón principal: la munición seleccionada. Mantenerlo pide a su cadencia (el servidor la vuelve a comprobar); con
	// una especial, un disparo por pulsación.
	const UTN_BuggyTurretComponent* Turret = Buggy ? Buggy->GetTurret() : nullptr;
	if (!Turret || Turret->IsGunnerKnocked() || bMainFireLatched)
	{
		return;
	}
	const ETNRallyAmmo Selected = Turret->GetSelectedAmmo();
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastFireRequest >= TNRallyTurret::SpecFor(Selected).FireInterval)
	{
		LastFireRequest = Now;
		// La ráfaga de erizos (#715) se repite mientras se mantiene: el resto de especiales, una por pulsación.
		bMainFireLatched = TNRallyTurret::IsSpecial(Selected) && !TNRallyTurret::IsBurstAmmo(Selected);
		RequestFire(false);
	}
}

void ATN_BuggyGunnerPawn::OnFireCocoReleased(const FInputActionValue& Value)
{
	bMainFireLatched = false;
}

void ATN_BuggyGunnerPawn::OnFireSpecial(const FInputActionValue& Value)
{
	RequestFire(true);
}

void ATN_BuggyGunnerPawn::OnFireSpecialHeld(const FInputActionValue& Value)
{
	// Ráfaga de erizos (#715): mantener el botón especial repite la petición a la cadencia de las púas; el servidor la
	// para si deja de llegar. Las demás especiales salen una vez por pulsación (OnFireSpecial).
	const UTN_BuggyTurretComponent* Turret = Buggy ? Buggy->GetTurret() : nullptr;
	if (!Turret || Turret->IsGunnerKnocked() || !TNRallyTurret::IsBurstAmmo(Turret->GetSpecialAmmo()))
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastFireRequest >= TNRallyTurret::ErizosSpikeInterval)
	{
		LastFireRequest = Now;
		RequestFire(true);
	}
}

void ATN_BuggyGunnerPawn::OnCycleAmmo(const FInputActionValue& Value)
{
	const int32 Direction = UTN_BuggyInputSet::CycleDirection(Value);
	if (UTN_BuggyTurretComponent* Turret = Buggy ? Buggy->GetTurret() : nullptr; Turret && Direction != 0)
	{
		// En un cliente, la torreta lo devuelve a RequestCycleAmmo; en el servidor escucha, lo aplica.
		Turret->CycleAmmo(Direction);
	}
}

void ATN_BuggyGunnerPawn::OnToggleCamera(const FInputActionValue& Value)
{
	ToggleFirstPerson();
}

void ATN_BuggyGunnerPawn::RequestFire(bool bSpecial)
{
	// Con la tableta grande abierta la artillera tiene las manos ocupadas: la torreta no dispara.
	if (UTN_RallyCopilotTablet::IsOpenFor(Cast<APlayerController>(GetController())))
	{
		return;
	}
	// La dirección en mundo es la que ve esta máquina (la de la mira): con ping, el servidor tiene el buggy girado de otra
	// manera y con solo el apuntado relativo el disparo salía desviado (#333).
	const FRotator Aim = TNRallyTurret::ClampAim(LocalAim);
	const FVector WorldDir = Buggy ? TNRallyTurret::AimWorldDirection(Buggy->GetActorRotation(), Aim) : FVector::ZeroVector;
	if (!HasAuthority())
	{
		SpawnLocalTracer(bSpecial, Aim, WorldDir);
	}
	ServerFire(bSpecial, static_cast<float>(LocalAim.Yaw), static_cast<float>(LocalAim.Pitch), WorldDir);
}

void ATN_BuggyGunnerPawn::SpawnLocalTracer(bool bSpecial, const FRotator& Aim, const FVector& WorldDir) const
{
	const UTN_BuggyTurretComponent* Turret = Buggy ? Buggy->GetTurret() : nullptr;
	UWorld* World = GetWorld();
	if (!Turret || !World || WorldDir.IsNearlyZero() || Buggy->AreWeaponsLocked() || Turret->IsGunnerKnocked())
	{
		return;
	}
	// Lo mismo que comprobará el servidor con el estado replicado: calor del coco y cargas de la especial.
	const bool bFireSpecial = bSpecial || TNRallyTurret::IsSpecial(Turret->GetSelectedAmmo());
	const ETNRallyAmmo Ammo = bFireSpecial ? Turret->GetSpecialAmmo() : ETNRallyAmmo::Coco;
	const bool bCanFire = bFireSpecial ? (Ammo != ETNRallyAmmo::None && Turret->GetSpecialCharges() > 0) : !Turret->IsOverheated();
	// Las conchas no vuelan (corren por el suelo): no hay trazador que adelantar.
	if (!bCanFire || TNRallyTurret::IsGroundShell(Ammo))
	{
		return;
	}
	const TNRallyTurret::FAmmoSpec Spec = TNRallyTurret::SpecFor(Ammo);
	const FVector Muzzle = TNRallyTurret::MuzzleWorldLocation(Turret->GetComponentLocation(), Buggy->GetActorRotation(), Aim,
		UTN_BuggyTurretComponent::MuzzleDistanceCm, UTN_BuggyTurretComponent::MuzzleSideCm);
	// Como el proyectil del servidor: hereda la velocidad del buggy salvo la burbuja.
	const FVector Inherited = Ammo == ETNRallyAmmo::Burbuja ? FVector::ZeroVector : Buggy->GetVelocity();
	const ATN_RallyTracerFX* Tracer = ATN_RallyTracerFX::Spawn(World, Ammo, Muzzle, WorldDir * Spec.SpeedCms + Inherited,
		World->GetGravityZ() * Spec.GravityScale);
	UE_LOG(LogTNBuggy, Verbose, TEXT("%s: trazador local de %s %s"), *Buggy->GetName(), *UEnum::GetValueAsString(Ammo),
		Tracer ? TEXT("creado") : TEXT("sin crear (máquina sin pantalla)"));
}

void ATN_BuggyGunnerPawn::RequestCycleAmmo(int32 Direction)
{
	if (Direction != 0)
	{
		ServerCycleAmmo(FMath::Clamp(Direction, -TNGunnerDetail::MaxCycleSteps, TNGunnerDetail::MaxCycleSteps));
	}
}

void ATN_BuggyGunnerPawn::OnSelfRightPressed(const FInputActionValue& Value)
{
	bSelfRightHeld = true;
	RespawnHold = TNBuggy::FHold();
	ServerSelfRight();
}

void ATN_BuggyGunnerPawn::OnSelfRightReleased(const FInputActionValue& Value)
{
	bSelfRightHeld = false;
	RespawnHold = TNBuggy::FHold();
}

// ── RPC ───────────────────────────────────────────────────────────────────────

bool ATN_BuggyGunnerPawn::ServerSetAim_Validate(float Yaw, float Pitch)
{
	return TNRallyTurret::IsAimFinite(Yaw, Pitch);
}

void ATN_BuggyGunnerPawn::ServerSetAim_Implementation(float Yaw, float Pitch)
{
	if (IsSeatedGunner())
	{
		// SetAimRelative limita el cabeceo a -10..+45: un cliente no puede apuntar fuera.
		Buggy->GetTurret()->SetAimRelative(FRotator(Pitch, Yaw, 0.f));
	}
}

bool ATN_BuggyGunnerPawn::ServerFire_Validate(bool bSpecial, float Yaw, float Pitch, FVector_NetQuantizeNormal WorldDir)
{
	return TNRallyTurret::IsAimFinite(Yaw, Pitch) && !WorldDir.ContainsNaN();
}

void ATN_BuggyGunnerPawn::ServerFire_Implementation(bool bSpecial, float Yaw, float Pitch, FVector_NetQuantizeNormal WorldDir)
{
	if (!IsSeatedGunner())
	{
		return;
	}
	UTN_BuggyTurretComponent* Turret = Buggy->GetTurret();
	Turret->SetAimRelative(FRotator(Pitch, Yaw, 0.f));
	const FVector ServerDir = Turret->GetAimWorldDirection();
	const FVector Dir = TNRallyTurret::ResolveClientFireDirection(ServerDir, WorldDir);
	UE_LOG(LogTNBuggy, Verbose, TEXT("%s: dirección del cliente a %.1f° de la del servidor (%s)"), *Buggy->GetName(),
		FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(ServerDir, FVector(WorldDir).GetSafeNormal()), -1.0, 1.0))),
		Dir.Equals(ServerDir) ? TEXT("manda la del servidor") : TEXT("manda la del cliente"));
	const bool bFired = bSpecial ? Turret->TryFire(true, Dir) : Turret->TryFireSelected(Dir);
	UE_LOG(LogTNBuggy, Verbose, TEXT("%s: la artillera %s pide disparo %s: %s"), *Buggy->GetName(), *GetNameSafe(Controller),
		bSpecial ? TEXT("especial") : TEXT("de la munición seleccionada"), bFired ? TEXT("sale") : TEXT("rechazado (cadencia, calor, cargas o noqueo)"));
}

bool ATN_BuggyGunnerPawn::ServerCycleAmmo_Validate(int32 Direction)
{
	return Direction != 0 && FMath::Abs(Direction) <= TNGunnerDetail::MaxCycleSteps;
}

void ATN_BuggyGunnerPawn::ServerCycleAmmo_Implementation(int32 Direction)
{
	if (IsSeatedGunner())
	{
		Buggy->GetTurret()->ApplyCycle(Direction);
	}
}

void ATN_BuggyGunnerPawn::ServerSelfRight_Implementation()
{
	if (IsSeatedGunner())
	{
		// Llamada en el servidor: la RPC del buggy se ejecuta aquí mismo.
		Buggy->ServerSelfRight();
	}
}

void ATN_BuggyGunnerPawn::ServerRequestRespawn_Implementation()
{
	if (IsSeatedGunner())
	{
		Buggy->ServerRequestRespawn();
	}
}
