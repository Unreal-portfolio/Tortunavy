#include "World/TN_ThrowableItemActor.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TN_Log.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Player/TortugaCharacter.h"
#include "Player/TN_HitFeedback.h"
#include "Core/ITN_EnemyTargetInterface.h"
#include "World/TN_PickupInteractableBase.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "Engine/World.h"
#include "TimerManager.h"

ATN_ThrowableItemActor::ATN_ThrowableItemActor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	// bAlwaysRelevant: sin esto, si el lanzador está lejos del cliente, la bola
	// no sería network-relevant → el cliente nunca vería el proyectil en vuelo.
	bAlwaysRelevant = true;

	// bReplicateMovement=false: el PM corre en TODAS las máquinas desde las mismas
	// condiciones iniciales → trayectoria 100% fluida en cada cliente sin ancho de banda.
	// Los parámetros se distribuyen via Multicast_InitializeThrow (Reliable), que
	// garantiza llegada DESPUÉS del spawn → sin race condition.
	SetReplicateMovement(false);

	// NetUpdateFrequency mínima: el actor existe en red pero no envía posición.
	// Solo necesita existir para recibir el Multicast y el Destroy final.
	SetNetUpdateFrequency(2.f);
	SetMinNetUpdateFrequency(1.f);

	// Mesh ES el root: UStaticMeshComponent es UPrimitiveComponent.
	// ProjectileMovement actualizará directamente la posición del actor.
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetIsReplicated(false);
	Mesh->SetSimulatePhysics(false);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetNotifyRigidBodyCollision(true);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->SetUpdatedComponent(Mesh);
	ProjectileMovement->bAutoActivate                        = false;
	ProjectileMovement->InitialSpeed                         = 0.0f;
	ProjectileMovement->MaxSpeed                             = 6000.0f;
	ProjectileMovement->ProjectileGravityScale               = 1.0f;
	ProjectileMovement->bShouldBounce                        = true;
	ProjectileMovement->BounceAdditionalIterations           = 1;
	// Cuando la velocidad baja de este umbral (cm/s) tras un rebote,
	// ProjectileMovement llama StopSimulating → OnProjectileStop → spawn pickup.
	ProjectileMovement->BounceVelocityStopSimulatingThreshold = 50.f;
	// #708: el vuelo en pasos fijos de 1/60 s, no del fotograma de cada máquina: contra una rampa, un fotograma largo y uno corto
	// daban rebotes distintos y el cliente se separaba de la trayectoria del anfitrión.
	ProjectileMovement->bForceSubStepping = true;
	ProjectileMovement->MaxSimulationTimeStep = 1.f / 60.f;
	ProjectileMovement->MaxSimulationIterations = 12;
}

void ATN_ThrowableItemActor::BeginPlay()
{
	Super::BeginPlay();
	SetLifeSpan(MaxLifeSeconds);

	UE_LOG(LogTortunabo, Log, TEXT("[TN_Throwable] BeginPlay auth=%d bReady=%d bLaunchApplied=%d inst=%s"),
		HasAuthority() ? 1 : 0, ThrowData.bReady ? 1 : 0, bLaunchApplied ? 1 : 0,
		*GetNameSafe(GetInstigator()));

	// Aplicar propiedades de física desde las Class Defaults del BP.
	ProjectileMovement->Bounciness = Bounciness;
	ProjectileMovement->Friction   = RollingFriction;

	// Visibilidad incondicional: el mesh es visible desde el primer frame en TODAS
	// las máquinas. El mesh correcto llega vía ThrowData (OnRep), pero no podemos
	// esperar a él para mostrar la bola — el BP puede tener el placeholder oculto.
	Mesh->SetHiddenInGame(false);
	Mesh->SetVisibility(true, true);

	// Ignorar colisión del lanzador en TODAS las máquinas. En cliente, el PM corre
	// localmente desde SpawnLocation (120 cm delante del char) — sin esto la bola
	// colisiona con el char lanzador, cae por debajo del BounceVelocityStopThreshold
	// (50 cm/s) y StopSimulating() detiene el PM permanentemente en el primer frame.
	IgnoreInstigatorCollision();

	// Solo el servidor valida impactos, detenciones y activa el PM.
	if (HasAuthority())
	{
		Mesh->OnComponentHit.AddDynamic(this, &ATN_ThrowableItemActor::OnMeshHit);
		ProjectileMovement->OnProjectileStop.AddDynamic(this, &ATN_ThrowableItemActor::OnProjectileStopped);
		ProjectileMovement->OnProjectileBounce.AddDynamic(this, &ATN_ThrowableItemActor::OnProjectileBounced);
	}

	// ApplyLaunchDataIfReady en BeginPlay: solo aplica mesh/escala en clientes.
	// El PM lo activa únicamente en servidor (ver ApplyLaunchDataIfReady).
	ApplyLaunchDataIfReady();

	if (!HasAuthority() && !bLaunchApplied)
	{
		FTimerDelegate Del;
		Del.BindUObject(this, &ATN_ThrowableItemActor::RetryApplyLaunch);
		GetWorldTimerManager().SetTimer(LaunchRetryTimerHandle, Del, 0.5f, false);
	}
}

void ATN_ThrowableItemActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// Enemigos de la playa sin colisión que pare la bola: el tramo de este fotograma contra su cuerpo.
	if (ProjectileMovement && ProjectileMovement->IsActive() && bLaunchApplied)
	{
		const FVector Here = GetActorLocation();
		if (bHasPrevFlightLocation)
		{
			CheckBeachEnemyHit(PrevFlightLocation, Here);
		}
		PrevFlightLocation = Here;
		bHasPrevFlightLocation = true;
	}
	else
	{
		bHasPrevFlightLocation = false;
	}
	if (!ProjectileMovement || !ProjectileMovement->IsActive() || ThrowAngularVelocityDegSec.IsNearlyZero())
	{
		return;
	}
	// Rotación visual de la bola mientras vuela — puramente cosmético, cada
	// máquina la calcula localmente. Mapeo: X→Roll, Y→Pitch, Z→Yaw (convención UE).
	Mesh->AddLocalRotation(FRotator(
		ThrowAngularVelocityDegSec.Y * DeltaTime,
		ThrowAngularVelocityDegSec.Z * DeltaTime,
		ThrowAngularVelocityDegSec.X * DeltaTime
	));
}

void ATN_ThrowableItemActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(LaunchRetryTimerHandle);
	Super::EndPlay(EndPlayReason);
}

void ATN_ThrowableItemActor::RetryApplyLaunch()
{
	if (!bLaunchApplied && ThrowData.bReady)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[TN_Throwable] RetryApplyLaunch: forcing launch apply after timeout"));
		ApplyLaunchDataIfReady();
	}
}

void ATN_ThrowableItemActor::InitializeThrow(const FVector& SpawnLocation, const FVector& InitialVelocity)
{
	if (!HasAuthority())
	{
		return;
	}

	UE_LOG(LogTortunabo, Log, TEXT("[TN_Throwable] SERVER InitializeThrow origin=(%.0f,%.0f,%.0f) v=(%.0f,%.0f,%.0f) mesh=%s"),
		SpawnLocation.X, SpawnLocation.Y, SpawnLocation.Z,
		InitialVelocity.X, InitialVelocity.Y, InitialVelocity.Z,
		*GetNameSafe(SourceItem.EquippedMesh));

	const FVector SafeScale = SourceItem.EquippedMeshScale.IsNearlyZero()
		? FVector::OneVector
		: SourceItem.EquippedMeshScale;

	// Distribuye a TODAS las máquinas: servidor + todos los clientes.
	// Reliable → llega garantizado DESPUÉS de que el spawn del actor ya existe en cliente.
	// Cada máquina activa su propio PM desde las mismas condiciones → fluido sin bandwidth.
	Multicast_InitializeThrow(SpawnLocation, InitialVelocity, SourceItem.EquippedMesh, SafeScale);
}

void ATN_ThrowableItemActor::SetSourceItem(const FTN_InventoryItem& Item)
{
	SourceItem = Item;
}

void ATN_ThrowableItemActor::Multicast_InitializeThrow_Implementation(
	FVector Origin, FVector Velocity, UStaticMesh* MeshAsset, FVector Scale)
{
	// Aplicar en TODAS las máquinas — mismo código, mismo resultado.
	ThrowData.SpawnLocation  = Origin;
	ThrowData.LaunchVelocity = Velocity;
	ThrowData.MeshScale      = Scale;
	ThrowData.EquippedMesh   = MeshAsset;
	ThrowData.bReady         = true;

	UE_LOG(LogTortunabo, Log, TEXT("[TN_Throwable] Multicast_InitializeThrow %s origin=(%.0f,%.0f,%.0f) v=(%.0f,%.0f,%.0f)"),
		HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT"),
		Origin.X, Origin.Y, Origin.Z, Velocity.X, Velocity.Y, Velocity.Z);

	ApplyLaunchDataIfReady();
}

void ATN_ThrowableItemActor::Multicast_BallStopped_Implementation(FVector FinalLocation)
{
	// SERVIDOR: el PM ya se detuvo solo — fue OnProjectileStop quien disparó esto.
	// Si volvemos a llamar StopSimulating desde dentro del delegate OnProjectileStop,
	// genera recursión infinita (Broadcast → OnProjectileStopped → Multicast → Broadcast…)
	// y crashea el host. El servidor no necesita hacer nada aquí.
	if (HasAuthority())
	{
		return;
	}

	// CLIENTES: detener la simulación local y sincronizar al punto final del servidor.
	// Usamos SetActive(false) en lugar de StopSimulating para NO volver a disparar
	// el delegate OnProjectileStop (aunque en cliente no está ligado, es más seguro).
	if (ProjectileMovement)
	{
		ProjectileMovement->SetActive(false);
		ProjectileMovement->Velocity = FVector::ZeroVector;
	}
	SetActorLocation(FinalLocation);
}

void ATN_ThrowableItemActor::OnProjectileBounced(const FHitResult& ImpactResult, const FVector& ImpactVelocity)
{
	// El servidor manda su estado tras el rebote (con la velocidad ya rebotada), como mucho cada 0,1 s: una rampa da rebotes
	// seguidos y bastan los primeros para que los clientes sigan el mismo camino.
	constexpr double MinSyncSeconds = 0.1;
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (!HasAuthority() || !ProjectileMovement || bPickupSpawned || (LastBounceSyncTime >= 0.0 && Now - LastBounceSyncTime < MinSyncSeconds))
	{
		return;
	}
	LastBounceSyncTime = Now;
	Multicast_BounceSync(GetActorLocation(), ProjectileMovement->Velocity);
}

void ATN_ThrowableItemActor::Multicast_BounceSync_Implementation(FVector_NetQuantize Location, FVector_NetQuantize Velocity)
{
	if (HasAuthority() || !ProjectileMovement || !bLaunchApplied || !ProjectileMovement->IsActive())
	{
		return;
	}
	SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	ProjectileMovement->Velocity = Velocity;
}

void ATN_ThrowableItemActor::IgnoreInstigatorCollision()
{
	if (!Mesh) { return; }
	if (APawn* ThrowInstigator = GetInstigator())
	{
		Mesh->IgnoreActorWhenMoving(ThrowInstigator, true);
	}
}


void ATN_ThrowableItemActor::ApplyLaunchDataIfReady()
{
	if (!ThrowData.bReady || bLaunchApplied)
	{
		return;
	}

	// Mesh y visibilidad: todos (servidor + clientes + JIP late-joiners).
	if (ThrowData.EquippedMesh)
	{
		Mesh->SetStaticMesh(ThrowData.EquippedMesh);
	}
	if (!ThrowData.MeshScale.IsNearlyZero())
	{
		Mesh->SetRelativeScale3D(ThrowData.MeshScale);
	}
	Mesh->SetHiddenInGame(false);
	Mesh->SetVisibility(true, true);

	bLaunchApplied = true;
	IgnoreInstigatorCollision();

	// TODAS las máquinas: mismas condiciones iniciales → misma trayectoria determinista.
	// Sin bReplicateMovement → cero actualizaciones de posición por red.
	SetActorLocation(ThrowData.SpawnLocation);
	if (ProjectileMovement)
	{
		ProjectileMovement->Velocity = ThrowData.LaunchVelocity;
		ProjectileMovement->Activate(true);
	}

	if (!ThrowAngularVelocityDegSec.IsNearlyZero())
	{
		Mesh->SetPhysicsAngularVelocityInDegrees(ThrowAngularVelocityDegSec);
	}

	UE_LOG(LogTortunabo, Log, TEXT("[TN_Throwable] ApplyLaunchDataIfReady %s origin=(%.0f,%.0f,%.0f) v=(%.0f,%.0f,%.0f) pmActive=%d"),
		HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT"),
		ThrowData.SpawnLocation.X, ThrowData.SpawnLocation.Y, ThrowData.SpawnLocation.Z,
		ThrowData.LaunchVelocity.X, ThrowData.LaunchVelocity.Y, ThrowData.LaunchVelocity.Z,
		ProjectileMovement && ProjectileMovement->IsActive() ? 1 : 0);
}

// ── Colisión y ciclo de vida ──────────────────────────────────────────────────

void ATN_ThrowableItemActor::OnMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
                                        UPrimitiveComponent* OtherComp, FVector NormalImpulse,
                                        const FHitResult& Hit)
{
	if (!HasAuthority() || bPickupSpawned)
	{
		return;
	}

	// Enemigo de la playa con cuerpo sólido (cangrejo gigante) → mareo con pajaritos, como los demás de la playa.
	if (ATN_BeachEnemy* BeachEnemy = Cast<ATN_BeachEnemy>(OtherActor))
	{
		if (!AlreadyHitEnemies.Contains(BeachEnemy))
		{
			const float CurrentSpeed = ProjectileMovement ? ProjectileMovement->Velocity.Size() : 0.f;
			if (CurrentSpeed >= MinKnockdownSpeed)
			{
				AlreadyHitEnemies.Add(BeachEnemy);
				BeachEnemy->ApplyHitStun(TNBeachHitStun::ThrownSeconds, this);
			}
		}
		return;
	}

	// Enemigo → stun si la bola va rápida. Una sola vez por enemy (track via AlreadyHitActors).
	if (ITN_EnemyTargetInterface* Enemy = Cast<ITN_EnemyTargetInterface>(OtherActor))
	{
		if (!AlreadyHitEnemies.Contains(OtherActor))
		{
			const float CurrentSpeed = ProjectileMovement ? ProjectileMovement->Velocity.Size() : 0.f;
			if (CurrentSpeed >= MinKnockdownSpeed)
			{
				AlreadyHitEnemies.Add(OtherActor);
				Enemy->ApplyStun(KnockbackDuration);
				UE_LOG(LogTortunabo, Log, TEXT("[ThrowableItem] Hit enemy %s at %.0f cm/s → STUN %.2fs"),
					*GetNameSafe(OtherActor), CurrentSpeed, KnockbackDuration);
			}
		}
		return;
	}

	ATortugaCharacter* HitPlayer = Cast<ATortugaCharacter>(OtherActor);

	// Superficies → el ProjectileMovement gestiona el rebote; nada que hacer aquí.
	if (!HitPlayer)
	{
		return;
	}

	// Auto-impacto → ignorar (IgnoreActorWhenMoving ya debería prevenirlo).
	if (GetInstigator() == HitPlayer)
	{
		return;
	}

	// Golpe a otro jugador → knockdown solo si la bola va lo bastante rápido.
	// La bola rebota naturalmente via ProjectileMovement (bShouldBounce=true).
	// Pickup se genera cuando la bola se detiene completamente (OnProjectileStopped).
	if (!AlreadyHitPlayers.Contains(HitPlayer))
	{
		const float CurrentSpeed = ProjectileMovement ? ProjectileMovement->Velocity.Size() : 0.f;

		if (CurrentSpeed >= MinKnockdownSpeed)
		{
			AlreadyHitPlayers.Add(HitPlayer);
			// Antes que el derribo: la sacudida y la vibración van con la velocidad del impacto, no con el empujón (#350).
			HitPlayer->NotifyHitFeedback(TNHitFeedback::StrengthFromImpulse(CurrentSpeed));
			HitPlayer->ApplyKnockdown(KnockbackDuration);
			UE_LOG(LogTortunabo, Log, TEXT("[ThrowableItem] Hit %s at %.0f cm/s → KNOCKDOWN (threshold=%.0f)"),
				*GetNameSafe(HitPlayer), CurrentSpeed, MinKnockdownSpeed);
		}
		else
		{
			UE_LOG(LogTortunabo, Log, TEXT("[ThrowableItem] Hit %s at %.0f cm/s → too slow, bounce only (threshold=%.0f)"),
				*GetNameSafe(HitPlayer), CurrentSpeed, MinKnockdownSpeed);
		}
	}
}

void ATN_ThrowableItemActor::CheckBeachEnemyHit(const FVector& From, const FVector& To)
{
	if (!ProjectileMovement || FVector::DistSquared(From, To) < 1.0)
	{
		return;
	}
	const float Radius = FMath::Clamp(Mesh ? static_cast<float>(Mesh->Bounds.SphereRadius) : 20.f, 10.f, 80.f);
	FVector AxisPoint = To;
	ATN_BeachEnemy* BeachEnemy = ATN_BeachEnemy::FindProjectileHit(this, From, To, Radius, &AxisPoint);
	if (!BeachEnemy || AlreadyHitEnemies.Contains(BeachEnemy))
	{
		return;
	}
	AlreadyHitEnemies.Add(BeachEnemy);
	const FVector Velocity = ProjectileMovement->Velocity;
	if (HasAuthority() && !bPickupSpawned && Velocity.Size() >= MinKnockdownSpeed)
	{
		BeachEnemy->ApplyHitStun(TNBeachHitStun::ThrownSeconds, this);
		UE_LOG(LogTortunabo, Log, TEXT("[ThrowableItem] Hit beach enemy %s at %.0f cm/s → mareo %.1fs"),
			*GetNameSafe(BeachEnemy), Velocity.Size(), TNBeachHitStun::ThrownSeconds);
	}
	// Rebota en su cuerpo (no hay colisión que la pare): hacia fuera del eje del cuerpo, perdiendo fuerza.
	FVector Normal = To - AxisPoint;
	if (!Normal.Normalize())
	{
		Normal = -Velocity.GetSafeNormal();
	}
	const double Into = FVector::DotProduct(Velocity, Normal);
	const FVector Bounced = Into < 0.0 ? Velocity - Normal * (2.0 * Into) : Velocity;
	ProjectileMovement->Velocity = Bounced * 0.45 + FVector(0.0, 0.0, 150.0);
}

void ATN_ThrowableItemActor::OnProjectileStopped(const FHitResult& ImpactResult)
{
	if (!HasAuthority() || bPickupSpawned)
	{
		return;
	}

	// ImpactResult.ImpactPoint (FVector_NetQuantize) no es asignable a FVector
	// en un ternario (C2446/C2737). Usamos if/else con XYZ explícitos.
	// ImpactPoint es el contacto exacto con la superficie → fuente primaria.
	// GetActorLocation() como fallback si el hit no es válido.
	FVector StopLocation = GetActorLocation();
	if (ImpactResult.IsValidBlockingHit())
	{
		StopLocation.X = ImpactResult.ImpactPoint.X;
		StopLocation.Y = ImpactResult.ImpactPoint.Y;
		StopLocation.Z = ImpactResult.ImpactPoint.Z;
	}

	UE_LOG(LogTortunabo, Log, TEXT("[ThrowableItem] OnProjectileStopped — loc=(%.0f,%.0f,%.0f) validHit=%d"),
		StopLocation.X, StopLocation.Y, StopLocation.Z, ImpactResult.IsValidBlockingHit() ? 1 : 0);

	// Sincronizar todas las simulaciones locales a la posición final del servidor.
	// Garantiza que la bola visualmente snap-ea al mismo punto antes de desaparecer.
	Multicast_BallStopped(StopLocation);

	SpawnPickupAtLocation(StopLocation);
	// SetLifeSpan en vez de Destroy inmediato: da tiempo a que el snap de
	// Multicast_BallStopped se vea en cliente antes de que el actor desaparezca.
	// Sin dormancia: con la destrucción diferida, el canal podría cerrarse por
	// dormancia antes del Destroy y el cliente se quedaría con una bola fantasma
	// (un actor dinámico dormido no genera destruction info al destruirse).
	SetLifeSpan(0.2f);
}

void ATN_ThrowableItemActor::LifeSpanExpired()
{
	if (HasAuthority() && !bPickupSpawned)
	{
		SpawnPickupAtLocation(GetActorLocation());
	}

	Super::LifeSpanExpired();
}

void ATN_ThrowableItemActor::SpawnPickupAtLocation(const FVector& Location)
{
	if (!HasAuthority() || !GetWorld() || bPickupSpawned)
	{
		return;
	}

	if (!SourceItem.IsValid() || !SourceItem.PickupActorClass)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[ThrowableItemActor] Sin PickupActorClass en SourceItem — el ítem se pierde."));
		return;
	}

	// Floor trace: asegurar que el pickup quede apoyado en el suelo
	FVector SpawnLocation = Location;
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TN_PickupFloorTrace), false, this);
		const FVector TraceStart = Location + FVector(0.f, 0.f, 80.f);
		const FVector TraceEnd   = Location - FVector(0.f, 0.f, 600.f);

		// Primero intentar WorldStatic (suelos estáticos)
		bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, Params);

		// Si no impacta, intentar Visibility (captura más tipos de geometría)
		if (!bHit)
		{
			bHit = GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params);
		}

		if (bHit)
		{
			SpawnLocation = Hit.ImpactPoint + FVector(0.f, 0.f, 5.f);
		}
		else
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[ThrowableItem] SpawnPickup: no floor found below %.1f,%.1f,%.1f — spawning at ball position."),
				Location.X, Location.Y, Location.Z);
		}
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (ATN_PickupInteractableBase* Pickup = GetWorld()->SpawnActor<ATN_PickupInteractableBase>(
	        SourceItem.PickupActorClass, SpawnLocation, FRotator::ZeroRotator, Params))
	{
		Pickup->InitializeFromInventoryItem(SourceItem);
		// Marcar SOLO tras spawn exitoso: si SpawnActor devuelve null y el flag ya
		// estuviera puesto, LifeSpanExpired no reintentaría y el ítem se perdería
		// en silencio.
		bPickupSpawned = true;
	}
	else
	{
		UE_LOG(LogTortunabo, Error, TEXT("[ThrowableItem] SpawnActor de '%s' devolvió null en (%.0f,%.0f,%.0f) — el ítem se pierde."),
			*GetNameSafe(SourceItem.PickupActorClass), SpawnLocation.X, SpawnLocation.Y, SpawnLocation.Z);
	}
}
