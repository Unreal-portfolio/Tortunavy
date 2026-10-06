#include "Vehicles/TN_BuggyTurretComponent.h"
#include "Kart/TN_KartShell.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyGunnerPawn.h"
#include "Vehicles/TN_BuggyMath.h"
#include "Vehicles/TN_BuggyRiderAnimComponent.h"
#include "Vehicles/TN_RallyCombatLogic.h"
#include "Vehicles/TN_RallyProjectile.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace TNTurretDetail
{
	/** Pasos máximos de un cambio de munición pedido por un cliente (la rueda puede sumar varios en un frame). */
	constexpr int32 MaxCycleSteps = 8;
	/** Radio del fogonazo básico (cm). */
	constexpr float MuzzleFlashRadiusCm = 45.f;
	/** Retroceso de la cámara de la artillera por disparo: cabeceo (grados) y hacia atrás (cm), más con más retroceso. */
	constexpr float KickPitchPerRecoilCm = 0.02f;
	constexpr float KickBackPerRecoilCm = 0.08f;
	constexpr float MinKickPitchDeg = 1.5f;
	constexpr float MinKickBackCm = 6.f;
	/** Sacudida de la cámara de la conductora local (0..1) por cada cm/s de retroceso del disparo. */
	constexpr float DriverTraumaPerRecoilCm = 0.0006f;
	/** Sacudida de una ráfaga de impacto en el propio buggy, que se apaga a RadiusCm + TraumaFalloffCm. */
	constexpr float BurstTrauma = 0.35f;
	constexpr float BurstTraumaFalloffCm = 400.f;

	float BurstTraumaAt(ETNRallyBurstKind Kind, float DistanceCm, float RadiusCm)
	{
		const bool bHurts = Kind == ETNRallyBurstKind::CocoHit || Kind == ETNRallyBurstKind::Explosion || Kind == ETNRallyBurstKind::Ink;
		if (!bHurts)
		{
			return 0.f;
		}
		return BurstTrauma * FMath::Clamp(1.f - DistanceCm / (RadiusCm + BurstTraumaFalloffCm), 0.f, 1.f);
	}
}

const FName UTN_BuggyTurretComponent::TintTag(TEXT("TNTurretTint"));

UTN_BuggyTurretComponent::UTN_BuggyTurretComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
	ProjectileClass = ATN_RallyProjectile::StaticClass();

	static ConstructorHelpers::FObjectFinder<USoundBase> CocoFinder(TEXT("/Game/Audio/Rally/SFX_Turret_Coco.SFX_Turret_Coco"));
	static ConstructorHelpers::FObjectFinder<USoundBase> AlgaFinder(TEXT("/Game/Audio/Rally/SFX_Turret_Alga.SFX_Turret_Alga"));
	static ConstructorHelpers::FObjectFinder<USoundBase> BurbujaFinder(TEXT("/Game/Audio/Rally/SFX_Turret_Burbuja.SFX_Turret_Burbuja"));
	static ConstructorHelpers::FObjectFinder<USoundBase> MorteroFinder(TEXT("/Game/Audio/Rally/SFX_Turret_Mortero.SFX_Turret_Mortero"));
	static ConstructorHelpers::FObjectFinder<USoundBase> TintaFinder(TEXT("/Game/Audio/Rally/SFX_Turret_Tinta.SFX_Turret_Tinta"));
	static ConstructorHelpers::FObjectFinder<USoundBase> AnclaFinder(TEXT("/Game/Audio/Rally/SFX_Turret_Ancla.SFX_Turret_Ancla"));
	FireSound = CocoFinder.Object;
	FireSoundByAmmo.Add(ETNRallyAmmo::Coco, CocoFinder.Object);
	FireSoundByAmmo.Add(ETNRallyAmmo::Alga, AlgaFinder.Object);
	FireSoundByAmmo.Add(ETNRallyAmmo::Burbuja, BurbujaFinder.Object);
	FireSoundByAmmo.Add(ETNRallyAmmo::Mortero, MorteroFinder.Object);
	FireSoundByAmmo.Add(ETNRallyAmmo::Tinta, TintaFinder.Object);
	FireSoundByAmmo.Add(ETNRallyAmmo::Ancla, AnclaFinder.Object);
	// Ráfaga de erizos (#715): cada púa suena como el coco, el disparo de la torreta más corto.
	FireSoundByAmmo.Add(ETNRallyAmmo::Erizos, CocoFinder.Object);
	// Medusa saltarina (#771): el bote suena como la burbuja.
	FireSoundByAmmo.Add(ETNRallyAmmo::Medusa, BurbujaFinder.Object);
	// Arpón (#772): suena como el ancla.
	FireSoundByAmmo.Add(ETNRallyAmmo::Arpon, AnclaFinder.Object);
	// Pez globo (#773): el lanzamiento suena como el alga.
	FireSoundByAmmo.Add(ETNRallyAmmo::PezGlobo, AlgaFinder.Object);
}

void UTN_BuggyTurretComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UTN_BuggyTurretComponent, AimYaw);
	DOREPLIFETIME(UTN_BuggyTurretComponent, AimPitch);
	DOREPLIFETIME(UTN_BuggyTurretComponent, Heat01);
	DOREPLIFETIME(UTN_BuggyTurretComponent, bOverheated);
	DOREPLIFETIME(UTN_BuggyTurretComponent, SpecialAmmo);
	DOREPLIFETIME(UTN_BuggyTurretComponent, SpecialCharges);
	DOREPLIFETIME(UTN_BuggyTurretComponent, SelectedAmmo);
	DOREPLIFETIME(UTN_BuggyTurretComponent, GunnerKnockEndServerTime);
}

void UTN_BuggyTurretComponent::BeginPlay()
{
	Super::BeginPlay();
	ApplySelectedLook();
}

ATN_Buggy* UTN_BuggyTurretComponent::GetBuggy() const
{
	return Cast<ATN_Buggy>(GetOwner());
}

FRotator UTN_BuggyTurretComponent::GetDisplayAim() const
{
	const ATN_Buggy* Buggy = GetBuggy();
	const ATN_BuggyGunnerPawn* Gunner = Buggy ? Buggy->GetGunnerPawn() : nullptr;
	if (Gunner && Gunner->IsLocallyControlled())
	{
		return Gunner->GetLocalAim();
	}
	return FRotator(AimPitch, AimYaw, 0.f);
}

FVector UTN_BuggyTurretComponent::GetAimWorldDirection() const
{
	const AActor* Owner = GetOwner();
	return TNRallyTurret::AimWorldDirection(Owner ? Owner->GetActorRotation() : FRotator::ZeroRotator, FRotator(AimPitch, AimYaw, 0.f));
}

bool UTN_BuggyTurretComponent::IsGunnerKnocked() const
{
	return GetGunnerKnockSecondsLeft() > 0.f;
}

float UTN_BuggyTurretComponent::GetGunnerKnockSecondsLeft() const
{
	const ATN_Buggy* Buggy = GetBuggy();
	if (!Buggy || GunnerKnockEndServerTime <= 0.f)
	{
		return 0.f;
	}
	return FMath::Max(0.f, GunnerKnockEndServerTime - static_cast<float>(Buggy->GetServerNow()));
}

void UTN_BuggyTurretComponent::SetAimRelative(const FRotator& RelativeAim)
{
	const FRotator Clamped = TNRallyTurret::ClampAim(RelativeAim);
	AimYaw = static_cast<float>(Clamped.Yaw);
	AimPitch = static_cast<float>(Clamped.Pitch);
}

void UTN_BuggyTurretComponent::GiveSpecial(ETNRallyAmmo Ammo, int32 Charges)
{
	Special = TNRallyTurret::Give(Ammo, Charges);
	// Una caja nueva sustituye la munición: la ráfaga que quedara a medias se pierde.
	Burst = TNRallyTurret::FBurst();
	SyncReplicatedState();
}

void UTN_BuggyTurretComponent::KnockGunner(float Seconds)
{
	ATN_Buggy* Buggy = GetBuggy();
	if (!Buggy || !Buggy->HasAuthority() || Seconds <= 0.f)
	{
		return;
	}
	GunnerKnockEndServerTime = static_cast<float>(Buggy->GetServerNow()) + Seconds;
	Buggy->ForceNetUpdate();
	UE_LOG(LogTNBuggy, Verbose, TEXT("%s: artillera noqueada %.1f s"), *Buggy->GetName(), Seconds);
}

void UTN_BuggyTurretComponent::SyncReplicatedState()
{
	Heat01 = HeatState.Heat;
	bOverheated = TNRallyTurret::IsOverheated(HeatState);
	SpecialAmmo = Special.Ammo;
	SpecialCharges = Special.Charges;
	const ETNRallyAmmo Resolved = TNRallyTurret::ResolveSelection(SelectedAmmo, Special);
	if (Resolved != SelectedAmmo)
	{
		SelectedAmmo = Resolved;
		// En el servidor escucha OnRep no se ejecuta.
		ApplySelectedLook();
	}
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
}

// ── Munición seleccionada ─────────────────────────────────────────────────────

void UTN_BuggyTurretComponent::CycleAmmo(int32 Direction)
{
	ATN_Buggy* Buggy = GetBuggy();
	if (!Buggy || Direction == 0)
	{
		return;
	}
	const int32 Steps = FMath::Clamp(Direction, -TNTurretDetail::MaxCycleSteps, TNTurretDetail::MaxCycleSteps);
	if (Buggy->HasAuthority())
	{
		ApplyCycle(Steps);
		return;
	}
	// La artillera no es dueña del buggy: su RPC va por su peón. La conductora sola sí lo es: va por la de la torreta.
	ATN_BuggyGunnerPawn* Gunner = Buggy->GetGunnerPawn();
	if (Gunner && Gunner->IsLocallyControlled())
	{
		Gunner->RequestCycleAmmo(Steps);
	}
	else if (Buggy->IsLocallyControlled())
	{
		ServerCycleAmmo(Steps);
	}
}

bool UTN_BuggyTurretComponent::ServerCycleAmmo_Validate(int32 Direction)
{
	return Direction != 0 && FMath::Abs(Direction) <= TNTurretDetail::MaxCycleSteps;
}

void UTN_BuggyTurretComponent::ServerCycleAmmo_Implementation(int32 Direction)
{
	// Con artillera sentada, la torreta es suya: la conductora no cambia su munición.
	const ATN_Buggy* Buggy = GetBuggy();
	if (Buggy && !Buggy->GetSeatController(ETNRallySeat::Gunner))
	{
		ApplyCycle(Direction);
	}
}

void UTN_BuggyTurretComponent::ApplyCycle(int32 Direction)
{
	ATN_Buggy* Buggy = GetBuggy();
	if (!Buggy || !Buggy->HasAuthority())
	{
		return;
	}
	const ETNRallyAmmo Next = TNRallyTurret::CycleAmmo(SelectedAmmo, Special, Direction);
	if (Next == SelectedAmmo)
	{
		return;
	}
	SelectedAmmo = Next;
	ApplySelectedLook();
	Buggy->ForceNetUpdate();
	UE_LOG(LogTNBuggy, Verbose, TEXT("%s: munición seleccionada %s"), *Buggy->GetName(), *UEnum::GetValueAsString(SelectedAmmo));
}

void UTN_BuggyTurretComponent::OnRep_SelectedAmmo()
{
	ApplySelectedLook();
}

void UTN_BuggyTurretComponent::ApplySelectedLook()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// La caña del cañón (TurretBarrel del buggy, con TintTag) cuelga de la torreta: toma el color de la munición seleccionada.
	for (USceneComponent* Child : GetAttachChildren())
	{
		UStaticMeshComponent* Barrel = Cast<UStaticMeshComponent>(Child);
		if (Barrel && Barrel->ComponentHasTag(TintTag))
		{
			TNRallyLook::Tint(Barrel, TNRallyLook::AmmoColor(SelectedAmmo));
		}
	}
}

// ── Disparo ───────────────────────────────────────────────────────────────────

bool UTN_BuggyTurretComponent::CanFireAmmo(ETNRallyAmmo Ammo, double Now) const
{
	const ATN_Buggy* Buggy = GetBuggy();
	// La carrera bloquea la torreta fuera de Racing y Finishing (ATN_RallyGameMode::ApplyWeaponLocks).
	if (Buggy->AreWeaponsLocked() || IsGunnerKnocked())
	{
		UE_LOG(LogTNBuggy, Verbose, TEXT("%s: torreta bloqueada (carrera o artillera noqueada), no dispara"), *Buggy->GetName());
		return false;
	}
	const TNRallyTurret::FAmmoSpec Spec = TNRallyTurret::SpecFor(Ammo);
	if (TNRallyTurret::IsSpecial(Ammo))
	{
		return Special.Ammo == Ammo && TNRallyTurret::CanFireSpecial(Special) && TNRallyTurret::CadenceOk(Now, LastSpecialShot, Spec.FireInterval);
	}
	return Ammo == ETNRallyAmmo::Coco && TNRallyTurret::CanFireCoco(HeatState) && TNRallyTurret::CadenceOk(Now, LastCocoShot, Spec.FireInterval);
}

bool UTN_BuggyTurretComponent::TryFireSelected(const FVector& WorldDir)
{
	return TryFire(TNRallyTurret::IsSpecial(SelectedAmmo), WorldDir);
}

bool UTN_BuggyTurretComponent::TryFire(bool bSpecial, const FVector& WorldDir)
{
	ATN_Buggy* Buggy = GetBuggy();
	UWorld* World = GetWorld();
	if (!Buggy || !World || !Buggy->HasAuthority() || WorldDir.IsNearlyZero() || WorldDir.ContainsNaN())
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	const ETNRallyAmmo Ammo = bSpecial ? Special.Ammo : ETNRallyAmmo::Coco;
	if (TNRallyTurret::IsBurstAmmo(Ammo))
	{
		return TryHoldBurst(WorldDir, Now);
	}
	if (!CanFireAmmo(Ammo, Now))
	{
		return false;
	}
	// El apuntado se limita como el de la artillera: la dirección final siempre cumple -10..+45 de cabeceo.
	const FRotator Relative = TNRallyTurret::RelativeAimFromWorld(Buggy->GetActorRotation(), WorldDir);
	SetAimRelative(Relative);
	const FVector Dir = TNRallyTurret::AimWorldDirection(Buggy->GetActorRotation(), Relative);
	const FVector Muzzle = TNRallyTurret::MuzzleWorldLocation(GetComponentLocation(), Buggy->GetActorRotation(), Relative,
		MuzzleDistanceCm, MuzzleSideCm);
	if (!LaunchAmmo(Ammo, Dir, Muzzle))
	{
		return false;
	}
	CommitShot(Ammo, Now);
	ApplyRecoil(Ammo, Dir);
	SyncReplicatedState();
	MulticastFired(Ammo, Muzzle, Dir);
	UE_LOG(LogTNBuggy, Verbose, TEXT("%s: disparo %s dir=(%.2f, %.2f, %.2f) calor=%.2f cargas=%d"), *Buggy->GetName(),
		*UEnum::GetValueAsString(Ammo), Dir.X, Dir.Y, Dir.Z, HeatState.Heat, Special.Charges);
	return true;
}

bool UTN_BuggyTurretComponent::LaunchAmmo(ETNRallyAmmo Ammo, const FVector& Dir, const FVector& Muzzle)
{
	if (TNRallyTurret::IsSelfAmmo(Ammo))
	{
		// Medusa saltarina (#771): sin proyectil; el bote es un impulso vertical en el servidor, como el del mortero. En el
		// aire no se puede usar (no se gasta la carga).
		ATN_Buggy* Self = GetBuggy();
		if (!TNRallyTurret::CanHop(Self->IsAirborne()))
		{
			return false;
		}
		Self->ApplyVelocityImpulse(FVector::UpVector * TNRallyTurret::JellyfishUpCms);
		ATN_RallyBurstFX::Broadcast(Self, ETNRallyBurstKind::BubblePop, Self->GetActorLocation(), 220.f);
		return true;
	}
	if (!TNRallyTurret::IsGroundShell(Ammo))
	{
		return SpawnProjectile(Ammo, Dir, Muzzle) != nullptr;
	}
	// Conchas de las cajas «?» (#629): salen de la boca hacia donde apunta la torreta y corren pegadas al suelo; la
	// teledirigida persigue al buggy de justo delante si se dispara hacia delante.
	ATN_Buggy* Buggy = GetBuggy();
	const bool bHoming = Ammo == ETNRallyAmmo::ConchaGuiada;
	ATN_Buggy* Target = bHoming ? ATN_KartShell::FindHomingTarget(GetWorld(), Buggy, Dir) : nullptr;
	const ATN_KartShell* Shell = ATN_KartShell::LaunchShell(GetWorld(), Buggy, Muzzle, Dir, Target, bHoming);
	if (!Shell)
	{
		UE_LOG(LogTNBuggy, Error, TEXT("%s: no se ha podido crear la concha de %s"), *Buggy->GetName(), *UEnum::GetValueAsString(Ammo));
	}
	return Shell != nullptr;
}

ATN_RallyProjectile* UTN_BuggyTurretComponent::SpawnProjectile(ETNRallyAmmo Ammo, const FVector& Dir, const FVector& Muzzle)
{
	ATN_Buggy* Buggy = GetBuggy();
	UClass* Class = ProjectileClass ? ProjectileClass.Get() : ATN_RallyProjectile::StaticClass();
	const FTransform Where(Dir.Rotation(), Muzzle);
	ATN_RallyProjectile* Projectile = GetWorld()->SpawnActorDeferred<ATN_RallyProjectile>(Class, Where, Buggy, Buggy,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Projectile)
	{
		UE_LOG(LogTNBuggy, Error, TEXT("%s: no se ha podido crear el proyectil de %s"), *Buggy->GetName(), *UEnum::GetValueAsString(Ammo));
		return nullptr;
	}
	// El proyectil hereda la velocidad del buggy (si no, al disparar hacia delante el buggy lo alcanzaría), salvo la
	// burbuja: es lenta a propósito para que la pueda coger cualquiera, también el propio buggy.
	const FVector Inherited = Ammo == ETNRallyAmmo::Burbuja ? FVector::ZeroVector : Buggy->GetVelocity();
	// La dirección de la velocidad resultante es la del disparo, no la del cañón más la del buggy (#717).
	Projectile->Init(Ammo, TNRallyTurret::ShotVelocity(Dir, Inherited, TNRallyTurret::SpecFor(Ammo).SpeedCms), Buggy);
	Projectile->FinishSpawning(Where);
	// Que salga hacia los clientes cuanto antes: simulan desde su posición y velocidad de salida.
	Projectile->ForceNetUpdate();
	return Projectile;
}

void UTN_BuggyTurretComponent::CommitShot(ETNRallyAmmo Ammo, double Now)
{
	if (TNRallyTurret::IsSpecial(Ammo))
	{
		Special = TNRallyTurret::AfterSpecialShot(Special);
		LastSpecialShot = Now;
	}
	else
	{
		HeatState = TNRallyTurret::AfterCocoShot(HeatState);
		LastCocoShot = Now;
	}
}

void UTN_BuggyTurretComponent::ApplyRecoil(ETNRallyAmmo Ammo, const FVector& Dir)
{
	ATN_Buggy* Buggy = GetBuggy();
	const float RecoilCms = TNRallyTurret::SpecFor(Ammo).RecoilCms;
	// Horizontal: hacia delante frena, hacia atrás acelera.
	Buggy->ApplyVelocityImpulse(TNRallyTurret::RecoilVelocity(Dir, RecoilCms));
	USkeletalMeshComponent* Chassis = Buggy->GetMesh();
	if (!Chassis->IsSimulatingPhysics())
	{
		return;
	}
	// Vertical: levanta el extremo hacia el que se dispara (el morro al disparar hacia delante).
	const FTransform& Xf = Buggy->GetActorTransform();
	const TNRallyTurret::FRecoilLift Lift = TNRallyTurret::RecoilLift(Xf.InverseTransformVectorNoScale(Dir), RecoilCms,
		TNRallyCombat::DefaultHalfLengthCm);
	if (Lift.LiftCms > 0.f)
	{
		Chassis->AddImpulseAtLocation(Buggy->GetActorUpVector() * Lift.LiftCms * Chassis->GetMass(), Xf.TransformPosition(Lift.LocalPoint));
	}
}

void UTN_BuggyTurretComponent::MulticastFired_Implementation(ETNRallyAmmo Ammo, FVector_NetQuantize Muzzle, FVector_NetQuantizeNormal Dir)
{
	using namespace TNTurretDetail;
	UWorld* World = GetWorld();
	if (!World || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	ATN_RallyBurstFX::Spawn(World, ETNRallyBurstKind::MuzzleFlash, Muzzle, MuzzleFlashRadiusCm);
	if (MuzzleFlashFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, MuzzleFlashFX, Muzzle, FVector(Dir).Rotation());
	}
	const TObjectPtr<USoundBase>* ForAmmo = FireSoundByAmmo.Find(Ammo);
	if (USoundBase* Sound = ForAmmo && *ForAmmo ? ForAmmo->Get() : FireSound.Get())
	{
		UGameplayStatics::SpawnSoundAtLocation(World, Sound, Muzzle);
	}
	ATN_Buggy* Buggy = GetBuggy();
	if (!Buggy)
	{
		return;
	}
	// La artillera se echa atrás en cada máquina en cuanto llega el disparo (si el multicast se pierde, la detecta por el
	// calor y las cargas replicadas; los dos avisos cuentan una vez).
	if (UTN_BuggyRiderAnimComponent* Rider = UTN_BuggyRiderAnimComponent::FindForRole(Buggy, ETNBuggyRiderRole::Gunner))
	{
		Rider->NotifyShot(FVector(Dir));
	}
	const float Recoil = TNRallyTurret::SpecFor(Ammo).RecoilCms;
	// Solo actúa en la máquina de la conductora local.
	Buggy->AddCameraTrauma(Recoil * DriverTraumaPerRecoilCm);
	ATN_BuggyGunnerPawn* Gunner = Buggy->GetGunnerPawn();
	if (Gunner && Gunner->IsLocallyControlled())
	{
		Gunner->AddCameraKick(FMath::Max(MinKickPitchDeg, Recoil * KickPitchPerRecoilCm), 0.f, FMath::Max(MinKickBackCm, Recoil * KickBackPerRecoilCm));
	}
}

void UTN_BuggyTurretComponent::MulticastBurst_Implementation(ETNRallyBurstKind Kind, FVector_NetQuantize Where, float RadiusCm)
{
	ATN_RallyBurstFX::Spawn(GetWorld(), Kind, Where, RadiusCm);
	// La ráfaga llega por la torreta del buggy alcanzado (o del que disparó si no alcanzó a nadie): sacude su cámara si
	// el impacto fue cerca.
	if (ATN_Buggy* Buggy = GetBuggy())
	{
		const float Distance = static_cast<float>(FVector::Dist(Buggy->GetActorLocation(), Where));
		Buggy->AddCameraTrauma(TNTurretDetail::BurstTraumaAt(Kind, Distance, RadiusCm));
	}
}

void UTN_BuggyTurretComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority())
	{
		HeatState = TNRallyTurret::Cool(HeatState, DeltaTime);
		TickBurst(GetWorld()->GetTimeSeconds());
		// Replica a saltos de 0,05 o al cambiar el sobrecalentamiento: el HUD no necesita más.
		if (bOverheated != TNRallyTurret::IsOverheated(HeatState) || FMath::Abs(HeatState.Heat - Heat01) >= 0.05f
			|| (HeatState.Heat == 0.f && Heat01 != 0.f))
		{
			Heat01 = HeatState.Heat;
			bOverheated = TNRallyTurret::IsOverheated(HeatState);
		}
	}
	// La torreta gira con el apuntado que se ve en esta máquina; su base, solo en guiñada.
	const FRotator Aim = GetDisplayAim();
	SetRelativeRotation(Aim);
	if (USceneComponent* Follower = YawFollower.Get())
	{
		Follower->SetRelativeRotation(FRotator(0.f, Aim.Yaw, 0.f));
	}
}

// ── Ráfaga de erizos (#715) ───────────────────────────────────────────────────

bool UTN_BuggyTurretComponent::IsHumanTrigger() const
{
	const ATN_Buggy* Buggy = GetBuggy();
	if (!Buggy)
	{
		return false;
	}
	// Con artillera, el gatillo es suyo; si no, de la conductora sola.
	const AController* Shooter = Buggy->GetSeatController(ETNRallySeat::Gunner);
	if (!Shooter)
	{
		Shooter = Buggy->GetSeatController(ETNRallySeat::Driver);
	}
	return Shooter && Shooter->IsPlayerController();
}

bool UTN_BuggyTurretComponent::TryHoldBurst(const FVector& WorldDir, double Now)
{
	ATN_Buggy* Buggy = GetBuggy();
	// Una ráfaga empezada solo necesita el gatillo apretado (y la torreta libre); una nueva, todas las comprobaciones.
	const bool bStarted = TNRallyTurret::IsBurstActive(Burst);
	if (bStarted ? (Buggy->AreWeaponsLocked() || IsGunnerKnocked()) : !CanFireAmmo(Special.Ammo, Now))
	{
		return false;
	}
	SetAimRelative(TNRallyTurret::RelativeAimFromWorld(Buggy->GetActorRotation(), WorldDir));
	Burst = TNRallyTurret::HoldBurst(Burst, Now, TNRallyTurret::BurstHoldSeconds(IsHumanTrigger()));
	TickBurst(Now);
	return true;
}

void UTN_BuggyTurretComponent::TickBurst(double Now)
{
	ATN_Buggy* Buggy = GetBuggy();
	if (!Buggy || !TNRallyTurret::IsBurstActive(Burst))
	{
		return;
	}
	if (!TNRallyTurret::IsBurstAmmo(Special.Ammo))
	{
		Burst = TNRallyTurret::FBurst();
		return;
	}
	if (!TNRallyTurret::BurstSpikeDue(Burst, Now) || Buggy->AreWeaponsLocked() || IsGunnerKnocked())
	{
		return;
	}
	// Cada púa sale hacia donde apunta ahora la torreta (la artillera sigue apuntando durante la ráfaga).
	const FRotator Aim(AimPitch, AimYaw, 0.f);
	const FVector Dir = TNRallyTurret::AimWorldDirection(Buggy->GetActorRotation(), Aim);
	const FVector Muzzle = TNRallyTurret::MuzzleWorldLocation(GetComponentLocation(), Buggy->GetActorRotation(), Aim,
		MuzzleDistanceCm, MuzzleSideCm);
	const ETNRallyAmmo Ammo = Special.Ammo;
	if (!LaunchAmmo(Ammo, Dir, Muzzle))
	{
		return;
	}
	Burst = TNRallyTurret::AfterBurstSpike(Burst, Now);
	LastSpecialShot = Now;
	if (!TNRallyTurret::IsBurstActive(Burst))
	{
		// Última púa: se gasta la carga.
		Special = TNRallyTurret::AfterSpecialShot(Special);
	}
	ApplyRecoil(Ammo, Dir);
	SyncReplicatedState();
	MulticastFired(Ammo, Muzzle, Dir);
}
