#include "Vehicles/TN_BuggyHealthComponent.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyGunnerPawn.h"
#include "Vehicles/TN_BuggyTurretComponent.h"
#include "Vehicles/TN_RallyAnchor.h"
#include "Vehicles/TN_RallyCombatLogic.h"
#include "Vehicles/TN_RallyProjectile.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_ShellComponent.h"
#include "Player/TortugaCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

const FName UTN_BuggyHealthComponent::GunnerHitboxTag(TEXT("TN_GunnerHitbox"));

namespace TNBuggyHealthDetail
{
	/** Esfera de la artillera: centro sobre el asiento (cm) y radio. Cubre el torso y la cabeza de la tortuga sentada. */
	constexpr float GunnerHitboxUpCm = 45.f;
	constexpr float GunnerHitboxRadiusCm = 45.f;
	/** Radio de la nube de arena al reventar (cm). */
	constexpr float SandCloudRadiusCm = 450.f;
	constexpr float SparksRadiusCm = 70.f;
	/** Humo: encima del motor, en el espacio de la carrocería (cm). */
	const FVector SmokeOffset(-60.f, 0.f, 90.f);
	/** Por debajo de esta medida (cm) la caja de la malla no es fiable y se usan las medidas por defecto. */
	constexpr float MinReliableExtentCm = 40.f;
	/** Sacudida de cámara al reventar: cabeceo y alabeo de la artillera (grados), retroceso (cm) y trauma de la conductora. */
	constexpr float ExplodeKickPitchDeg = 10.f;
	constexpr float ExplodeKickRollDeg = 8.f;
	constexpr float ExplodeKickBackCm = 40.f;
	constexpr float ExplodeTrauma = 1.f;
	/** Sacudida de cámara en un choque fuerte (mismas unidades). */
	constexpr float CrashKickPitchDeg = 3.f;
	constexpr float CrashKickRollDeg = 5.f;
	constexpr float CrashKickBackCm = 15.f;
	constexpr float CrashTrauma = 0.4f;

	/** Empujón de cámara a la artillera local de Buggy (reventón o choque). */
	void KickLocalGunner(const ATN_Buggy* Buggy, float PitchDeg, float RollDeg, float BackCm)
	{
		ATN_BuggyGunnerPawn* Gunner = Buggy ? Buggy->GetGunnerPawn() : nullptr;
		if (Gunner && Gunner->IsLocallyControlled())
		{
			Gunner->AddCameraKick(PitchDeg, RollDeg, BackCm);
		}
	}
}

UTN_BuggyHealthComponent::UTN_BuggyHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	static ConstructorHelpers::FObjectFinder<USoundBase> ExplosionFinder(TEXT("/Game/Audio/Rally/SFX_Buggy_Explode.SFX_Buggy_Explode"));
	static ConstructorHelpers::FObjectFinder<USoundBase> CrashFinder(TEXT("/Game/Audio/Rally/SFX_Buggy_Crash.SFX_Buggy_Crash"));
	ExplosionSound = ExplosionFinder.Object;
	CrashSound = CrashFinder.Object;
}

void UTN_BuggyHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UTN_BuggyHealthComponent, Health);
}

ATN_Buggy* UTN_BuggyHealthComponent::GetBuggy() const
{
	return Cast<ATN_Buggy>(GetOwner());
}

float UTN_BuggyHealthComponent::GetHealth01() const
{
	return MaxHealth > 0.f ? FMath::Clamp(Health / MaxHealth, 0.f, 1.f) : 0.f;
}

bool UTN_BuggyHealthComponent::IsSmoking() const
{
	return TNRallyCombat::IsSmoking(Health, MaxHealth);
}

void UTN_BuggyHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	ATN_Buggy* Buggy = GetBuggy();
	if (!Buggy)
	{
		UE_LOG(LogTNBuggy, Warning, TEXT("%s: UTN_BuggyHealthComponent necesita un ATN_Buggy como dueño"), *GetNameSafe(GetOwner()));
		return;
	}
	CreateGunnerHitbox();
	if (Buggy->HasAuthority())
	{
		Health = MaxHealth;
		// Enlazado desde aquí para no tocar el buggy: el chasis avisa de sus contactos físicos (choques y atropellos).
		USkeletalMeshComponent* Chassis = Buggy->GetMesh();
		Chassis->SetNotifyRigidBodyCollision(true);
		Chassis->OnComponentHit.AddDynamic(this, &UTN_BuggyHealthComponent::OnChassisHit);
	}
	UpdateSmoke();
}

void UTN_BuggyHealthComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RestoreTimer);
		World->GetTimerManager().ClearTimer(SmokePuffTimer);
	}
	if (ATN_Buggy* Buggy = GetBuggy())
	{
		Buggy->GetMesh()->OnComponentHit.RemoveDynamic(this, &UTN_BuggyHealthComponent::OnChassisHit);
	}
	Super::EndPlay(EndPlayReason);
}

void UTN_BuggyHealthComponent::CreateGunnerHitbox()
{
	using namespace TNBuggyHealthDetail;
	ATN_Buggy* Buggy = GetBuggy();
	if (!Buggy || GunnerHitbox)
	{
		return;
	}
	// Solo para los proyectiles (WorldDynamic): no choca con la física, las tortugas, la cámara ni las ruedas propias.
	GunnerHitbox = NewObject<USphereComponent>(Buggy, TEXT("GunnerHitbox"));
	GunnerHitbox->SetupAttachment(Buggy->GetMesh());
	GunnerHitbox->SetRelativeLocation(ATN_Buggy::GunnerSeatLocal + FVector(0.f, 0.f, GunnerHitboxUpCm));
	GunnerHitbox->InitSphereRadius(GunnerHitboxRadiusCm);
	GunnerHitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	GunnerHitbox->SetCollisionObjectType(ECC_Vehicle);
	GunnerHitbox->SetCollisionResponseToAllChannels(ECR_Ignore);
	GunnerHitbox->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	GunnerHitbox->SetGenerateOverlapEvents(false);
	GunnerHitbox->ComponentTags.Add(GunnerHitboxTag);
	GunnerHitbox->RegisterComponent();
}

FVector2D UTN_BuggyHealthComponent::GetHalfExtents() const
{
	using namespace TNBuggyHealthDetail;
	const ATN_Buggy* Buggy = GetBuggy();
	const UStaticMeshComponent* Body = Buggy ? Buggy->GetBody() : nullptr;
	const UStaticMesh* Mesh = Body ? Body->GetStaticMesh() : nullptr;
	if (!Mesh)
	{
		return FVector2D(TNRallyCombat::DefaultHalfLengthCm, TNRallyCombat::DefaultHalfWidthCm);
	}
	const FVector Extent = Mesh->GetBoundingBox().GetExtent() * Body->GetRelativeScale3D().GetAbs();
	// El buggy es más largo que ancho: así no importa hacia qué eje de la malla mire la carrocería.
	const float Length = static_cast<float>(FMath::Max(Extent.X, Extent.Y));
	const float Width = static_cast<float>(FMath::Min(Extent.X, Extent.Y));
	if (Width < MinReliableExtentCm)
	{
		return FVector2D(TNRallyCombat::DefaultHalfLengthCm, TNRallyCombat::DefaultHalfWidthCm);
	}
	return FVector2D(Length, Width);
}

// ── Impactos de munición ──────────────────────────────────────────────────────

bool UTN_BuggyHealthComponent::ReceiveAmmoHit(ETNRallyAmmo Ammo, const FVector& WorldPoint, const FVector& ShotDir, bool bGunnerHit)
{
	ATN_Buggy* Buggy = GetBuggy();
	if (!Buggy || !Buggy->HasAuthority())
	{
		return false;
	}
	// Fantasma tras reaparecer: el impacto no le hace nada (ni gasta el escudo).
	if (Buggy->IsRespawnProtected())
	{
		return false;
	}
	if (Buggy->TryConsumeShield())
	{
		return false;
	}
	ApplyAmmoPush(Ammo, WorldPoint, ShotDir);
	ApplyAmmoEffect(Ammo, WorldPoint);
	// Solo hay a quién noquear si la plaza de la artillera está ocupada.
	if (bGunnerHit && Buggy->GetSeatController(ETNRallySeat::Gunner) && Buggy->GetTurret())
	{
		Buggy->GetTurret()->KnockGunner(TNRallyCombat::GunnerKnockSeconds);
	}
	ApplyDamage(TNRallyCombat::ImpactFor(Ammo).Damage);
	return true;
}

void UTN_BuggyHealthComponent::ApplyAmmoPush(ETNRallyAmmo Ammo, const FVector& WorldPoint, const FVector& ShotDir)
{
	ATN_Buggy* Buggy = GetBuggy();
	USkeletalMeshComponent* Chassis = Buggy->GetMesh();
	if (!Chassis->IsSimulatingPhysics())
	{
		return;
	}
	const FTransform& Xf = Buggy->GetActorTransform();
	const FVector2D Half = GetHalfExtents();
	const TNRallyCombat::FImpactPush Push = TNRallyCombat::ComputeImpactPush(Ammo, Xf.InverseTransformPosition(WorldPoint),
		Xf.InverseTransformVectorNoScale(ShotDir), static_cast<float>(Half.X), static_cast<float>(Half.Y));
	if (Push.LocalImpulseCms.IsNearlyZero())
	{
		return;
	}
	// Cambio de velocidad por la masa: el mismo empujón en cualquier buggy; aplicado fuera del centro, también gira.
	const FVector Impulse = Xf.TransformVectorNoScale(Push.LocalImpulseCms) * Chassis->GetMass();
	Chassis->AddImpulseAtLocation(Impulse, Xf.TransformPosition(Push.LocalPoint));
	Buggy->ForceNetUpdate();
	UE_LOG(LogTNBuggy, Verbose, TEXT("%s: %s en zona %d, empujón local (%.0f, %.0f, %.0f) cm/s"), *Buggy->GetName(),
		*UEnum::GetValueAsString(Ammo), static_cast<int32>(Push.Zone), Push.LocalImpulseCms.X, Push.LocalImpulseCms.Y, Push.LocalImpulseCms.Z);
}

void UTN_BuggyHealthComponent::ApplyAmmoEffect(ETNRallyAmmo Ammo, const FVector& WorldPoint)
{
	ATN_Buggy* Buggy = GetBuggy();
	switch (Ammo)
	{
	case ETNRallyAmmo::Coco:
		// Dirección nula: solo el bamboleo; el empujón ya va en el punto de impacto.
		Buggy->ApplyCocoHit(FVector::ZeroVector);
		break;
	case ETNRallyAmmo::Tinta:
		Buggy->ApplyInk();
		break;
	case ETNRallyAmmo::Mortero:
		Buggy->ApplyMortarBlast();
		break;
	case ETNRallyAmmo::Ancla:
		ATN_RallyAnchorTether::Attach(Buggy, WorldPoint);
		break;
	case ETNRallyAmmo::Erizos:
		// Empujón lateral lejos del lado donde da la púa.
		Buggy->ApplySpikeHit(Buggy->GetActorLocation() - WorldPoint);
		break;
	default:
		break;
	}
}

// ── Vida ──────────────────────────────────────────────────────────────────────

void UTN_BuggyHealthComponent::ApplyDamage(float Amount)
{
	ATN_Buggy* Buggy = GetBuggy();
	UWorld* World = GetWorld();
	if (!Buggy || !World || !Buggy->HasAuthority() || Amount <= 0.f || TNRallyCombat::IsDestroyed(Health))
	{
		return;
	}
	if (World->GetTimeSeconds() < InvulnerableUntil || Buggy->IsRespawnProtected())
	{
		return;
	}
	Health = TNRallyCombat::ApplyDamage(Health, Amount, MaxHealth);
	UpdateSmoke();
	Buggy->ForceNetUpdate();
	UE_LOG(LogTNBuggy, Verbose, TEXT("%s: -%.0f de vida, queda %.0f"), *Buggy->GetName(), Amount, Health);
	if (TNRallyCombat::IsDestroyed(Health))
	{
		Explode();
	}
}

void UTN_BuggyHealthComponent::Explode()
{
	ATN_Buggy* Buggy = GetBuggy();
	UWorld* World = GetWorld();
	MulticastExplode(Buggy->GetActorLocation());
	InvulnerableUntil = World->GetTimeSeconds() + TNRallyCombat::DeathInvulnerableSeconds;
	// La carrera lo recoge en su bucle (ATN_RallyGameMode::ConsumeRespawnRequests) y lo hace reaparecer siempre, con su
	// propio motivo: no es una petición de R y no espera a que acabe la inmunidad de una reaparición anterior.
	Buggy->NotifyDestroyed();
	World->GetTimerManager().SetTimer(RestoreTimer, this, &UTN_BuggyHealthComponent::RestoreAfterDeath,
		TNRallyCombat::DeathRestoreSeconds, false);
	UE_LOG(LogTNBuggy, Log, TEXT("%s: revienta"), *Buggy->GetName());
}

void UTN_BuggyHealthComponent::RestoreAfterDeath()
{
	Health = MaxHealth;
	UpdateSmoke();
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
}

void UTN_BuggyHealthComponent::OnRep_Health()
{
	UpdateSmoke();
}

void UTN_BuggyHealthComponent::UpdateSmoke()
{
	ATN_Buggy* Buggy = GetBuggy();
	if (!Buggy || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const bool bSmoke = IsSmoking();
	if (bSmoke && !SmokeComponent && SmokeFX)
	{
		USceneComponent* AttachTo = Buggy->GetBody() ? static_cast<USceneComponent*>(Buggy->GetBody()) : Buggy->GetRootComponent();
		SmokeComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(SmokeFX, AttachTo, NAME_None, TNBuggyHealthDetail::SmokeOffset,
			FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false);
	}
	else if (!bSmoke && SmokeComponent)
	{
		SmokeComponent->DestroyComponent();
		SmokeComponent = nullptr;
	}
	// Sin Niagara, humo de malla propia: SpawnSmokePuff se reprograma solo mientras dure el humo.
	if (bSmoke && !SmokeFX && !IsEmittingSmokePuffs())
	{
		SpawnSmokePuff();
	}
	else if (!bSmoke && IsEmittingSmokePuffs())
	{
		GetWorld()->GetTimerManager().ClearTimer(SmokePuffTimer);
	}
}

bool UTN_BuggyHealthComponent::IsEmittingSmokePuffs() const
{
	const UWorld* World = GetWorld();
	return World && World->GetTimerManager().IsTimerActive(SmokePuffTimer);
}

void UTN_BuggyHealthComponent::SpawnSmokePuff()
{
	ATN_Buggy* Buggy = GetBuggy();
	UWorld* World = GetWorld();
	const float PuffsPerSecond = TNRallyCombat::SmokePuffsPerSecond(Health, MaxHealth);
	if (!Buggy || !World || PuffsPerSecond <= 0.f || Buggy->IsActorBeingDestroyed())
	{
		return;
	}
	const USceneComponent* From = Buggy->GetBody() ? static_cast<USceneComponent*>(Buggy->GetBody()) : Buggy->GetRootComponent();
	// Un poco de dispersión para que las bocanadas no salgan en fila.
	const FVector Jitter(FMath::FRandRange(-15.f, 15.f), FMath::FRandRange(-15.f, 15.f), 0.f);
	const FVector Where = From->GetComponentTransform().TransformPosition(TNBuggyHealthDetail::SmokeOffset + Jitter);
	ATN_RallyBurstFX::Spawn(World, ETNRallyBurstKind::Smoke, Where, SmokePuffRadiusCm);
	World->GetTimerManager().SetTimer(SmokePuffTimer, this, &UTN_BuggyHealthComponent::SpawnSmokePuff, 1.f / PuffsPerSecond, false);
}

void UTN_BuggyHealthComponent::MulticastExplode_Implementation(FVector_NetQuantize Where)
{
	UWorld* World = GetWorld();
	if (!World || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	ATN_RallyBurstFX::Spawn(World, ETNRallyBurstKind::Sand, Where, TNBuggyHealthDetail::SandCloudRadiusCm);
	if (ExplosionFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, ExplosionFX, Where);
	}
	if (ExplosionSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(World, ExplosionSound, Where);
	}
	TNBuggyHealthDetail::KickLocalGunner(GetBuggy(), TNBuggyHealthDetail::ExplodeKickPitchDeg,
		TNBuggyHealthDetail::ExplodeKickRollDeg, TNBuggyHealthDetail::ExplodeKickBackCm);
	if (ATN_Buggy* Buggy = GetBuggy())
	{
		Buggy->AddCameraTrauma(TNBuggyHealthDetail::ExplodeTrauma);
	}
}

// ── Choques y atropellos ──────────────────────────────────────────────────────

void UTN_BuggyHealthComponent::OnChassisHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	if (!OtherActor || OtherActor == GetOwner())
	{
		return;
	}
	if (ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(OtherActor))
	{
		TryRunOver(Turtle);
		return;
	}
	if (!IsCrashContact(GetOwner(), OtherActor))
	{
		return;
	}
	HandleCrash(OtherActor, NormalImpulse, Hit);
}

bool UTN_BuggyHealthComponent::IsCrashContact(const AActor* Owner, const AActor* Other)
{
	if (!Other || Other == Owner)
	{
		return false;
	}
	// Un coco que da en el chasis ya quita su daño en ReceiveAmmoHit; contarlo además como choque sumaba CrashMaxDamage
	// por su velocidad de cierre (#296).
	return !Other->IsA<ATN_RallyProjectile>() && !Other->FindComponentByClass<UProjectileMovementComponent>();
}

void UTN_BuggyHealthComponent::HandleCrash(AActor* OtherActor, const FVector& NormalImpulse, const FHitResult& Hit)
{
	ATN_Buggy* Buggy = GetBuggy();
	const double Now = GetWorld()->GetTimeSeconds();
	if (!Buggy || Now - LastCrashTime < TNRallyCombat::CrashCooldownSeconds)
	{
		return;
	}
	const float Mass = FMath::Max(Buggy->GetMesh()->GetMass(), 1.f);
	const FVector RelativeVelocity = Buggy->GetVelocity() - OtherActor->GetVelocity();
	const float Closing = static_cast<float>(FMath::Abs(FVector::DotProduct(RelativeVelocity, Hit.ImpactNormal)));
	// El impulso normal de Chaos da el golpe real; la velocidad de cierre cubre los contactos sin impulso informado.
	const float DeltaV = FMath::Max(static_cast<float>(NormalImpulse.Size()) / Mass, Closing);
	const float Damage = TNRallyCombat::CrashDamage(DeltaV, static_cast<float>(Hit.ImpactNormal.Z));

	float Bump = 0.f;
	const FVector Forward = FVector(Buggy->GetActorForwardVector().X, Buggy->GetActorForwardVector().Y, 0.f).GetSafeNormal();
	if (Cast<ATN_Buggy>(OtherActor))
	{
		// Golpe por detrás: el de detrás viene más rápido en el sentido de la marcha del de delante.
		const float LocalX = static_cast<float>(Buggy->GetActorTransform().InverseTransformPosition(Hit.ImpactPoint).X);
		const float RearClosing = static_cast<float>(FVector::DotProduct(OtherActor->GetVelocity() - Buggy->GetVelocity(), Forward));
		Bump = TNRallyCombat::RearBumpCmsFor(LocalX, static_cast<float>(GetHalfExtents().X), RearClosing);
	}
	if (Damage <= 0.f && Bump <= 0.f)
	{
		return;
	}
	LastCrashTime = Now;
	if (Bump > 0.f)
	{
		Buggy->ApplyVelocityImpulse(Forward * Bump);
	}
	MulticastCrash(Hit.ImpactPoint);
	ApplyDamage(Damage);
}

void UTN_BuggyHealthComponent::MulticastCrash_Implementation(FVector_NetQuantize Where)
{
	UWorld* World = GetWorld();
	if (!World || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	ATN_RallyBurstFX::Spawn(World, ETNRallyBurstKind::Sparks, Where, TNBuggyHealthDetail::SparksRadiusCm);
	if (CrashFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, CrashFX, Where);
	}
	if (CrashSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(World, CrashSound, Where);
	}
	TNBuggyHealthDetail::KickLocalGunner(GetBuggy(), TNBuggyHealthDetail::CrashKickPitchDeg,
		TNBuggyHealthDetail::CrashKickRollDeg, TNBuggyHealthDetail::CrashKickBackCm);
	if (ATN_Buggy* Buggy = GetBuggy())
	{
		Buggy->AddCameraTrauma(TNBuggyHealthDetail::CrashTrauma);
	}
}

void UTN_BuggyHealthComponent::TryRunOver(ATortugaCharacter* Turtle)
{
	ATN_Buggy* Buggy = GetBuggy();
	UTN_ShellComponent* Shell = Turtle->GetShellComponent();
	UTN_CarryComponent* Carry = Turtle->GetCarryComponent();
	if (!Buggy || !Shell || Turtle->IsDead() || (Carry && Carry->IsBeingCarried()))
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (const double* Last = LastRunOver.Find(Turtle); Last && Now - *Last < TNRallyCombat::RunOverCooldownSeconds)
	{
		return;
	}
	const FVector Launch = TNRallyCombat::TurtleLaunchVelocity(Buggy->GetVelocity(), Turtle->GetActorLocation() - Buggy->GetActorLocation());
	if (Launch.IsNearlyZero())
	{
		return;
	}
	LastRunOver.Add(Turtle, Now);
	// La física de lanzamiento del juego (como al lanzar a una tortuga cogida): sale hecha bola, rueda y, al pararse,
	// sale sola del caparazón. No hace daño.
	if (Carry && Carry->IsCarrying())
	{
		Carry->ForceRelease(false);
	}
	if (Turtle->IsKnockedDown())
	{
		Turtle->RecoverFromKnockdown();
	}
	if (!Shell->IsInShell())
	{
		Shell->ForceEnterShell(false, false);
	}
	Shell->StartBody(Launch, true, true);
	UE_LOG(LogTNBuggy, Verbose, TEXT("%s atropella a %s: sale a (%.0f, %.0f, %.0f) cm/s"), *Buggy->GetName(), *Turtle->GetName(),
		Launch.X, Launch.Y, Launch.Z);
}
