// ─────────────────────────────────────────────────────────────────────────────
// TortugaCharacter — Knockdown, muerte y ragdoll.
//
// Definiciones extraídas de TortugaCharacter.cpp para mejorar la legibilidad.
// Misma clase ATortugaCharacter en otra unidad de traducción: sin cambios de
// lógica ni de replicación, solo organización.
// ─────────────────────────────────────────────────────────────────────────────

#include "Player/TN_ShellComponent.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_CarryRules.h"
#include "Player/TortugaCharacter.h"
#include "Player/TN_DizzyBirdsComponent.h"
#include "Player/TN_TurtleAnimInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Core/TN_Log.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Animation/AnimInstance.h"
#include "Net/UnrealNetwork.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Core/TN_CoopPlayerState.h"
#include "Game/TN_RunGameMode.h"
#include "World/Beach/TN_RaceItems.h"

// ── Knockdown ─────────────────────────────────────────────────────────────────

void ATortugaCharacter::ApplyKnockdown(float Duration, FVector ImpulseOverride)
{
	if (!HasAuthority())
	{
		return;
	}
	// Con el protector solar puesto o volando en el pelícano taxi (objetos de la carrera) nada la derriba.
	if (TNRaceItems::IsInvulnerable(this))
	{
		return;
	}
	// Noqueada de verdad: un momento quieta en el suelo con los pajaritos, aunque el golpe pida menos.
	Duration = FMath::Max(Duration, MinKnockdownSeconds);

	// Un derribo saca del caparazon: si no, el speed cap seguiria puesto al
	// recuperarse y el personaje se quedaria inmovil.
	if (ShellComponent)
	{
		ShellComponent->ForceExitShell();
	}

	// En brazos de otra (#68): quien la lleva la suelta antes del derribo, ya fuera del caparazón (sin una bola que nazca y
	// se quite en el acto). Si no, el derribo empezaba con el movimiento apagado y enganchada encima del portador, y al
	// levantarse andaba (MOVE_Walking) pegada a él con CarriedBy puesto.
	if (CarryComponent && TNCarryRules::KnockdownDropsFromCarrier(CarryComponent->IsBeingCarried()))
	{
		ATortugaCharacter* Carrier = CarryComponent->GetCarrier();
		if (UTN_CarryComponent* CarrierCarry = Carrier ? Carrier->GetCarryComponent() : nullptr)
		{
			CarrierCarry->ForceRelease(false);
		}
	}

	// Evitar solapar knockdowns
	if (bIsKnockedDown)
	{
		// Si ya está en knockdown, reiniciar el timer con la nueva duración
		GetWorldTimerManager().SetTimer(KnockdownTimerHandle, this,
		                                &ATortugaCharacter::RecoverFromKnockdown, Duration, false);
		return;
	}

	// Cancelar dive si estaba activo — knockdown tiene prioridad
	if (bIsDiving)
	{
		EndDive();
	}

	bIsKnockedDown = true;

	// Impulso: usar override si se proporcionó, si no calcular del momentum actual
	if (UCharacterMovementComponent* MC = GetCharacterMovement())
	{
		FVector KnockImpulse;
		if (!ImpulseOverride.IsZero())
		{
			KnockImpulse = ImpulseOverride;
		}
		else
		{
			const FVector CurrentVel = MC->Velocity;
			KnockImpulse = FVector(
				CurrentVel.X * KnockdownHorizontalMultiplier,
				CurrentVel.Y * KnockdownHorizontalMultiplier,
				FMath::Min(CurrentVel.Z, 0.f) - KnockdownDownwardForce
			);
		}
		LaunchCharacter(KnockImpulse, /*bXYOverride=*/true, /*bZOverride=*/true);
	}

	// ── Knockdown visual via sistema de emotes ───────────────────────────────
	// ReplicatedEmoteIndex = KNOCKDOWN_EMOTE_ID replica la animación a todos los clientes
	// usando el mismo canal de replicación que los emotes normales (funciona perfectamente).
	ReplicatedEmoteIndex = KNOCKDOWN_EMOTE_ID;
	// Servidor: aplicar localmente (OnRep no dispara en quien posee la variable)
	StartEmoteLocally(KNOCKDOWN_EMOTE_ID);

	// Tilt del cuerpo (pitch -180°) — se ejecuta en servidor + todos los clientes.
	// Sin esto el emote solo agita brazos y el jugador se ve flotando, no tumbado.
	MulticastApplyKnockdownVisual(true);

	// ── DBNO heartbeat: solo el jugador local incapacitado oye el latido ──
	if (IsLocallyControlled())
	{
		PlayDBNOHeartbeatSound();
	}

	GetWorldTimerManager().SetTimer(KnockdownTimerHandle, this,
	                                &ATortugaCharacter::RecoverFromKnockdown, Duration, false);

	UE_LOG(LogTortunabo, Log, TEXT("[Knockdown] %s knocked down for %.1fs (momentum activo)"), *GetNameSafe(this), Duration);
}

void ATortugaCharacter::RecoverFromKnockdown()
{
	if (!HasAuthority())
	{
		return;
	}

	// Levantada antes de tiempo (la bola de un aturdimiento, un teletransporte, un rescate...): el temporizador del derribo ya
	// no vale. Si no, saltaba más tarde y la ponía a andar en plena bola (y volvía a sonar el «¡arriba!»).
	GetWorldTimerManager().ClearTimer(KnockdownTimerHandle);

	bIsKnockedDown = false;

	// Restaurar movimiento
	if (UCharacterMovementComponent* MC = GetCharacterMovement())
	{
		MC->SetMovementMode(MOVE_Walking);
	}

	// ── Cancelar el knockdown emote ───────────────────────────────────────────
	// Establecer -1 hace que OnRep_ReplicatedEmoteIndex cancele el emote en todos los clientes
	if (ReplicatedEmoteIndex == KNOCKDOWN_EMOTE_ID)
	{
		ReplicatedEmoteIndex = -1;
		// Servidor: cancelar emote localmente (OnRep no dispara en quien posee la variable)
		if (ActiveEmoteIndex >= 0 || bEmoteBlendingOut)
		{
			CancelEmoteLocalOnly();
		}
	}

	// Restaurar rotación del cuerpo en todas las máquinas
	MulticastApplyKnockdownVisual(false);

	// ── Audio feedback de revive ─────────────────────────────────────────
	StopDBNOHeartbeatSound();
	MulticastPlayReviveSuccessSound();

	UE_LOG(LogTortunabo, Log, TEXT("[Knockdown] %s recovered"), *GetNameSafe(this));
}

void ATortugaCharacter::OnRep_IsKnockedDown()
{
	// Pajaritos del mareo en todas las máquinas (también para quien entra con el derribo ya empezado).
	if (DizzyBirds)
	{
		DizzyBirds->SetDizzy(bIsKnockedDown);
	}

	// ReplicatedEmoteIndex usa COND_SkipOwner: el DUEÑO del pawn nunca recibe
	// OnRep_ReplicatedEmoteIndex cuando el servidor pone KNOCKDOWN_EMOTE_ID.
	// Por eso manejamos aquí TANTO el input COMO el visual del knockdown para
	// el cliente que es el dueño (IsLocallyControlled). Los otros clientes
	// (no dueños) reciben el emote via OnRep_ReplicatedEmoteIndex normalmente.
	if (IsLocallyControlled())
	{
		// El momentum viene del servidor vía replicación de movimiento.
		// Move() bloquea el input mientras bIsKnockedDown = true.
		// Al recuperar, restaurar Walking por si el timer llegó antes de aterrizar.
		if (!bIsKnockedDown)
		{
			if (UCharacterMovementComponent* MC = GetCharacterMovement())
			{
				MC->SetMovementMode(MOVE_Walking);
			}
		}

		// ── Visual knockdown para el dueño (ruta alternativa a OnRep_ReplicatedEmoteIndex) ─
		if (bIsKnockedDown)
		{
			// Arrancar el emote de knockdown localmente — igual que hace el servidor
			// en ApplyKnockdown() para el listen-server player.
			StartEmoteLocally(KNOCKDOWN_EMOTE_ID);
			PlayDBNOHeartbeatSound();
		}
		else
		{
			// Cancelar el emote de knockdown al recuperarse
			if (ActiveEmoteIndex == KNOCKDOWN_EMOTE_ID || bEmoteBlendingOut)
			{
				CancelEmoteLocalOnly();
			}
			StopDBNOHeartbeatSound();
			// PlayReviveSuccessSound ya no se llama aquí: MulticastPlayReviveSuccessSound
			// (RecoverFromKnockdown) lo reproduce en todas las máquinas, incluida esta.
			bCanAirDash = true;  // Restore air dash after knockdown recovery
		}
	}
}

void ATortugaCharacter::MulticastApplyKnockdownVisual_Implementation(bool bKnocked)
{
	// La animación de agitar brazos la emite el sistema de emotes via
	// ReplicatedEmoteIndex = KNOCKDOWN_EMOTE_ID. Aquí aplicamos la rotación
	// simulada del cuerpo (pitch -180°) encima del emote — sin tilt el
	// jugador se ve "flotando" en vez de tumbado. Se ejecuta en TODAS las
	// máquinas para cubrir listen-server + todos los clientes (incluido el
	// dueño, que no recibe OnRep_ReplicatedEmoteIndex por COND_SkipOwner).
	ApplyKnockdownVisual(bKnocked);

	if (bKnocked && KnockdownSound)
	{
		PlaySfxAtSelf(KnockdownSound);
	}
}

void ATortugaCharacter::ApplyKnockdownVisual(bool bKnocked)
{
	// Pajaritos y estrellitas dando vueltas sobre la cabeza, con su piar y sus cuerdas mareadas, mientras dura.
	if (DizzyBirds)
	{
		DizzyBirds->SetDizzy(bKnocked);
	}

	// Derribada desde la bola del caparazón (#251). El servidor sale de la bola antes del derribo (ApplyKnockdown), pero en
	// los clientes este aviso llega antes que la réplica del fin de la bola (el RPC va antes que las propiedades): si la caja
	// sigue enganchada aquí, se suelta ya, como en el servidor. Si no, el derribo empezaba con la malla colocada sobre la caja
	// (la foto de la malla se quedaba con su giro y, al levantarse, la tortuga andaba tumbada como en la bola) y la caja
	// seguía colocando la malla y quitándole la física al ragdoll hasta que llegaba la réplica.
	if (bKnocked && ShellComponent && ShellComponent->HasLocalBody())
	{
		ShellComponent->DropLocalBody();
	}

	// ── Ruta A: ragdoll físico (opción 2 del rediseño Q1-07) ───────────────────
	// Si el BP configuró bUsePhysicsRagdoll=true y el SkelMesh tiene PhysicsAsset,
	// activamos ragdoll completo. Sin PhysicsAsset no hay ragdoll posible → cae
	// silenciosamente al tilt manual (Ruta B más abajo) y no rompe nada.
	USkeletalMeshComponent* SkelMesh = GetMesh();
	const bool bCanRagdoll = bUsePhysicsRagdoll && SkelMesh && SkelMesh->GetPhysicsAsset() != nullptr;
	if (bCanRagdoll)
	{
		UCharacterMovementComponent* CMC_Ragdoll = GetCharacterMovement();

		if (bKnocked)
		{
			if (bKnockdownRagdollActive) { return; } // idempotente

			// Al revivir, la malla vuelve a su sitio sobre la cápsula: sin la subida ni el giro del panzazo, ni el giro de la
			// bola del caparazón (#251).
			SnapshotSkelMeshRelTransform = FTransform(DiveMeshDefaultRot, DiveMeshDefaultLoc, DiveMeshDefaultScale);
			SnapshotSkelMeshCollisionProfile = SkelMesh->GetCollisionProfileName();
			PreKnockdownStandLocation = GetActorLocation();

			// Fuera de la pose del caparazón antes de que el ragdoll pause las animaciones (#251): la escala de los huesos del
			// ragdoll sale de la animación y, con la pausa, la cabeza y las patas se quedaban metidas en la concha (a medias o
			// del todo) bajo la cara de mareo hasta levantarse. Se vuelve a evaluar la pose antes de que simule.
			if (UTN_TurtleAnimInstance* TurtleAnim = Cast<UTN_TurtleAnimInstance>(SkelMesh->GetAnimInstance()))
			{
				if (TurtleAnim->SnapOutOfShellPose())
				{
					SkelMesh->RefreshBoneTransforms();
				}
			}

			if (HasAuthority())
			{
				SetReplicateMovement(false);
			}

			// KNOCKDOWN: preservar el momentum del LaunchCharacter (plátano, golpes).
			// Capturamos la velocity del CMC ANTES de pararlo para transferirla a los
			// bodies del ragdoll después — si no, el ragdoll arranca inerte y se cae
			// donde estabas sin "resbalar" por el impulso del plátano.
			FVector KnockdownInitialVel = CMC_Ragdoll ? CMC_Ragdoll->Velocity : FVector::ZeroVector;
			// El LaunchCharacter de ApplyKnockdown aún no se ha aplicado (el CMC lo gasta en su siguiente tick, que se
			// apaga aquí mismo): se pasa al ragdoll y se descarta, para que no lance la cápsula al levantarse.
			if (CMC_Ragdoll && !CMC_Ragdoll->PendingLaunchVelocity.IsZero())
			{
				KnockdownInitialVel = CMC_Ragdoll->PendingLaunchVelocity;
				CMC_Ragdoll->PendingLaunchVelocity = FVector::ZeroVector;
			}

			if (CMC_Ragdoll)
			{
				CMC_Ragdoll->NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;
				CMC_Ragdoll->DisableMovement();
				CMC_Ragdoll->StopMovementImmediately();
				CMC_Ragdoll->SetComponentTickEnabled(false);
			}

			if (UCapsuleComponent* Cap = GetCapsuleComponent())
			{
				Cap->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}

			SkelMesh->SetCollisionProfileName(RagdollCollisionProfile);
			// Colisión continua en todos los huesos: los golpes lanzan el ragdoll a 10-25 m/s y, sin ella, un hueso
			// pequeño atraviesa en un paso de física la colisión fina del terreno (mallas procedurales) y la tortuga
			// acaba bajo el suelo. Además, un tope a la velocidad de entrada.
			SkelMesh->SetAllUseCCD(true);
			// Una vez por tortuga: avisa si algún cuerpo del ragdoll no choca con lo estático o lo dinámico del mundo
			// (terreno, rocas y decorado son WorldStatic; murallas y fortalezas de la playa, WorldDynamic). Con el perfil
			// Ragdoll todos bloquean; solo falla si el Physics Asset tiene cuerpos con la colisión desactivada.
			if (!bRagdollCollisionReported)
			{
				bRagdollCollisionReported = true;
				int32 NotBlocking = 0;
				for (const FBodyInstance* Body : SkelMesh->Bodies)
				{
					if (Body && (!CollisionEnabledHasPhysics(Body->GetCollisionEnabled())
						|| Body->GetResponseToChannel(ECC_WorldStatic) != ECR_Block || Body->GetResponseToChannel(ECC_WorldDynamic) != ECR_Block))
					{
						++NotBlocking;
					}
				}
				if (NotBlocking > 0)
				{
					UE_LOG(LogTortunabo, Warning, TEXT("[Ragdoll] %s: %d de %d cuerpos del ragdoll (perfil '%s') no chocan con el mundo: revisa su colisión en el Physics Asset."),
						*GetName(), NotBlocking, SkelMesh->Bodies.Num(), *RagdollCollisionProfile.ToString());
				}
			}
			KnockdownInitialVel = KnockdownInitialVel.GetClampedToMaxSize(KnockdownRagdollMaxEntrySpeed);
			// Orden Epic: SIMULAR primero, pausar anims DESPUÉS. Evita el frame
			// "semitieso" (bPauseAnims=true congela pose antes de que física arranque).
			SkelMesh->SetAllBodiesSimulatePhysics(true);
			SkelMesh->SetAllBodiesPhysicsBlendWeight(1.f);
			SkelMesh->SetEnableGravity(true);
			// Chaos: Body->SetEnableGravity(true) propaga internamente al solver
			// (FPhysicsInterface::SetGravityEnabled_AssumesLocked). Mismo patrón
			// que EnterRagdollState (death) → consistencia entre rutas.
			// (Triple round 2 sugirió bEnableGravity + UpdatePhysicsProperties,
			// pero esa función no existe en FBodyInstance; SetEnableGravity es
			// la API canónica.)
			for (FBodyInstance* Body : SkelMesh->Bodies)
			{
				if (Body) { Body->SetEnableGravity(true); }
			}
			SkelMesh->SetAllPhysicsLinearVelocity(KnockdownInitialVel);
			SkelMesh->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector);
			SkelMesh->WakeAllRigidBodies();
			SkelMesh->bPauseAnims = true;  // después de simulación activa
			UE_LOG(LogTortunabo, Warning, TEXT("[Diagnostic] Ragdoll KNOCKDOWN ON (%s) IsSim=%s InitVel=(%.0f,%.0f,%.0f)"),
				*GetName(), SkelMesh->IsSimulatingPhysics()?TEXT("Y"):TEXT("N"),
				KnockdownInitialVel.X, KnockdownInitialVel.Y, KnockdownInitialVel.Z);

			bKnockdownRagdollActive = true;
			bRagdollProbeValid = false;
		}
		else
		{
			if (!bKnockdownRagdollActive) { return; }

			// Pose en la que quedó el cuerpo (en el mundo), antes de apagar la física: de ahí parte la animación de
			// levantarse.
			TArray<FTransform> GroundPose;
			GroundPose.SetNum(SkelMesh->GetNumBones());
			for (int32 BoneIdx = 0; BoneIdx < GroundPose.Num(); ++BoneIdx)
			{
				GroundPose[BoneIdx] = SkelMesh->GetBoneTransform(BoneIdx);
			}

			// Mover el capsule a donde cayó el ragdoll antes de desactivar física,
			// para que no teleporte de vuelta a la pos pre-knockdown. Con los pies en el suelo de debajo del cuerpo
			// (si lo encuentra), no a la altura de la cadera tumbada.
			{
				const FBodyInstance* RootBody = SkelMesh->GetBodyInstance();
				const FVector RagdollLoc = RootBody && RootBody->IsValidBodyInstance()
					? RootBody->GetUnrealWorldTransform().GetLocation()
					: SkelMesh->GetBoneLocation(SkelMesh->GetBoneName(0), EBoneSpaces::WorldSpace);
				// Sobre el suelo de debajo del cuerpo (o de encima, si el cuerpo acabó por debajo); si no lo hay, donde
				// estaba de pie al caer. Y sin quedar metida en nada: si no, la cápsula se «desincrusta» hacia abajo por la
				// colisión fina del terreno y cae por debajo del mapa.
				FVector StandLoc = PreKnockdownStandLocation;
				if (!FindStandSpotNear(RagdollLoc, StandLoc))
				{
					StandLoc = PreKnockdownStandLocation;
				}
				// La cápsula vuelve a chocar ANTES de buscar sitio: FindTeleportSpot solo aparta una cápsula con colisión de
				// consulta. Con la del ragdoll (sin colisión) no hacía nada y, junto a una roca o una muralla, la tortuga se
				// levantaba con media cápsula (y la malla) dentro. El ragdoll no choca con cápsulas (perfil Ragdoll).
				if (UCapsuleComponent* Cap = GetCapsuleComponent())
				{
					Cap->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				}
				if (UWorld* World = GetWorld())
				{
					World->FindTeleportSpot(this, StandLoc, GetActorRotation());
				}
				SetActorLocation(StandLoc, false, nullptr, ETeleportType::TeleportPhysics);
			}
			bRagdollProbeValid = false;

			SkelMesh->SetAllBodiesPhysicsBlendWeight(0.f);
			SkelMesh->SetAllBodiesSimulatePhysics(false);
			SkelMesh->bPauseAnims = false;
			SkelMesh->SetCollisionProfileName(SnapshotSkelMeshCollisionProfile);

			// NO AttachToComponent: nunca hicimos detach. Solo restaurar la relative
			// transform al snapshot para devolver el mesh a su pose "vivo" sobre el capsule.
			SkelMesh->SetRelativeTransform(SnapshotSkelMeshRelTransform);

			if (UCapsuleComponent* Cap = GetCapsuleComponent())
			{
				Cap->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			}

			if (CMC_Ragdoll)
			{
				CMC_Ragdoll->SetComponentTickEnabled(true);
				CMC_Ragdoll->SetMovementMode(MOVE_Walking);
				CMC_Ragdoll->NetworkSmoothingMode = ENetworkSmoothingMode::Exponential;
			}

			if (HasAuthority())
			{
				SetReplicateMovement(true);
			}

			bKnockdownRagdollActive = false;

			// Levantarse: la malla ya está en la cápsula; la animación va de la pose del suelo a la de pie.
			BeginGetUpFromWorldPose(GroundPose);
			if (const UWorld* World = GetWorld())
			{
				GetUpLockUntil = World->GetTimeSeconds() + GetUpSeconds;
			}
		}
		return;
	}

	// ── Ruta B: fallback tilt manual (comportamiento pre-Q1-07) ────────────────
	USceneComponent* VisComp = KnockdownVisualComp.Get();

	// ── Fallback: si el multicast llegó antes de que BeginPlay encontrara el componente,
	// intentar resolverlo ahora con la misma lógica de búsqueda ──
	if (!VisComp)
	{
		// 1) Buscar por nombre configurable
		if (KnockdownComponentName != NAME_None)
		{
			if (USceneComponent* Named = FindChildByName(KnockdownComponentName))
			{
				KnockdownVisualComp = Named;
			}
		}
		// 2) Fallback: SkeletalMesh con asset
		if (!KnockdownVisualComp.IsValid())
		{
			if (USkeletalMeshComponent* FallbackSkelMesh = GetMesh())
			{
				if (FallbackSkelMesh->GetSkeletalMeshAsset())
				{
					KnockdownVisualComp = FallbackSkelMesh;
				}
			}
		}
		if (!KnockdownVisualComp.IsValid())
		{
			for (UActorComponent* Comp : GetComponents())
			{
				if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Comp))
				{
					if (SMC != GetRootComponent() && SMC != HelmetMeshComp && SMC->GetStaticMesh())
					{
						KnockdownVisualComp = SMC;
						break;
					}
				}
			}
		}
		if (!KnockdownVisualComp.IsValid())
		{
			TArray<USceneComponent*> AllChildren;
			if (GetRootComponent())
			{
				GetRootComponent()->GetChildrenComponents(true, AllChildren);
			}
			for (USceneComponent* Child : AllChildren)
			{
				if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Child))
				{
					if (SMC != HelmetMeshComp && SMC->GetStaticMesh())
					{
						KnockdownVisualComp = SMC;
						break;
					}
				}
			}
		}
		if (!KnockdownVisualComp.IsValid())
		{
			KnockdownVisualComp = GetMesh();
		}
		if (KnockdownVisualComp.IsValid())
		{
			MeshDefaultRelativeRotation = KnockdownVisualComp->GetRelativeRotation();
		}
		VisComp = KnockdownVisualComp.Get();
	}

	if (!VisComp)
	{
		UE_LOG(LogTortunabo, Error, TEXT("[Knockdown] ApplyKnockdownVisual(%s): No visual component on %s — knockdown tilt invisible! "
			"HasAuthority=%s IsLocal=%s Mesh=%s"),
			bKnocked ? TEXT("true") : TEXT("false"),
			*GetNameSafe(this),
			HasAuthority() ? TEXT("Y") : TEXT("N"),
			IsLocallyControlled() ? TEXT("Y") : TEXT("N"),
			GetMesh() ? *GetNameSafe(GetMesh()) : TEXT("NULL"));
		return;
	}

	UCharacterMovementComponent* CMC = GetCharacterMovement();

	if (bKnocked)
	{
		// ── Desactivar smoothing del CMC para que NO sobreescriba la rotación ──
		if (CMC)
		{
			CMC->NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;
		}

		FRotator KnockedRot = MeshDefaultRelativeRotation;
		KnockedRot.Roll += 180.0f;
		VisComp->SetRelativeRotation(KnockedRot);
	}
	else
	{
		VisComp->SetRelativeRotation(MeshDefaultRelativeRotation);

		// ── Restaurar smoothing al salir del knockdown ─────────────────────
		if (CMC)
		{
			CMC->NetworkSmoothingMode = ENetworkSmoothingMode::Exponential;
		}
	}

	UE_LOG(LogTortunabo, Verbose, TEXT("[Knockdown] ApplyKnockdownVisual(%s) on %s — comp=%s"),
		bKnocked ? TEXT("true") : TEXT("false"), *GetNameSafe(this), *VisComp->GetName());
}

void ATortugaCharacter::BeginGetUpFromWorldPose(const TArray<FTransform>& WorldPose)
{
	USkeletalMeshComponent* SkelMesh = GetMesh();
	const USkeletalMesh* MeshAsset = SkelMesh ? SkelMesh->GetSkeletalMeshAsset() : nullptr;
	UTN_TurtleAnimInstance* TurtleAnim = SkelMesh ? Cast<UTN_TurtleAnimInstance>(SkelMesh->GetAnimInstance()) : nullptr;
	if (!MeshAsset || !TurtleAnim || WorldPose.Num() == 0) { return; }

	// Mundo → espacio de la malla (ya en su sitio nuevo sobre la cápsula) → locales respecto del padre.
	const FReferenceSkeleton& RefSkel = MeshAsset->GetRefSkeleton();
	const int32 NumBones = FMath::Min(WorldPose.Num(), RefSkel.GetNum());
	const FTransform ToComponent = SkelMesh->GetComponentTransform();
	TArray<FTransform> CompSpace;
	CompSpace.SetNum(NumBones);
	for (int32 BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx)
	{
		CompSpace[BoneIdx] = WorldPose[BoneIdx].GetRelativeTransform(ToComponent);
	}
	TArray<FTransform> LocalPose;
	LocalPose.SetNum(NumBones);
	for (int32 BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx)
	{
		const int32 ParentIdx = RefSkel.GetParentIndex(BoneIdx);
		LocalPose[BoneIdx] = (ParentIdx == INDEX_NONE || ParentIdx >= NumBones)
			? CompSpace[BoneIdx]
			: CompSpace[BoneIdx].GetRelativeTransform(CompSpace[ParentIdx]);
		LocalPose[BoneIdx].SetScale3D(FVector::OneVector);
		LocalPose[BoneIdx].NormalizeRotation();
	}
	TurtleAnim->BeginGetUp(LocalPose, GetUpSeconds);
}

// ─────────────────────────────────────────────────────────────────────────────
// ── Kill interface ────────────────────────────────────────────────────────────

void ATortugaCharacter::RequestKill(AActor* KillInstigator)
{
	if (!HasAuthority()) { return; }

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) { return; }

	ATN_RunGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ATN_RunGameMode>() : nullptr;
	if (!GM) { return; }

	UE_LOG(LogTortunabo, Log, TEXT("[Character] RequestKill on '%s' by '%s'"),
		*GetNameSafe(this), *GetNameSafe(KillInstigator));

	GM->MarkPlayerDead(PC);
}

// DEATH VISUAL SYSTEM
// ─────────────────────────────────────────────────────────────────────────────

void ATortugaCharacter::SetDeadVisual(bool bDead)
{
	if (!HasAuthority()) { return; }

	// Morir saca del caparazon por el mismo motivo que el derribo: el speed cap
	// no debe sobrevivir al revive.
	if (bDead && ShellComponent)
	{
		ShellComponent->ForceExitShell();
	}

	bIsDead = bDead;
	// DualMax round 2 — Codex CRITICAL: garantizar entrega del UPROPERTY replicado
	// a clientes especteadores. ForceNetUpdate fuerza al actor a entrar en next
	// replication pass; FlushNetDormancy despierta el actor si estaba dormant
	// (caso reportado: tras revive previo, segunda muerte no disparaba OnRep_IsDead
	// en cliente especteador porque cliente creía bIsDead=true).
	FlushNetDormancy();
	ForceNetUpdate();

	UE_LOG(LogTortunabo, Warning, TEXT("[DeathState][SERVER] %s SetDeadVisual(%d) bIsDead=%d RepMove=%d Dormant=%d Role=%d"),
		*GetName(), bDead, bIsDead, IsReplicatingMovement() ? 1 : 0, (int32)NetDormancy, (int32)GetLocalRole());

	USkeletalMeshComponent* SkelMesh = GetMesh();
	FVector GroundLocation = GetActorLocation();
	if (bDead)
	{
		// Do not snap death ragdoll down to the floor. Knockdown works because it
		// starts from the current pose/location; snapping to capsule half-height can
		// spawn low leg bodies already intersecting the ground.
		float RequiredLift = DeathRagdollSpawnLift;
		if (SkelMesh && GetWorld())
		{
			FHitResult GroundHit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(DeathRagdollFloorClearance), false, this);
			Params.AddIgnoredActor(this);
			const FVector ActorLoc = GetActorLocation();
			const FVector TraceStart = ActorLoc + FVector(0.f, 0.f, 150.f);
			const FVector TraceEnd = ActorLoc - FVector(0.f, 0.f, 1000.f);
			bool bFoundGround = GetWorld()->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_WorldStatic, Params);
			if (!bFoundGround)
			{
				bFoundGround = GetWorld()->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_WorldDynamic, Params);
			}
			if (bFoundGround)
			{
				const float MeshBottomZ = SkelMesh->Bounds.GetBox().Min.Z;
				const float DesiredBottomZ = GroundHit.ImpactPoint.Z + DeathRagdollFloorClearance;
				RequiredLift = FMath::Max(RequiredLift, DesiredBottomZ - MeshBottomZ);
			}
		}

		GroundLocation = GetActorLocation() + FVector(0.f, 0.f, RequiredLift);
		SetActorLocation(GroundLocation, false, nullptr, ETeleportType::TeleportPhysics);
		UE_LOG(LogTortunabo, Log, TEXT("[Death] Ragdoll spawn lift %.1fcm (base=%.1f clearance=%.1f)"),
			RequiredLift, DeathRagdollSpawnLift, DeathRagdollFloorClearance);

		EnterRagdollState();
		SetReplicateMovement(false);

		// R2: freeze diferido. Tras RagdollFreezeDelay de simulación local, el
		// servidor captura la posición autoritativa del cadáver y todas las
		// máquinas congelan y snapean (bRagdollFrozen + RagdollFrozenLoc).
		GetWorldTimerManager().SetTimer(RagdollFreezeTimerHandle,
			FTimerDelegate::CreateUObject(this, &ATortugaCharacter::ServerFreezeRagdoll),
			RagdollFreezeDelay, false);
	}
	else
	{
		bCanAirDash = true;

		// Cancelar el freeze pendiente y limpiar el flag replicado antes de
		// revivir (en clientes, OnRep_RagdollFrozen(false) es un no-op).
		GetWorldTimerManager().ClearTimer(RagdollFreezeTimerHandle);
		bRagdollFrozen = false;

		ExitRagdollState();

		// Restaurar movement replication para el pawn revivido.
		SetReplicateMovement(true);
	}

	MulticastSetDeadVisual(bDead, GroundLocation);

	// Servidor / listen-server: disparar el evento BP aquí (los clientes lo reciben
	// dentro de MulticastSetDeadVisual_Implementation).
	OnDeathVisualSet(bDead);

	UE_LOG(LogTortunabo, Log, TEXT("[Death] %s dead visual = %s"), *GetNameSafe(this), bDead ? TEXT("RAGDOLL") : TEXT("ALIVE"));
}

void ATortugaCharacter::EnterRagdollState()
{
	USkeletalMeshComponent* SkelMesh = GetMesh();
	UE_LOG(LogTortunabo, Warning, TEXT("[Ragdoll][ENTER_BEGIN] %s NetMode=%d Role=%d MeshSim=%d Bodies=%d PA=%s"),
		*GetName(), (int32)GetNetMode(), (int32)GetLocalRole(),
		SkelMesh && SkelMesh->IsSimulatingPhysics() ? 1 : 0,
		SkelMesh ? SkelMesh->Bodies.Num() : -1,
		SkelMesh && SkelMesh->GetPhysicsAsset() ? TEXT("YES") : TEXT("NO"));

	if (!SkelMesh) { return; }

	// Fallback sin PhysicsAsset: no hay ragdoll real. Se congela la pose actual
	// (AnimationCustomMode evita la T-pose), se tumba el mesh 90 grados hacia
	// delante y se apagan capsula y CMC igual que en la ruta fisica.
	// ExitRagdollState restaura todo via snapshot + SetAnimationMode(AnimationBlueprint).
	if (!SkelMesh->GetPhysicsAsset())
	{
		SnapshotSkelMeshRelTransform     = SkelMesh->GetRelativeTransform();
		// Sin la subida del panzazo: al revivir, la malla vuelve a su sitio sobre la cápsula.
		SnapshotSkelMeshRelTransform.SetLocation(DiveMeshDefaultLoc);
		SnapshotSkelMeshRelTransform.SetScale3D(DiveMeshDefaultScale);
		SnapshotSkelMeshCollisionProfile = SkelMesh->GetCollisionProfileName();

		if (UAnimInstance* AnimInst = SkelMesh->GetAnimInstance())
		{
			AnimInst->StopAllMontages(0.f);
		}
		SkelMesh->SetAnimationMode(EAnimationMode::AnimationCustomMode);
		SkelMesh->bBlendPhysics = false;

		// Se conservan Yaw y Roll del snapshot para que caiga en la direccion que mira.
		const FRotator SnapRot = SnapshotSkelMeshRelTransform.GetRotation().Rotator();
		SkelMesh->SetRelativeRotation(FRotator(90.f, SnapRot.Yaw, SnapRot.Roll));

		if (UCapsuleComponent* Cap = GetCapsuleComponent())
		{
			Cap->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		if (UCharacterMovementComponent* CMC = GetCharacterMovement())
		{
			CMC->StopMovementImmediately();
			CMC->DisableMovement();
			CMC->NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;
			CMC->SetComponentTickEnabled(false);
		}

		UE_LOG(LogTortunabo, Warning, TEXT("[Ragdoll] FALLBACK sin PhysicsAsset: pose congelada + tilt 90 en %s"), *GetName());
		return;
	}

	// Idempotente: si ya simula no re-arranca.
	if (SkelMesh->IsSimulatingPhysics())
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Ragdoll][EARLY_OUT_ALREADY_SIM] %s Bodies=%d"),
			*GetName(), SkelMesh->Bodies.Num());
		return;
	}

	// 1. Stop montages, but keep the same startup order as knockdown ragdoll:
	//    simulate first, pause animation after bodies are awake. This avoids a
	//    death-only path where a frozen/custom pose starts partially interpenetrating
	//    the floor and Chaos keeps resolving it forever.
	if (UAnimInstance* AnimInst = SkelMesh->GetAnimInstance())
	{
		AnimInst->Montage_Stop(0.f);
		AnimInst->StopAllMontages(0.f);
	}

	// 2. Snapshot del state actual para poder revivir limpiamente.
	SnapshotSkelMeshRelTransform    = SkelMesh->GetRelativeTransform();
	// Sin la subida del panzazo: al revivir, la malla vuelve a su sitio sobre la cápsula.
	SnapshotSkelMeshRelTransform.SetLocation(DiveMeshDefaultLoc);
	SnapshotSkelMeshRelTransform.SetScale3D(DiveMeshDefaultScale);
	SnapshotSkelMeshCollisionProfile = SkelMesh->GetCollisionProfileName();

	// 3. Capsule no colisiona (evita interacción con bodies del SkM ragdoll).
	if (UCapsuleComponent* Cap = GetCapsuleComponent())
	{
		Cap->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// 4. CMC apagado.
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->StopMovementImmediately();
		CMC->DisableMovement();
		CMC->NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;
		CMC->SetComponentTickEnabled(false);
	}

	// 5. (movido al caller — DualMax round 3): el LineTrace + SetActorLocation
	//    se hacen ANTES de EnterRagdollState. Server lo calcula en SetDeadVisual
	//    y lo envía como param del Multicast; cliente lo aplica en
	//    MulticastSetDeadVisual_Implementation antes de llamar EnterRagdollState.
	//    Esto elimina la divergencia client/server por LineTrace local
	//    inconsistente y la race condition entre RPC y bReplicateMovement.

	// 6. Use the same collision setup as knockdown. The Ragdoll profile should be
	//    authored in the PhysicsAsset/project settings; do not override it here.
	SkelMesh->SetCollisionProfileName(RagdollCollisionProfile);
	// Colisión continua: el ragdoll de la muerte tampoco atraviesa la colisión fina del terreno.
	SkelMesh->SetAllUseCCD(true);

	// 7. (movido al paso 11 — bBlendPhysics se setea AL FINAL, después de
	//    SetSimulate + Wake. Triple Mode round 2 — Gemini 8/10: setear
	//    bBlendPhysics ANTES de simulate hacía que los bodies pelearan con la
	//    kinematic anim pose; el solver aplicaba constraint resolution suave
	//    que se veía como "vuela lentamente sin gravedad". Bodies arrancan sim
	//    primero dictando pose; blend se activa después con bodies ya en control.)

	// 8. Damping: dejar valores del PhysicsAsset (NO forzar 0). DualMax round 2
	//    — Codex CRITICAL: damping=0 era mala idea para cadáveres porque Chaos
	//    tenía menos disipación de energía → micro-vibración persistente al
	//    asentarse (jitter reportado). El PhysicsAsset del mannequin ya tiene
	//    damping moderado calibrado. Si en playtests el ragdoll sigue
	//    micro-vibrando, aplicar uniforme: LinearDamping=0.8, AngularDamping=1.5.

	// 9. Activar simulación en TODOS los bodies. Llamar SOLO SetAllBodies; añadir
	//    SetSimulatePhysics(true) puede sobrescribir el state del root body que
	//    SetAllBodies acaba de configurar.
	SkelMesh->SetAllBodiesSimulatePhysics(true);
	SkelMesh->SetAllBodiesPhysicsBlendWeight(1.f);
	SkelMesh->SetEnableGravity(true);
	// Chaos: Body->SetEnableGravity(true) — esta API propaga al solver vía
	// FPhysicsInterface::SetGravityEnabled_AssumesLocked cuando IsSimulatingPhysics
	// es true. Como llegamos aquí DESPUÉS de SetAllBodiesSimulatePhysics(true),
	// los bodies sí están simulando → propagación correcta.
	// (Triple round 2 sugirió bEnableGravity + UpdatePhysicsProperties, pero esa
	// función no existe en FBodyInstance; SetEnableGravity es la API canónica.)
	for (FBodyInstance* Body : SkelMesh->Bodies)
	{
		if (Body) { Body->SetEnableGravity(true); }
	}

	// 10. Vel inicial CERO defensivo (después de simulate, no antes — antes los
	//     bodies aún no existían en el simulator).
	SkelMesh->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
	SkelMesh->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector);
	SkelMesh->WakeAllRigidBodies();

	// 11. bBlendPhysics AL FINAL — bodies ya simulan + están awake; ahora el
	//     blend permite que la sim física domine sobre cualquier pose anim
	//     residual sin que los bodies tengan que pelear con kinematic targets.
	SkelMesh->bBlendPhysics = true;
	SkelMesh->bPauseAnims = true;

	// Marca local del arranque de la simulación — ApplyRagdollFreeze la usa para
	// diferir el freeze si la sim acaba de empezar (caso JIP).
	if (const UWorld* World = GetWorld())
	{
		LocalRagdollStartTime = World->GetTimeSeconds();
	}

	UE_LOG(LogTortunabo, Warning, TEXT("[Ragdoll] ENTER (%s) auth=%d · AnimPaused=true(post-wake) · BlendPhysics=true(post-wake) · bodies=%d"),
		*GetName(), HasAuthority(), SkelMesh->Bodies.Num());
}

void ATortugaCharacter::ExitRagdollState()
{
	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (!SkelMesh) { return; }

	if (SkelMesh->IsSimulatingPhysics())
	{
		SkelMesh->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
		SkelMesh->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector);
		SkelMesh->SetAllBodiesSimulatePhysics(false);
	}
	SkelMesh->SetAllBodiesPhysicsBlendWeight(0.f);
	SkelMesh->bBlendPhysics = false;
	SkelMesh->bPauseAnims = false;
	SkelMesh->SetEnableGravity(false);

	// Restaurar AnimBP — EnterRagdollState lo apagó con AnimationCustomMode.
	SkelMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	if (SnapshotSkelMeshCollisionProfile != NAME_None)
	{
		SkelMesh->SetCollisionProfileName(SnapshotSkelMeshCollisionProfile);
	}
	SkelMesh->SetRelativeTransform(SnapshotSkelMeshRelTransform);

	if (UCapsuleComponent* Cap = GetCapsuleComponent())
	{
		Cap->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Cap->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		Cap->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
		Cap->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	}
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->SetComponentTickEnabled(true);
		CMC->NetworkSmoothingMode = ENetworkSmoothingMode::Exponential;
	}

	UE_LOG(LogTortunabo, Log, TEXT("[Ragdoll] EXIT (%s) authority=%d"), *GetName(), HasAuthority());
}

void ATortugaCharacter::OnRep_IsDead()
{
	USkeletalMeshComponent* SkM = GetMesh();
	UE_LOG(LogTortunabo, Warning, TEXT("[DeathState][ONREP] %s bIsDead=%d NetMode=%d Role=%d RemoteRole=%d MeshSim=%d Bodies=%d PA=%s"),
		*GetName(), bIsDead, (int32)GetNetMode(), (int32)GetLocalRole(), (int32)GetRemoteRole(),
		SkM && SkM->IsSimulatingPhysics() ? 1 : 0,
		SkM ? SkM->Bodies.Num() : -1,
		SkM && SkM->GetPhysicsAsset() ? TEXT("YES") : TEXT("NO"));
	// JIP late-join: cliente recibe bIsDead replicado y aplica el state
	// correspondiente vía las helpers compartidas (mismo patrón canónico).
	if (bIsDead) { EnterRagdollState(); }
	else         { ExitRagdollState();  }
}

void ATortugaCharacter::MulticastSetDeadVisual_Implementation(bool bDead, FVector GroundLocation)
{
	UE_LOG(LogTortunabo, Warning, TEXT("[DeathState][MC] %s bDead=%d NetMode=%d HasAuthority=%d Role=%d RemoteRole=%d Ground=(%.0f,%.0f,%.0f)"),
		*GetName(), bDead, (int32)GetNetMode(), HasAuthority() ? 1 : 0, (int32)GetLocalRole(), (int32)GetRemoteRole(),
		GroundLocation.X, GroundLocation.Y, GroundLocation.Z);

	// Sonido de muerte: se reproduce en TODAS las máquinas (incluido listen-server)
	// antes del early-return de autoridad. Anclado a la pos actual del actor — el
	// teleport a GroundLocation aún no ha ocurrido, así que el sonido sale en la
	// pos visible donde murió.
	if (bDead && KillSound)
	{
		PlaySfxAtSelf(KillSound);
	}

	if (HasAuthority()) { return; }  // server ya lo hizo en SetDeadVisual
	if (bDead)
	{
		// DualMax round 3: cliente teleporta a la pos GROUND-SNAP autoritativa
		// recibida con el RPC (no espera replicación de bReplicateMovement, que
		// llega después del MC). Después arranca sim en la pos correcta.
		SetActorLocation(GroundLocation, false, nullptr, ETeleportType::TeleportPhysics);
		EnterRagdollState();
	}
	else
	{
		ExitRagdollState();
	}
	// Notificar BP para que active el raptor, VFX, audio de muerte, etc.
	OnDeathVisualSet(bDead);
}

// ── R2: freeze/snap replicado del ragdoll de muerte ───────────────────────────

void ATortugaCharacter::ServerFreezeRagdoll()
{
	if (!HasAuthority() || !bIsDead || bRagdollFrozen)
	{
		return;
	}

	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (!SkelMesh || !SkelMesh->IsSimulatingPhysics())
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Ragdoll][FREEZE] abortado en %s — mesh null o sin simular"), *GetName());
		return;
	}

	// Posición autoritativa del root bone. La loc se escribe ANTES que el flag:
	// ambas propiedades viajan en el mismo bunch y el OnRep del flag la lee.
	RagdollFrozenLoc = SkelMesh->GetBoneLocation(SkelMesh->GetBoneName(0), EBoneSpaces::WorldSpace);
	bRagdollFrozen = true;
	FlushNetDormancy();
	ForceNetUpdate();

	// Listen-host: OnRep no dispara en la máquina con autoridad.
	ApplyRagdollFreeze();
}

void ATortugaCharacter::OnRep_RagdollFrozen()
{
	if (bRagdollFrozen && bIsDead)
	{
		ApplyRagdollFreeze();
	}
	// bRagdollFrozen=false llega con el revive: ExitRagdollState (vía
	// OnRep_IsDead / MulticastSetDeadVisual) ya restaura el estado vivo.
}

void ATortugaCharacter::ApplyRagdollFreeze()
{
	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (!bIsDead || !bRagdollFrozen || !SkelMesh)
	{
		return;
	}
	// Ya congelado en esta máquina (o revive en vuelo apagó la sim) → no-op.
	if (!SkelMesh->IsSimulatingPhysics())
	{
		return;
	}

	// JIP: bIsDead y bRagdollFrozen llegan en el mismo bunch inicial y el
	// ragdoll local acaba de arrancar. Petrificar la pose de spawn de pie se ve
	// roto — dejar ~0.5s de simulación para que Chaos pose el cuerpo primero.
	if (const UWorld* World = GetWorld())
	{
		constexpr float MinSimSeconds = 0.5f;
		const float SimTime = World->GetTimeSeconds() - LocalRagdollStartTime;
		if (LocalRagdollStartTime >= 0.f && SimTime < MinSimSeconds)
		{
			GetWorldTimerManager().SetTimer(RagdollFreezeTimerHandle,
				FTimerDelegate::CreateUObject(this, &ATortugaCharacter::ApplyRagdollFreeze),
				MinSimSeconds - SimTime, false);
			return;
		}
	}

	// Congelar: con bPauseAnims=true + bBlendPhysics, la última pose física está
	// cacheada en component-space → detener la simulación no produce salto.
	SkelMesh->PutAllRigidBodiesToSleep();
	SkelMesh->SetAllBodiesSimulatePhysics(false);

	// Snap por delta a la posición autoritativa: mover el ACTOR traslada el
	// cadáver congelado entero (la pose es component-space y el mesh sigue
	// attachado al capsule). En el server el delta es ~0; en clientes corrige
	// la divergencia acumulada de Chaos. Orden crítico: sim parada ANTES de
	// mover — los bodies simulados no siguen al actor, los kinematic sí.
	const FVector LocalRootLoc = SkelMesh->GetBoneLocation(SkelMesh->GetBoneName(0), EBoneSpaces::WorldSpace);
	const FVector Delta = FVector(RagdollFrozenLoc) - LocalRootLoc;
	AddActorWorldOffset(Delta, false, nullptr, ETeleportType::TeleportPhysics);

	UE_LOG(LogTortunabo, Log, TEXT("[Ragdoll][FREEZE] %s auth=%d delta=(%.0f,%.0f,%.0f) |delta|=%.0fcm"),
		*GetName(), HasAuthority() ? 1 : 0, Delta.X, Delta.Y, Delta.Z, Delta.Size());
}

void ATortugaCharacter::HideLimbs()
{
	if (GetMesh()) { GetMesh()->SetVisibility(false, true); }
	if (HelmetMeshComp) { HelmetMeshComp->SetVisibility(false, true); }
}

void ATortugaCharacter::ShowLimbs()
{
	if (GetMesh()) { GetMesh()->SetVisibility(true, true); }
	if (HelmetMeshComp) { HelmetMeshComp->SetVisibility(true, true); }
}

// ─────────────────────────────────────────────────────────────────────────────
// Ragdoll del derribo: sin atravesar el suelo y con la cámara siguiéndolo
// ─────────────────────────────────────────────────────────────────────────────

void ATortugaCharacter::TickKnockdownRagdoll(float DeltaTime)
{
	if (!bKnockdownRagdollActive)
	{
		bRagdollProbeValid = false;
		return;
	}
	USkeletalMeshComponent* SkelMesh = GetMesh();
	UWorld* World = GetWorld();
	FBodyInstance* RootBody = SkelMesh ? SkelMesh->GetBodyInstance() : nullptr;
	if (!World || !RootBody || !RootBody->IsValidBodyInstance() || !SkelMesh->IsSimulatingPhysics())
	{
		return;
	}
	FVector Probe = RootBody->GetUnrealWorldTransform().GetLocation();

	// 1) Si del fotograma anterior a este el cuerpo ha cruzado geometría estática (el terreno es una malla fina), se
	//    devuelve encima de lo que ha cruzado y se le quita la velocidad hacia dentro. Solo objetos estáticos: otras
	//    tortugas, enemigos o el propio ragdoll no cuentan.
	if (bRagdollProbeValid && FVector::DistSquared(Probe, RagdollProbeLast) > 1.0)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(KnockdownRagdollTunnel), true, this);
		if (World->LineTraceSingleByObjectType(Hit, RagdollProbeLast, Probe, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			const FVector Normal = Hit.ImpactNormal.IsNearlyZero() ? FVector::UpVector : Hit.ImpactNormal;
			const FVector Fix = Hit.ImpactPoint + Normal * KnockdownRagdollTunnelMargin - Probe;
			for (FBodyInstance* Body : SkelMesh->Bodies)
			{
				if (!Body || !Body->IsValidBodyInstance())
				{
					continue;
				}
				FTransform BodyXf = Body->GetUnrealWorldTransform();
				BodyXf.AddToTranslation(Fix);
				Body->SetBodyTransform(BodyXf, ETeleportType::TeleportPhysics);
				const FVector Vel = Body->GetUnrealWorldVelocity();
				const double Into = FVector::DotProduct(Vel, Normal);
				if (Into < 0.0)
				{
					Body->SetLinearVelocity(Vel - Into * Normal, false);
				}
			}
			Probe += Fix;
			UE_LOG(LogTortunabo, Warning, TEXT("[Ragdoll] %s atravesaba %s: devuelto %.0f cm encima."),
				*GetName(), *GetNameSafe(Hit.GetActor()), Fix.Size());
		}
	}
	RagdollProbeLast = Probe;
	bRagdollProbeValid = true;

	// 2) La cápsula (sin colisión ni movimiento mientras dura) sigue al cuerpo: la cámara, en su brazo, sigue a la
	//    tortuga tumbada como siempre en vez de quedarse donde empezó el golpe. Sin teletransporte físico: la malla
	//    simulada no se mueve con ella.
	const float HalfH = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f;
	SetActorLocation(Probe + FVector(0.f, 0.f, HalfH * 0.5f), false, nullptr, ETeleportType::None);
}

bool ATortugaCharacter::FindStandSpotNear(const FVector& From, FVector& OutStandLoc) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const float HalfH = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(KnockdownGetUpFloor), true, this);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FHitResult Hit;
	// Primero, el suelo justo debajo del cuerpo (lo normal).
	if (World->LineTraceSingleByObjectType(Hit, From + FVector(0.0, 0.0, 120.0), From - FVector(0.0, 0.0, 600.0), Objects, Params)
		&& Hit.ImpactNormal.Z > 0.3)
	{
		OutStandLoc = Hit.ImpactPoint + FVector(0.0, 0.0, HalfH + 2.0);
		return true;
	}
	// Si no hay, el cuerpo puede haber quedado por debajo de la superficie: se busca desde bastante más arriba.
	if (World->LineTraceSingleByObjectType(Hit, From + FVector(0.0, 0.0, 3000.0), From - FVector(0.0, 0.0, 600.0), Objects, Params)
		&& Hit.ImpactNormal.Z > 0.3)
	{
		OutStandLoc = Hit.ImpactPoint + FVector(0.0, 0.0, HalfH + 2.0);
		return true;
	}
	return false;
}
