// ATN_Buggy: munición, disparo de la conductora sola y de la IA, e impactos sobre el buggy (coco, mortero, tinta,
// charco de alga y escudo). Todo se decide en el servidor y llega a los clientes como estado replicado.

#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyTurretComponent.h"
#include "Vehicles/TN_RallyProjectile.h"
#include "Vehicles/TN_RallyTurretLogic.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace TNBuggyEffects
{
	/** Altura sobre el origen del blanco a la que apunta el tiro directo (cm): el centro de la carrocería. */
	constexpr float TargetAimUpCm = 60.f;

	/** Cabeceo con el que la conductora sola lanza cada munición si no hay blanco (grados). */
	float LaunchPitchFor(ETNRallyAmmo Ammo)
	{
		switch (Ammo)
		{
		case ETNRallyAmmo::Mortero: return 30.f;
		case ETNRallyAmmo::Alga: return 12.f;
		case ETNRallyAmmo::Burbuja: return 0.f;
		default: return 3.f;
		}
	}
}

void ATN_Buggy::GiveSpecialAmmo(ETNRallyAmmo Ammo, int32 Charges)
{
	if (HasAuthority() && Turret)
	{
		Turret->GiveSpecial(Ammo, Charges);
	}
}

ETNRallyAmmo ATN_Buggy::GetSpecialAmmo() const
{
	return Turret ? Turret->GetSpecialAmmo() : ETNRallyAmmo::None;
}

void ATN_Buggy::AIFire(const FVector& AimWorldDir, bool bSpecial)
{
	if (HasAuthority() && Turret)
	{
		Turret->TryFire(bSpecial, AimWorldDir);
	}
}

void ATN_Buggy::DriverFireAuto(bool bSpecial, bool bBackward)
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !Turret || !World)
	{
		return;
	}
	// El botón principal dispara la munición seleccionada (UTN_BuggyTurretComponent::CycleAmmo); el especial, la especial.
	const bool bFireSpecial = bSpecial || TNRallyTurret::IsSpecial(Turret->GetSelectedAmmo());
	const ETNRallyAmmo Ammo = bFireSpecial ? Turret->GetSpecialAmmo() : ETNRallyAmmo::Coco;
	const FVector Forward = GetActorForwardVector() * (bBackward ? -1.f : 1.f);
	const FVector Origin = Turret->GetComponentLocation();

	TArray<FVector> Candidates;
	for (TActorIterator<ATN_Buggy> It(World); It; ++It)
	{
		if (*It != this)
		{
			Candidates.Add(It->GetActorLocation());
		}
	}
	const int32 Target = TNRallyTurret::PickAutoAimTarget(Origin, Forward, Candidates);
	const bool bLobbed = Ammo == ETNRallyAmmo::Mortero || Ammo == ETNRallyAmmo::Alga;
	UE_LOG(LogTNBuggy, Verbose, TEXT("%s: apuntado automático %s, %d candidatos, blanco %d, origen (%.0f, %.0f, %.0f), dir (%.2f, %.2f)"), *GetName(),
		bBackward ? TEXT("atrás") : TEXT("delante"), Candidates.Num(), Target, Origin.X, Origin.Y, Origin.Z, Forward.X, Forward.Y);
	if (Target != INDEX_NONE && !bLobbed)
	{
		// Tiro directo al centro del blanco (la torreta limita el cabeceo).
		Turret->TryFire(bFireSpecial, (Candidates[Target] + FVector(0.f, 0.f, TNBuggyEffects::TargetAimUpCm)) - Origin);
		return;
	}
	FVector Flat = Target != INDEX_NONE ? Candidates[Target] - Origin : Forward;
	Flat.Z = 0.f;
	Flat = Flat.GetSafeNormal();
	if (Flat.IsNearlyZero())
	{
		Flat = Forward;
	}
	FRotator Aim = Flat.Rotation();
	Aim.Pitch = TNBuggyEffects::LaunchPitchFor(Ammo);
	if (Target != INDEX_NONE)
	{
		// Parabólico (alga y mortero): el cabeceo que hace caer el proyectil en el blanco.
		const TNRallyTurret::FAmmoSpec Spec = TNRallyTurret::SpecFor(Ammo);
		const float Range = static_cast<float>(FVector::Dist2D(Candidates[Target], Origin));
		const float Height = static_cast<float>(Origin.Z - Candidates[Target].Z);
		Aim.Pitch = TNRallyTurret::LobPitchDeg(Range, Height, Spec.SpeedCms, -World->GetGravityZ() * Spec.GravityScale);
	}
	Turret->TryFire(bFireSpecial, Aim.Vector());
}

bool ATN_Buggy::ServerDriverFire_Validate(bool bSpecial, bool bBackward)
{
	return true;
}

void ATN_Buggy::ServerDriverFire_Implementation(bool bSpecial, bool bBackward)
{
	// Con artillera, la torreta es suya: la conductora no dispara.
	if (bGunnerSeated)
	{
		return;
	}
	DriverFireAuto(bSpecial, bBackward);
}

void ATN_Buggy::ApplyVelocityImpulse(const FVector& DeltaVelocity)
{
	if (!HasAuthority() || DeltaVelocity.IsNearlyZero())
	{
		return;
	}
	USkeletalMeshComponent* Chassis = GetMesh();
	if (Chassis->IsSimulatingPhysics())
	{
		// Cambio de velocidad en el centro de masas: no depende de la masa ni genera vuelco.
		Chassis->AddImpulse(DeltaVelocity, NAME_None, true);
	}
	ForceNetUpdate();
}

bool ATN_Buggy::IsShielded() const
{
	return ShieldEndServerTime > GetServerNow();
}

float ATN_Buggy::GetInkSecondsLeft() const
{
	return FMath::Max(0.f, InkEndServerTime - static_cast<float>(GetServerNow()));
}

bool ATN_Buggy::TryConsumeShield()
{
	if (!HasAuthority())
	{
		return false;
	}
	const TNRallyTurret::FImpactOutcome Outcome = TNRallyTurret::ResolveImpact(IsShielded());
	if (!Outcome.bShieldConsumed)
	{
		return false;
	}
	ShieldEndServerTime = 0.f;
	ATN_RallyBurstFX::Broadcast(this, ETNRallyBurstKind::Shield, GetActorLocation(), 250.f);
	UE_LOG(LogTNBuggy, Verbose, TEXT("%s: el escudo anula el impacto"), *GetName());
	ForceNetUpdate();
	return true;
}

void ATN_Buggy::GrantShield()
{
	if (!HasAuthority())
	{
		return;
	}
	ShieldEndServerTime = static_cast<float>(GetServerNow()) + TNRallyTurret::ShieldSeconds;
	UE_LOG(LogTNBuggy, Verbose, TEXT("%s: escudo de burbuja %.0f s"), *GetName(), TNRallyTurret::ShieldSeconds);
	ForceNetUpdate();
}

void ATN_Buggy::ApplyCocoHit(const FVector& HitDir)
{
	if (!HasAuthority() || TryConsumeShield())
	{
		return;
	}
	// Dirección nula: solo el bamboleo. Es lo que pide UTN_BuggyHealthComponent, que ya aplica el empujón en el punto de
	// impacto; con dirección, el empujón lateral de siempre (buggies sin componente de vida).
	if (!HitDir.IsNearlyZero())
	{
		// Empujón lateral: la parte de la dirección del coco perpendicular al morro, todo en horizontal (con el morro
		// inclinado, el Forward sin aplanar dejaba parte del empujón a lo largo).
		const FVector FlatForward = FVector(GetActorForwardVector().X, GetActorForwardVector().Y, 0.f).GetSafeNormal();
		FVector Lateral = FVector(HitDir.X, HitDir.Y, 0.f);
		Lateral -= FVector::DotProduct(Lateral, FlatForward) * FlatForward;
		if (Lateral.IsNearlyZero(0.05f))
		{
			const FVector FlatRight = FVector(-FlatForward.Y, FlatForward.X, 0.f);
			Lateral = FlatRight * (FVector::DotProduct(HitDir, FlatRight) >= 0.f ? 1.f : -1.f);
		}
		ApplyVelocityImpulse(Lateral.GetSafeNormal() * TNRallyTurret::CocoLateralCms);
	}
	WobbleEndServerTime = static_cast<float>(GetServerNow()) + TNRallyTurret::CocoWobbleSeconds;
	ForceNetUpdate();
}

void ATN_Buggy::ApplyMortarBlast()
{
	if (!HasAuthority() || TryConsumeShield())
	{
		return;
	}
	ApplyVelocityImpulse(FVector::UpVector * TNRallyTurret::MortarUpCms);
}

void ATN_Buggy::ApplyInk()
{
	if (!HasAuthority() || TryConsumeShield())
	{
		return;
	}
	InkEndServerTime = static_cast<float>(GetServerNow()) + TNRallyTurret::InkSeconds;
	UE_LOG(LogTNBuggy, Verbose, TEXT("%s: tinta %.0f s"), *GetName(), TNRallyTurret::InkSeconds);
	ForceNetUpdate();
}

void ATN_Buggy::NotePuddleContact()
{
	if (!HasAuthority())
	{
		return;
	}
	// El charco comprueba cada 0,1 s: con 0,25 s de margen el efecto no parpadea.
	PuddleUntilServerTime = static_cast<float>(GetServerNow()) + 0.25f;
	if (!bInPuddle)
	{
		bInPuddle = true;
		ApplyPuddleEntrySpin();
		ForceNetUpdate();
	}
}

void ATN_Buggy::ApplyPuddleEntrySpin()
{
	// Derrape corto al entrar en el charco (#770): un giro de guiñada que decide el servidor (sentido al azar) y que llega a
	// los clientes con el movimiento replicado del chasis; con el agarre del charco, el buggy se va de lado un instante.
	USkeletalMeshComponent* Chassis = GetMesh();
	if (!HasAuthority() || !Chassis || !Chassis->IsSimulatingPhysics())
	{
		return;
	}
	const float SpinDeg = TNRallyTurret::PuddleEntrySpinDegPerSecond(static_cast<float>(GetVelocity().Size2D()), FMath::RandBool());
	if (SpinDeg != 0.f)
	{
		Chassis->AddAngularImpulseInDegrees(GetActorUpVector() * SpinDeg, NAME_None, true);
	}
}
