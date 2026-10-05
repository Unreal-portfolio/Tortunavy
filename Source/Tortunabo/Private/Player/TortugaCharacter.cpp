#include "Player/TortugaCharacter.h"
#include "Core/TN_Log.h"
#include "Player/MP_GamePlayerController.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "GameFramework/PlayerController.h"
#include "Components/PostProcessComponent.h"
#include "Player/TN_InventoryComponent.h"
#include "Core/TN_CosmeticLook.h"
#include "Art/TN_TurtleArt.h"
#include "Player/TN_ShellBody.h"
#include "Player/TN_ShellComponent.h"
#include "Player/TN_ShellImpactFXComponent.h"
#include "Player/TN_CarriedCamera.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_DizzyBirdsComponent.h"
#include "Player/TN_HeadLook.h"
#include "Player/TN_TurtleFaceComponent.h"
#include "Player/TN_SlopeTiltComponent.h"
#include "Player/TN_StaminaComponent.h"
#include "Player/TN_WadingComponent.h"
#include "VR/TN_VRGrabComponent.h"
#include "Player/TN_ProcAnimInstance.h"
#include "Player/TN_TurtleAnimInstance.h"
#include "Player/TN_TurtleDustComponent.h"
#include "Player/TN_TurtleFoleyComponent.h"
#include "Player/TN_TurtleMovementComponent.h"
#include "Player/TN_TurtleActionSfx.h"
#include "World/TN_InteractableBase.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "World/Beach/TN_BeachTrampoline.h"
#include "GameFramework/PlayerState.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/AudioComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_CosmeticsTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "World/Beach/TN_BeachTrapStatusComponent.h"

// ── CVar de debug ─────────────────────────────────────────────────────────────
// Activar en consola con: TN.Debug.Interaction 1
// Desactivar con: TN.Debug.Interaction 0
// NB: linkage externo (no static) para que TortugaCharacter_Interaction.cpp, que
// contiene los métodos que también lo consultan, pueda referenciarlo vía extern.
TAutoConsoleVariable<int32> CVarDebugInteraction(
	TEXT("TN.Debug.Interaction"),
	0,
	TEXT("1 = Draw debug lines/spheres para el raycast de interacción y logs detallados. 0 = off."),
	ECVF_Cheat);

ATortugaCharacter::ATortugaCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UTN_TurtleMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;   // needed for leg animation
	SpawnCollisionHandlingMethod = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	// Sonidos de acción de serie (Docs/Sonido_Tortuga.md, «Acciones»); el Blueprint puede cambiarlos.
	KillSound = TNTurtleActionSfx::FindDefaultSound(ETNTurtleActionSfx::Kill);
	PickupSound = TNTurtleActionSfx::FindDefaultSound(ETNTurtleActionSfx::Pickup);
	ThrowSound = TNTurtleActionSfx::FindDefaultSound(ETNTurtleActionSfx::Throw);
	ConsumeSound = TNTurtleActionSfx::FindDefaultSound(ETNTurtleActionSfx::Consume);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
	// 30 Hz (10 como mínimo con la frecuencia adaptativa: quieta o lejos): el movimiento de los demás va suavizado y el del
	// dueño lo corrigen sus propios RPC. Con 60/30 Hz y ocho jugadores, el anfitrión mandaba el doble a cada cliente.
	SetNetUpdateFrequency(30.f);
	SetMinNetUpdateFrequency(10.f);
	// Las tortugas de los jugadores (ocho como mucho) llegan siempre a todas las máquinas, estén donde estén: con la
	// distancia de corte de serie (150 m), en el mapa procedural y en la playa un cliente perdía la tortuga lejana y su
	// cara del HUD (energía, caparazón), su marca en la pista y el espectador que la sigue se quedaban congelados.
	bAlwaysRelevant = true;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 360.f, 0.f);
	GetCharacterMovement()->NetworkSmoothingMode = ENetworkSmoothingMode::Exponential;

	// bEnablePhysicsInteraction habilita PushForceFactor/TouchForceFactor sobre rigid bodies
	// por contacto. 0.5 = empuje sutil suficiente para mover cajas/bolas en abierto pero
	// NO para tunnelearlas contra paredes estáticas (sandwich-through). Tunable en runtime
	// desde BP si se quiere iterar.
	GetCharacterMovement()->bEnablePhysicsInteraction = true;
	GetCharacterMovement()->PushForceFactor = 0.5f;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 350.f;
	CameraBoom->bUsePawnControlRotation = true;

	// ── Colisión del spring arm con paredes ───────────────────────────────────
	// Usamos el comportamiento nativo del SpringArm (bDoCollisionTest) en lugar de
	// casts manuales: es O(1) por frame, se ejecuta dentro del componente y ya
	// gestiona interpolación de retracción/extensión. Explicitamos los campos
	// por si algún Class Default en BP los hubiera puesto a false.
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->ProbeChannel     = ECC_Camera;  // todas las paredes bloquean ECC_Camera por defecto
	CameraBoom->ProbeSize        = 30.f;         // CAM-01 iter 2: 22 seguía clipando suelo al mirar arriba.
	                                             // 30 cubre la mayoría de casos. Complementado con un
	                                             // floor-clamp manual en Tick (ver ClampCameraAboveFloor).

	// ── Cinematic camera lag ──────────────────────────────────────────────────
	// Suaviza la posición de la cámara para un feel AAA fluido.
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 8.f;                 // match CameraPositionLagSpeed default

	// Suaviza la rotación de la cámara independientemente de la posición.
	CameraBoom->bEnableCameraRotationLag = true;
	CameraBoom->CameraRotationLagSpeed = 14.f;        // match CameraRotationLagSpeed default

	// Distancia máxima que el lag puede acumular antes de hacer "snap".
	CameraBoom->CameraLagMaxDistance = 180.f;

	// Over-the-shoulder offset: ligeramente a la derecha y elevada.
	CameraBoom->SocketOffset = FVector(0.f, 55.f, 65.f);

	// Eleva el pivot del boom sobre la raíz del personaje (encima de la cabeza).
	CameraBoom->SetRelativeLocation(FVector(0.f, 0.f, 40.f));

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
	FollowCamera->FieldOfView = 80.f;                 // match CameraFOVDefault

	// Paths must match actual asset locations in Content/Blueprints/Gameplay/Controls/.
	// BP_TortugaCharacter can override these in Class Defaults.
	DefaultMappingContext = TSoftObjectPtr<UInputMappingContext>(FSoftObjectPath(TEXT("/Game/Blueprints/Gameplay/Controls/IMC_Player.IMC_Player")));
	MoveAction = TSoftObjectPtr<UInputAction>(FSoftObjectPath(TEXT("/Game/Blueprints/Gameplay/Controls/IA_Move.IA_Move")));
	LookAction = TSoftObjectPtr<UInputAction>(FSoftObjectPath(TEXT("/Game/Blueprints/Gameplay/Controls/IA_Look.IA_Look")));
	JumpAction = TSoftObjectPtr<UInputAction>(FSoftObjectPath(TEXT("/Game/Blueprints/Gameplay/Controls/IA_Jump.IA_Jump")));
	InteractAction = TSoftObjectPtr<UInputAction>(FSoftObjectPath(TEXT("/Game/Blueprints/Gameplay/Controls/IA_Interact.IA_Interact")));
	RotateInventoryAction = TSoftObjectPtr<UInputAction>(FSoftObjectPath(TEXT("/Game/Blueprints/Gameplay/Controls/IA_RotateInventory.IA_RotateInventory")));
	SprintAction = TSoftObjectPtr<UInputAction>(FSoftObjectPath(TEXT("/Game/Blueprints/Gameplay/Controls/IA_Sprint.IA_Sprint")));
	ShellAction = TSoftObjectPtr<UInputAction>(FSoftObjectPath(TEXT("/Game/Blueprints/Gameplay/Controls/IA_Shell.IA_Shell")));
	DropItemAction = TSoftObjectPtr<UInputAction>(FSoftObjectPath(TEXT("/Game/Blueprints/Gameplay/Controls/IA_DropItem.IA_DropItem")));

	// Emote actions: configured in BP_TortugaCharacter Class Defaults.
	// Array vacío por defecto para que el editor permita añadir/borrar filas individualmente.

	InventoryComponent = CreateDefaultSubobject<UTN_InventoryComponent>(TEXT("InventoryComponent"));
	StaminaComponent = CreateDefaultSubobject<UTN_StaminaComponent>(TEXT("StaminaComponent"));
	WadingComponent = CreateDefaultSubobject<UTN_WadingComponent>(TEXT("WadingComponent"));
	ShellComponent = CreateDefaultSubobject<UTN_ShellComponent>(TEXT("ShellComponent"));
	CarryComponent = CreateDefaultSubobject<UTN_CarryComponent>(TEXT("CarryComponent"));
	// La malla se inclina con la pendiente (solo visual; ver UTN_SlopeTiltComponent).
	SlopeTilt = CreateDefaultSubobject<UTN_SlopeTiltComponent>(TEXT("SlopeTilt"));
	DizzyBirds = CreateDefaultSubobject<UTN_DizzyBirdsComponent>(TEXT("DizzyBirds"));
	DizzyBirds->SetupAttachment(RootComponent);
	// Lengua, caras de cansancio, sudor y boca (se engancha sola a la cabeza de la malla en su primer fotograma).
	TurtleFace = CreateDefaultSubobject<UTN_TurtleFaceComponent>(TEXT("TurtleFace"));
	// Coger objetos con física con las aletas en VR (Docs/Modo_VR.md).
	VRGrabComponent = CreateDefaultSubobject<UTN_VRGrabComponent>(TEXT("VRGrab"));

	// Casco cosmético: adjunto directamente a GetMesh() (SkeletalMeshComponent).
	// Al estar en el árbol del mesh, recibe el network smoothing del CMC → sin lag.
	// Sin mesh asignado → invisible hasta que se equipe un casco real.
	HelmetMeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HelmetMesh"));
	HelmetMeshComp->SetupAttachment(GetMesh()); // IMPORTANTE: GetMesh, no RootComponent
	HelmetMeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HelmetMeshComp->SetIsReplicated(false); // Solo cosmético, no necesita replicación
	HelmetMeshComp->SetHiddenInGame(true);

	// Emote sounds: assign in BP Class Defaults. Array vacío por defecto.

	// Overlay de tinta: PostProcess local, desactivado por defecto.
	// bUnbound=true → afecta toda la pantalla del cliente local.
	// Se activa solo en IsLocallyControlled() — los demás clientes nunca lo ven.
	InkPostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("InkPostProcess"));
	InkPostProcess->SetupAttachment(RootComponent);
	InkPostProcess->bEnabled = false;
	InkPostProcess->bUnbound = true;

	// Make capsule AND mesh invisible to camera traces → the spring arm won't collide
	// with other players. Each player's own pawn is already auto-ignored.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetMesh()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
}

void ATortugaCharacter::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTortunabo, Log, TEXT("[TortugaCharacter] BeginPlay '%s' — LocallyControlled=%s  HasAuthority=%s  Controller=%s"),
		*GetName(),
		IsLocallyControlled() ? TEXT("YES") : TEXT("NO"),
		HasAuthority() ? TEXT("YES") : TEXT("NO"),
		GetController() ? *GetController()->GetName() : TEXT("NULL"));

	CacheInputAssets();
	ApplyInputMappingIfLocal();

	if (IsLocallyControlled() && InteractionScanInterval > 0.f)
	{
		GetWorldTimerManager().SetTimer(InteractionScanTimerHandle, this, &ATortugaCharacter::UpdateFocusedInteractable, InteractionScanInterval, true);
		UE_LOG(LogTortunabo, Log, TEXT("[TortugaCharacter] Interaction scan timer started (interval=%.2fs)"), InteractionScanInterval);
	}

	// Animación en C++ sobre el esqueleto de la malla (UTN_TurtleAnimInstance: clips de locomoción y poses de cada
	// acción), sea cual sea el AnimBP del Blueprint. Hereda de UTN_ProcAnimInstance: los ajustes por hueso de los
	// sistemas viejos siguen valiendo. Tiene que ir antes de InitBone para que la instancia exista en el primer Tick.
	if (GetMesh())
	{
		GetMesh()->SetAnimInstanceClass(UTN_TurtleAnimInstance::StaticClass());

		// HQ-WARN-01 defensive: si BP defaults o seamless travel dejan el SkM en
		// modo simulate-physics, el primer SetActorLocation/AttachToComponent dispara
		// "Attempting to move a fully simulated skeletal mesh". Reset explícito aquí.
		if (GetMesh()->IsSimulatingPhysics())
		{
			GetMesh()->SetSimulatePhysics(false);
			GetMesh()->bPauseAnims = false;
			UE_LOG(LogTortunabo, Warning, TEXT("[TortugaCharacter] BeginPlay: reset stale physics on %s"), *GetName());
		}
	}

	// ── ROUND 3 · FORZAR valores críticos sobre cualquier override de BP ──
	// Los UPROPERTY EditDefaultsOnly permiten al BP grabar valores antiguos que
	// pisan el constructor. Reasignar aquí garantiza que los fixes aplican.
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->bEnablePhysicsInteraction = true;
		CMC->PushForceFactor           = 1.0f;  // 2.0 tunneleaba · 0.5 no movía · 1.0 middle ground
		CMC->TouchForceFactor          = 1.0f;

		// Nado básico: más rápido que andar y más lento que esprintar. El agua la
		// ponen los volúmenes (APhysicsVolume con bWaterVolume) del mapa.
		CMC->GetNavAgentPropertiesRef().bCanSwim = true;
		CMC->MaxSwimSpeed = SwimSpeed;
		CMC->Buoyancy     = SwimBuoyancy;
	}
	if (CameraBoom)
	{
		CameraBoom->ProbeSize        = 30.f;
		CameraBoom->ProbeChannel     = ECC_Camera;
		CameraBoom->bDoCollisionTest = true;
	}

	// ── ROUND 3 · DIAGNOSTIC LOGS ──
	// Si los fixes siguen sin aplicar, estos logs revelan el estado real en runtime.
	UE_LOG(LogTortunabo, Verbose, TEXT("[Diagnostic] %s BeginPlay: PushForceFactor=%.2f ProbeSize=%.2f bUsePhysicsRagdoll=%s PhysicsAsset=%s"),
		*GetName(),
		GetCharacterMovement() ? GetCharacterMovement()->PushForceFactor : -1.f,
		CameraBoom ? CameraBoom->ProbeSize : -1.f,
		bUsePhysicsRagdoll ? TEXT("Y") : TEXT("N"),
		(GetMesh() && GetMesh()->GetPhysicsAsset()) ? TEXT("ASSIGNED") : TEXT("NULL"));

	ResolveAnimationBones();

	// Network smoothing: con el mesh unificado GetMesh() es el único componente visual.
	// El CMC ya aplica smoothing a GetMesh() y sus hijos directamente — no se necesita
	// re-adjuntar nada. El HelmetMeshComp está adjunto al socket "Sombrero" en GetMesh().

	ResolveKnockdownVisualComponent();

	// ── Dive: guardar rotaciones por defecto y HalfHeight de la cápsula ─────────
	// La de la clase manda: si el panzazo de otra tortuga llega antes que su BeginPlay (entrar a media partida), su
	// cápsula ya viene encogida.
	DiveCapsuleOrigHalfHeight = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	DiveCapsuleOrigHalfHeight = FMath::Max(DiveCapsuleOrigHalfHeight, GetStandingCapsuleHalfHeight());
	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		DiveMeshDefaultRot = SkelMesh->GetRelativeRotation();
		DiveMeshDefaultLoc = SkelMesh->GetRelativeLocation();
		DiveMeshDefaultScale = SkelMesh->GetRelativeScale3D();

		// El BP guarda de la malla vieja un PhysicsAssetOverride cuyos huesos no existen en TotugaDemo_Rig: sin
		// cuerpos físicos no hay ragdoll (plátano, muerte). Si el override no casa con la malla, se usa el suyo.
		UPhysicsAsset* Override = SkelMesh->PhysicsAssetOverride;
		UPhysicsAsset* Own = SkelMesh->GetSkeletalMeshAsset() ? SkelMesh->GetSkeletalMeshAsset()->GetPhysicsAsset() : nullptr;
		if (Override && Own && Override != Own)
		{
			bool bMatches = false;
			for (const TObjectPtr<USkeletalBodySetup>& Setup : Override->SkeletalBodySetups)
			{
				if (Setup && SkelMesh->GetBoneIndex(Setup->BoneName) != INDEX_NONE) { bMatches = true; break; }
			}
			if (!bMatches)
			{
				SkelMesh->SetPhysicsAsset(Own, true);
				UE_LOG(LogTortunabo, Log, TEXT("[Ragdoll] %s: PhysicsAssetOverride '%s' no casa con la malla; se usa '%s'."),
					*GetName(), *Override->GetName(), *Own->GetName());
			}
		}
	}

	ApplyCameraDefaultsFromProperties();

	// ── Vincular inventario al sistema de stamina para el peso ────────────────
	// Solo en el servidor (donde la stamina se actualiza), pero linkear en todos
	// es inofensivo porque GetTotalCarriedWeight solo lee datos replicados.
	if (StaminaComponent && InventoryComponent)
	{
		StaminaComponent->SetInventoryComponent(InventoryComponent);
	}

	// ── Cosmetics: casco inicial ──────────────────────────────────────────────
	// Adjuntar HelmetMeshComp al socket del Skeletal Mesh. El socket debe existir
	// en el mesh con el nombre configurado en HelmetSocketName.
	if (HelmetMeshComp && GetMesh())
	{
		HelmetMeshComp->AttachToComponent(GetMesh(),
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			HelmetSocketName);
		UE_LOG(LogTortunabo, Log, TEXT("[TortugaCharacter] HelmetMeshComp adjunto al socket '%s'."), *HelmetSocketName.ToString());
	}

	CacheDefaultSkelMeshMaterials();

	// Piezas de Arte de la tortuga (caparazón, casco de serie, ojos, lengua) aunque no llegue a vestirse con los cosméticos
	// de un jugador (las tortugas de práctica del tutorial no tienen PlayerState). Al vestirse se vuelven a poner.
	TNTurtleArt::ApplyPieces(GetMesh(), HelmetMeshComp && HelmetMeshComp->GetStaticMesh());

	StartCosmeticRetryTimer();

	// Pasos, aterrizajes y jadeo sintetizados (solo en máquinas con audio; lee el estado replicado, sin RPC).
	UTN_TurtleFoleyComponent::FindOrAddTo(this);

	// Polvo, arenilla, astillas o salpicaduras del arrastre del panzazo (cosmético y local; nada en servidor dedicado).
	UTN_TurtleDustComponent::FindOrAddTo(this);

	// Golpes de la bola del caparazón (sonido y mini efecto según contra qué choca): cosmético y local, cada máquina en su copia de la bola.
	// Se engancha sola a la caja física cuando aparece (sin tocar TN_ShellBody ni TN_ShellComponent); nada en servidor dedicado.
	UTN_ShellImpactFXComponent::FindOrAddTo(this);
}

void ATortugaCharacter::ResolveAnimationBones()
{
	// Resolve socket names to bone names on the Skeletal Mesh and capture rest poses.
	// Sockets must exist on the mesh with these exact names; angles will be tuned later.
	// Rest pose is read from the SOCKET transform (component-space), not directly from
	// the bone. This lets artists correct the rest orientation per-bone by rotating the
	// socket in the Skeleton Editor without recompiling.
	auto InitBone = [&](FName SocketName, FName& OutBone, FRotator& OutRestRot, FVector& OutRestLoc)
	{
		if (!GetMesh()) { return; }
		OutBone = GetMesh()->GetSocketBoneName(SocketName);
		if (OutBone != NAME_None)
		{
			const FTransform SocketTM = GetMesh()->GetSocketTransform(SocketName, RTS_Component);
			OutRestRot = SocketTM.Rotator();
			OutRestLoc = SocketTM.GetTranslation();
		}
		else
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[TortugaCharacter] Socket '%s' not found on mesh — animation for this bone disabled."), *SocketName.ToString());
		}
	};

	InitBone(TEXT("Pata1"),  Pata1Bone,  Pata1RestRot,  Pata1RestLoc);
	InitBone(TEXT("Pata2"),  Pata2Bone,  Pata2RestRot,  Pata2RestLoc);
	InitBone(TEXT("Brazo1"), Brazo1Bone, Brazo1RestRot, Brazo1RestLoc);
	InitBone(TEXT("Brazo2"), Brazo2Bone, Brazo2RestRot, Brazo2RestLoc);
	InitBone(TEXT("Cola"),   ColaBone,   ColaRestRot,   ColaRestLoc);
	InitBone(TEXT("Cabeza"), CabezaBone, CabezaRestRot, CabezaRestLoc);

	// Bone scale at rest is (1,1,1) in all UE5 skeletons — no query needed.
	CabezaRestScale = FVector::OneVector;

	// Log de diagnóstico: estado final de todos los huesos de emote
	UE_LOG(LogTortunabo, Log, TEXT("[TortugaCharacter] Resolved bones — Brazo1='%s' Brazo2='%s' Pata1='%s' Pata2='%s' Cola='%s' Cabeza='%s'"),
		*Brazo1Bone.ToString(), *Brazo2Bone.ToString(),
		*Pata1Bone.ToString(),  *Pata2Bone.ToString(),
		*ColaBone.ToString(),   *CabezaBone.ToString());
}

void ATortugaCharacter::ResolveKnockdownVisualComponent()
{
	// Guardar la rotación por defecto del mesh para restaurarla tras knockdown.
	// 1) Buscar por nombre configurable (KnockdownComponentName).
	if (KnockdownComponentName != NAME_None)
	{
		if (USceneComponent* Named = FindChildByName(KnockdownComponentName))
		{
			KnockdownVisualComp = Named;
			UE_LOG(LogTortunabo, Log, TEXT("[TortugaCharacter] KnockdownVisualComp → '%s' (por KnockdownComponentName)"), *Named->GetName());
		}
	}
	// 2) Fallback: SkeletalMesh con asset.
	if (!KnockdownVisualComp.IsValid())
	{
		if (USkeletalMeshComponent* SkelMesh = GetMesh())
		{
			if (SkelMesh->GetSkeletalMeshAsset())
			{
				KnockdownVisualComp = SkelMesh;
			}
		}
	}
	if (!KnockdownVisualComp.IsValid())
	{
		// Blockout: buscar el primer StaticMeshComponent hijo (directo)
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
		// Blockout con SceneComponents anidados: buscar recursivamente en hijos del root
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
	// Último fallback: el SkeletalMesh aunque esté vacío (para que el tilt se guarde)
	if (!KnockdownVisualComp.IsValid())
	{
		KnockdownVisualComp = GetMesh();
	}

	if (KnockdownVisualComp.IsValid())
	{
		MeshDefaultRelativeRotation = KnockdownVisualComp->GetRelativeRotation();
	}
}

void ATortugaCharacter::ApplyCameraDefaultsFromProperties()
{
	// ── Aplicar camera settings serializables ────────────────────────────────
	// Los valores UPROPERTY pueden haberse sobrescrito en el BP hijo → aplicarlos aquí.
	if (CameraBoom)
	{
		CameraBoom->TargetArmLength        = CameraArmLengthDefault;
		CameraBoom->CameraLagSpeed         = CameraPositionLagSpeed;
		CameraBoom->CameraRotationLagSpeed = CameraRotationLagSpeed;
		CameraBoom->SocketOffset           = CameraSocketOffset;
		CameraBoom->SetRelativeLocation(CameraBoomRelativeOffset);
	}
	if (FollowCamera)
	{
		FollowCamera->FieldOfView = CameraFOVDefault;
		// Inclinar la cámara ligeramente hacia abajo para bajar el punto de mira.
		// No afecta a Controller->GetControlRotation() — solo es cosmético en la cámara.
		FollowCamera->SetRelativeRotation(FRotator(CameraAimPitchOffset, 0.f, 0.f));
	}
}

void ATortugaCharacter::CacheDefaultSkelMeshMaterials()
{
	// Cachear los materiales originales de la malla: UTN_CosmeticLook parte siempre de ellos. Si un OnRep de
	// cosméticos llegó antes que BeginPlay ya están guardados (y los de ahora serían los de la tienda).
	if (DefaultSkelMeshMaterials.Num() > 0)
	{
		return;
	}
	if (USkeletalMeshComponent* SKM = GetMesh())
	{
		const int32 NumMats = SKM->GetNumMaterials();
		for (int32 i = 0; i < NumMats; ++i)
		{
			DefaultSkelMeshMaterials.Add(SKM->GetMaterial(i));
		}
		UE_LOG(LogTortunabo, Log, TEXT("[SKIN-DEBUG] '%s' cached %d default materials from SKM"),
			*GetName(), NumMats);
		for (int32 i = 0; i < DefaultSkelMeshMaterials.Num(); ++i)
		{
			UE_LOG(LogTortunabo, Verbose, TEXT("[SKIN-DEBUG]   slot %d: %s"),
				i,
				DefaultSkelMeshMaterials[i] ? *DefaultSkelMeshMaterials[i]->GetName() : TEXT("NULL"));
		}
	}
}

void ATortugaCharacter::StartCosmeticRetryTimer()
{
	// Restaurar cosméticos al (re)spawnar en el mapa.
	// En clientes, PlayerState puede llegar tarde → timer repetitivo que reintenta
	// cada 0.3s hasta éxito o 10 intentos (3s). Cubre pawns remotos cuyo
	// PlayerState no está disponible en el primer tick.
	CosmeticRetryCount = 0;
	GetWorldTimerManager().SetTimer(CosmeticRetryTimerHandle,
		[WeakThis = TWeakObjectPtr<ATortugaCharacter>(this)]()
		{
			if (!WeakThis.IsValid())
			{
				return;
			}
			if (WeakThis->ApplyCosmeticsFromPlayerState())
			{
				WeakThis->GetWorldTimerManager().ClearTimer(WeakThis->CosmeticRetryTimerHandle);
				return;
			}
			if (++WeakThis->CosmeticRetryCount >= 10)
			{
				UE_LOG(LogTortunabo, Warning, TEXT("[TortugaCharacter] '%s' — cosméticos: agotados 10 reintentos sin PlayerState."),
					*WeakThis->GetName());
				WeakThis->GetWorldTimerManager().ClearTimer(WeakThis->CosmeticRetryTimerHandle);
			}
		}, 0.3f, true, 0.1f);  // first fire at 0.1s, repeat every 0.3s
}

// ── SFX helpers ──────────────────────────────────────────────────────────────

void ATortugaCharacter::PlaySfxAtSelf(USoundBase* Sound) const
{
	if (!Sound || !GetWorld()) { return; }
	// Con la atenuación natural de los sonidos del derribo si el recurso no trae la suya (antes sonaba en 2D en todo el mapa).
	TNTurtleActionSfx::PlayAt(GetWorld(), Sound, GetActorLocation(), ReviveAudioInnerRadius, ReviveAudioOuterRadius);
}

void ATortugaCharacter::MulticastPlaySfx_Implementation(USoundBase* Sound)
{
	PlaySfxAtSelf(Sound);
}

void ATortugaCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// (Ragdoll: cada máquina simula físicas localmente con state idéntico.
	//  Sin replicate movement durante muerte — convergencia natural.)

	// ── Knockdown ground lock ─────────────────────────────────────────────
	// Si el personaje fue lanzado (PufferFish, banana) durante knockdown,
	// HandlePendingLaunch pone MOVE_Falling para que vuele por los aires.
	// Cuando aterriza, CMC pone MOVE_Walking. Re-desactivamos movimiento aquí
	// para que siga inmovilizado mientras dure el knockdown.
	//
	// IMPORTANTE: sólo lockeamos si la velocidad horizontal ya cayó por debajo
	// del umbral. Así un knockdown con deslizamiento (banana) sigue rodando por
	// el suelo hasta que la fricción del CMC lo frena de forma natural.
	// Sin este chequeo, DisableMovement se dispara en el primer tick de contacto
	// con el suelo y corta en seco el slide (bug Q4-03 residual).
	if (HasAuthority() && bIsKnockedDown)
	{
		if (UCharacterMovementComponent* MC = GetCharacterMovement())
		{
			if (MC->IsMovingOnGround() && MC->Velocity.Size2D() < KnockdownGroundLockSpeed)
			{
				MC->DisableMovement();
			}
		}
	}

	TickKnockdownRagdoll(DeltaTime); // ragdoll del derribo: sin atravesar el suelo y con la cámara siguiéndolo
	TickDive(DeltaTime);           // dive physics recovery + procedural animation
	TickJumpAnim(DeltaTime);       // jump procedural animation (suppressed during dive)
	TickEmote(DeltaTime);          // emote system (overrides leg anim when active)
	TickLegAnimation(DeltaTime);   // normal locomotion (suppressed during emotes/dive/jump)
	TickCameraInterp(DeltaTime);   // cinematic camera zoom/FOV interpolation
	TickVRView(DeltaTime);         // VR con gafas: el giro del mando sigue a la cabeza (TortugaCharacter_VR.cpp)
	TickFirstPersonView(DeltaTime); // primera persona (con o sin gafas): ojos en la cabeza y cuerpo sin cabeza (TortugaCharacter_FirstPerson.cpp)
	// La cabeza que sigue a la cámara va en UTN_TurtleAnimInstance (GetViewRelativeToBody y ReplicatedViewYaw).
	TickFallRules(DeltaTime);      // caída larga → caparazón (servidor)
	TickShellVisual(DeltaTime);    // encoger/estirar extremidades al entrar/salir del caparazón
	TickEyes(DeltaTime);           // parpadeo y ojos en espiral (cosmético)
}

void ATortugaCharacter::TickLegAnimation(float DeltaTime)
{
	// Suppressed while an emote (or its blend-out) controls all 5 components.
	if (ActiveEmoteIndex >= 0 || bEmoteBlendingOut)
	{
		return;
	}

	// Suppressed during knockdown — the character is tipped over, legs shouldn't animate.
	if (bIsKnockedDown)
	{
		return;
	}

	// Suppressed during dive and jump anim — those systems drive the leg components.
	if (bIsDiving || DiveTiltAlpha > 0.f || bJumpAnimActive)
	{
		return;
	}

	// Bail out early if neither leg bone is available.
	if (Pata1Bone == NAME_None && Pata2Bone == NAME_None)
	{
		return;
	}

	// GetVelocity() is replicated by CharacterMovement — works on every machine.
	const float Speed = GetVelocity().Size2D();

	const bool bIsSprinting = StaminaComponent && StaminaComponent->IsSprinting();

	const float TargetAmplitude = bIsSprinting ? LegSprintAmplitudeDeg : LegWalkAmplitudeDeg;
	const float TargetFrequency = bIsSprinting ? LegSprintFrequency    : LegWalkFrequency;

	// Fade the amplitude envelope smoothly when starting/stopping movement.
	const float TargetMult = (Speed > LegMinSpeed) ? 1.f : 0.f;
	LegAmplitudeMultiplier = FMath::FInterpTo(LegAmplitudeMultiplier, TargetMult, DeltaTime, 8.f);

	// Advance phase only while the character is moving (avoids phase pop on stop/resume).
	if (Speed > LegMinSpeed)
	{
		LegPhaseAccumulator += TargetFrequency * DeltaTime;
		LegPhaseAccumulator  = FMath::Fmod(LegPhaseAccumulator, 1.f); // keep in [0,1)
	}

	// Current pendulum angle.
	const float Angle = TargetAmplitude * LegAmplitudeMultiplier
	                  * FMath::Sin(LegPhaseAccumulator * 2.f * PI);

	// Pata1 and Pata2 are 180° out of phase → diagonal trot gait.
	if (Pata1Bone != NAME_None) { ApplyLegAngle(Pata1Bone, Pata1RestRot,  Angle); }
	if (Pata2Bone != NAME_None) { ApplyLegAngle(Pata2Bone, Pata2RestRot, -Angle); }

	// ── Arm swing — same phase accumulator, contralateral to legs ────────────
	// Arms hang down at ArmRestAngleDeg from T-pose (applied in parent space via
	// ArmSwingAxis / ApplyArmAngle). Brazo1 swings opposite to Pata1 for natural gait.
	const float ArmAmplitude = bIsSprinting ? ArmSprintAmplitudeDeg : ArmWalkAmplitudeDeg;
	const float ArmAngle     = ArmAmplitude * LegAmplitudeMultiplier
	                         * FMath::Sin(LegPhaseAccumulator * 2.f * PI);

	// Brazo1 (right): -ArmRestAngleDeg → arm falls DOWN (+AX = up, so -AX = down).
	// Brazo2 (left, mirrored): +ArmRestAngleDeg → arm falls DOWN (-AX = up for left arm).
	// Swing uses AZ axis: +ArmAngle naturally pushes right arm BACK and left arm FORWARD
	// because they sit on opposite sides of the body (+Y vs -Y in T-pose).
	if (Brazo1Bone != NAME_None) { ApplyArmAngle(Brazo1Bone, Brazo1RestRot, -ArmRestAngleDeg, ArmAngle); }
	if (Brazo2Bone != NAME_None) { ApplyArmAngle(Brazo2Bone, Brazo2RestRot,  ArmRestAngleDeg, -ArmAngle); }

	// ── Footsteps (cosmetic, local-only en cada máquina) ─────────────────────
	// Cada máquina tickea TickLegAnimation para todos los pawns; Velocity está
	// replicada. No hace falta RPC: cada cliente decide si el pawn X debe
	// sonar paso, y todos convergen al mismo ritmo (±1 frame).
	if (FootstepSound && !bIsDead)
	{
		const UCharacterMovementComponent* CMC = GetCharacterMovement();
		const bool bGrounded = CMC && !CMC->IsFalling();
		if (bGrounded && Speed > FootstepMinGroundSpeed)
		{
			FootstepCooldown -= DeltaTime;
			if (FootstepCooldown <= 0.f)
			{
				PlaySfxAtSelf(FootstepSound);
				FootstepCooldown = bIsSprinting ? FootstepSprintInterval : FootstepWalkInterval;
			}
		}
		else
		{
			FootstepCooldown = 0.f;
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// TickCameraInterp — Cinematic camera zoom & FOV interpolation (local only)
// ─────────────────────────────────────────────────────────────────────────────
void ATortugaCharacter::TickCameraInterp(float DeltaTime)
{
	// Solo aplica en el cliente local que controla este pawn.
	if (!IsLocallyControlled()) { return; }
	if (!CameraBoom || !FollowCamera) { return; }
	// En primera persona (VR o sin gafas) la cámara es otra (TortugaCharacter_VR.cpp, TortugaCharacter_FirstPerson.cpp).
	if (bVRViewActive || bFirstPersonActive)
	{
		CameraCarriedPull = 0.f;
		return;
	}

	const bool bSprinting = StaminaComponent && StaminaComponent->IsSprinting();

	// Llevada por el aire (la gaviota o el pelícano de la zona de gaviotas): la cámara se aleja y sube un poco para ver
	// adónde la llevan; al soltarla vuelve a su sitio sin saltos (Player/TN_CarriedCamera.h).
	CameraCarriedPull = TNCarriedCamera::StepPull(CameraCarriedPull, ATN_BeachEnemy::IsTurtleCarriedThroughAir(this), DeltaTime,
		CameraCarriedRiseSeconds, CameraCarriedReturnSeconds);
	const float CarriedK = TNCarriedCamera::Ease(CameraCarriedPull);

	// ── Interpolación de longitud del brazo ───────────────────────────────────
	const float TargetArmLength = (bSprinting ? CameraArmLengthSprint : CameraArmLengthDefault) + CarriedK * CameraCarriedExtraArm;
	CameraBoom->TargetArmLength = FMath::FInterpTo(
		CameraBoom->TargetArmLength,
		TargetArmLength,
		DeltaTime,
		CameraArmLengthInterpSpeed
	);

	// ── Interpolación de FOV ──────────────────────────────────────────────────
	const float TargetFOV = bSprinting ? CameraFOVSprint : CameraFOVDefault;
	FollowCamera->FieldOfView = FMath::FInterpTo(
		FollowCamera->FieldOfView,
		TargetFOV,
		DeltaTime,
		CameraFOVInterpSpeed
	);

	// ── Sync live lag speeds (por si se editan en runtime desde Blueprint) ────
	CameraBoom->CameraLagSpeed         = CameraPositionLagSpeed;
	CameraBoom->CameraRotationLagSpeed = CameraRotationLagSpeed;

	// ── CAM-01 floor clamp ────────────────────────────────────────────────────
	// Prueba ambos canales (ECC_Camera + ECC_Visibility) — si el suelo no bloquea
	// Camera en este proyecto, Visibility suele bloquear seguro.
	{
		const FVector CamWorld = FollowCamera->GetComponentLocation();
		const FVector TraceEnd = CamWorld - FVector(0.f, 0.f, 120.f);
		FHitResult HitCam, HitVis;
		FCollisionQueryParams QP(SCENE_QUERY_STAT(CamFloorClamp), /*bTraceComplex=*/false, this);
		const bool bHitCam = GetWorld() && GetWorld()->LineTraceSingleByChannel(
			HitCam, CamWorld, TraceEnd, ECC_Camera, QP);
		const bool bHitVis = GetWorld() && GetWorld()->LineTraceSingleByChannel(
			HitVis, CamWorld, TraceEnd, ECC_Visibility, QP);

		// Usa el hit más cercano entre ambos canales.
		float DistToFloor = TNumericLimits<float>::Max();
		if (bHitCam) { DistToFloor = FMath::Min(DistToFloor, CamWorld.Z - HitCam.ImpactPoint.Z); }
		if (bHitVis) { DistToFloor = FMath::Min(DistToFloor, CamWorld.Z - HitVis.ImpactPoint.Z); }

		float DesiredLiftZ = 0.f;
		constexpr float MinFloor = 40.f;
		if (DistToFloor < MinFloor)
		{
			DesiredLiftZ = (MinFloor - DistToFloor);
		}
		CameraFloorLiftCurrent = FMath::FInterpTo(CameraFloorLiftCurrent, DesiredLiftZ, DeltaTime, 10.f);

		// Temblor mientras la tortuga que llevamos forcejea (lo alimenta UTN_CarryComponent).
		FollowCamera->SetRelativeRotation(FRotator(CameraAimPitchOffset, 0.f, 0.f) + CarryShake);

		FVector RelLoc = CameraBoomRelativeOffset;
		RelLoc.Z += CameraFloorLiftCurrent + CarriedK * CameraCarriedLift;
		CameraBoom->SetRelativeLocation(RelLoc);

		// DIAGNOSTIC: log cada 1s con datos del trace (para depurar si clamp activa).
		static float LogAccumulator = 0.f;
		LogAccumulator += DeltaTime;
		if (LogAccumulator > 1.0f)
		{
			LogAccumulator = 0.f;
			UE_LOG(LogTortunabo, Verbose,
				TEXT("[Diagnostic] FloorClamp HitCam=%s HitVis=%s DistToFloor=%.1f Lift=%.1f"),
				bHitCam ? TEXT("Y") : TEXT("N"),
				bHitVis ? TEXT("Y") : TEXT("N"),
				DistToFloor > 1e8f ? -1.f : DistToFloor,
				CameraFloorLiftCurrent);
		}
	}
}

void ATortugaCharacter::ApplyLegAngle(FName BoneName, const FRotator& RestRot, float AngleDeg) const
{
	const FQuat SwingQuat(LegSwingAxis.GetSafeNormal(), FMath::DegreesToRadians(AngleDeg));
	SetAnimBoneRot(BoneName, (FQuat(RestRot) * SwingQuat).Rotator());
}

void ATortugaCharacter::ApplyArmAngle(FName BoneName, const FRotator& RestRot,
                                       float RestOffsetDeg, float SwingDeg) const
{
	const FVector RestAxis  = ArmSwingAxis.GetSafeNormal();
	const FVector SwingAxis = FVector(1.f, 0.f, 0.f);
	const FQuat   OffsetQuat(RestAxis,  FMath::DegreesToRadians(RestOffsetDeg));
	const FQuat   SwingQuat (SwingAxis, FMath::DegreesToRadians(SwingDeg));
	SetAnimBoneRot(BoneName, (SwingQuat * OffsetQuat * FQuat(RestRot)).Rotator());
}

USceneComponent* ATortugaCharacter::FindChildByName(FName Name) const
{
	// 1. Exact FName match (fastest)
	for (UActorComponent* Comp : GetComponents())
	{
		if (Comp && Comp->GetFName() == Name)
		{
			return Cast<USceneComponent>(Comp);
		}
	}

	// 2. Fallback: case-insensitive substring match on the component name.
	//    Catches "Cabeza_0", "cabeza", "SM_Cabeza", etc.
	const FString NameStr = Name.ToString();
	for (UActorComponent* Comp : GetComponents())
	{
		if (Comp)
		{
			const FString CompName = Comp->GetName();
			if (CompName.Contains(NameStr, ESearchCase::IgnoreCase))
			{
				UE_LOG(LogTortunabo, Warning, TEXT("[TortugaCharacter] '%s' not found as exact name — matched '%s' (fuzzy). Rename to '%s' in BP for best results."),
					*NameStr, *CompName, *NameStr);
				return Cast<USceneComponent>(Comp);
			}
		}
	}

	return nullptr;
}

void ATortugaCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Log diagnóstico para investigar despawn intermitente del ragdoll-pickup.
	// Si veo "EndPlay reason=Destroyed bIsDead=true" con el ragdoll aún actuando
	// de visual del rescue pickup → algo destruye el pawn indebidamente.
	// Casos legítimos: Logout del PC propietario, travel, quit. Casos buggy:
	// alguien llama Destroy() directo sin checks.
	UE_LOG(LogTortunabo, Log,
		TEXT("[DEATH-DEBUG] %s EndPlay reason=%d bIsDead=%d bIsKnocked=%d HasOwner=%d"),
		*GetName(), (int32)EndPlayReason, bIsDead ? 1 : 0, bIsKnockedDown ? 1 : 0,
		GetOwner() != nullptr);

	FocusedInteractable = nullptr;
	ActiveEmoteIndex   = -1;
	bEmoteBlendingOut  = false;
	bIsDiving          = false;
	DiveLockTimer      = 0.f;
	DiveTiltAlpha      = 0.f;
	bJumpAnimActive    = false;
	JumpAnimTime       = 0.f;

	GetWorldTimerManager().ClearTimer(CosmeticRetryTimerHandle);
	StopEmoteSound();
	StopReviveChannelSound();
	StopDBNOHeartbeatSound();

	GetWorldTimerManager().ClearTimer(InteractionScanTimerHandle);
	GetWorldTimerManager().ClearTimer(KnockdownTimerHandle);
	GetWorldTimerManager().ClearTimer(ReviveChannelTimerHandle);
	GetWorldTimerManager().ClearTimer(RagdollFreezeTimerHandle);
	Super::EndPlay(EndPlayReason);
}

void ATortugaCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	CacheInputAssets();
	ApplyInputMappingIfLocal();

	// ── Re-aplicar casco tras seamless travel ────────────────────────────────
	// Después del viaje, el pawn es nuevo. OnRep_EquippedHelmetId no se dispara
	// (el valor no cambió) y OnRep_PlayerState puede llegar antes de que la ref
	// al pawn sea válida en el PlayerState. PawnClientRestart es el hook seguro:
	// en este punto el PlayerController y PlayerState ya están disponibles en el cliente.
	ApplyCosmeticsFromPlayerState();

	// ── Restaurar input mode y foco del viewport ─────────────────────────────
	// ApplyGameplayInputMode() se llama en BeginPlay del PC, pero en ese momento
	// el viewport puede no estar completamente listo (especialmente en builds
	// empaquetados). PawnClientRestart se dispara DESPUÉS de que la posesión es
	// confirmada en el cliente, garantizando que el foco se aplica correctamente.
	// ForceRestoreInput = ResetIgnoreInputFlags + ApplyGameplayInputMode.
	if (AMP_GamePlayerController* PC = Cast<AMP_GamePlayerController>(GetController()))
	{
		PC->ForceRestoreInput();

		// ── Re-añadir HUD widgets al viewport (cliente tras seamless travel) ─
		// OnPossess solo se ejecuta en el SERVIDOR. En el cliente, PawnClientRestart
		// es el hook equivalente (disparado por ClientRestart RPC).
		// Los widgets del PC persisten entre mapas pero son eliminados del viewport
		// por UWorld::CleanupWorld durante la transición. Aquí los volvemos a añadir.
		PC->RefreshHUDAfterPossession();
	}

	// ── Scan timer de interacción ─────────────────────────────────────────────
	// BeginPlay no puede arrancar el timer porque IsLocallyControlled() es false
	// antes de que el PC posea el pawn. PawnClientRestart se dispara DESPUÉS de
	// la posesión (vía ClientRestart RPC), cuando IsLocallyControlled() ya es true.
	// Cubre tanto la posesión inicial como la re-posesión tras seamless travel.
	if (IsLocallyControlled() && InteractionScanInterval > 0.f
		&& !GetWorldTimerManager().IsTimerActive(InteractionScanTimerHandle))
	{
		GetWorldTimerManager().SetTimer(InteractionScanTimerHandle, this,
			&ATortugaCharacter::UpdateFocusedInteractable, InteractionScanInterval, true);
		UE_LOG(LogTortunabo, Log, TEXT("[TortugaCharacter] Interaction scan timer started in PawnClientRestart (interval=%.2fs)"),
			InteractionScanInterval);
	}
}

void ATortugaCharacter::CacheInputAssets()
{
	if (bInputAssetsLoaded)
	{
		return;
	}

	LoadedMappingContext = DefaultMappingContext.LoadSynchronous();
	LoadedMoveAction = MoveAction.LoadSynchronous();
	LoadedLookAction = LookAction.LoadSynchronous();
	LoadedJumpAction = JumpAction.LoadSynchronous();
	LoadedInteractAction = InteractAction.LoadSynchronous();
	LoadedRotateInventoryAction = RotateInventoryAction.LoadSynchronous();
	LoadedSprintAction = SprintAction.LoadSynchronous();
	LoadedShellAction = ShellAction.LoadSynchronous();
	LoadedDropItemAction = DropItemAction.LoadSynchronous();

	// Load emote actions (tamaño dinámico — configurado en el BP)
	LoadedEmoteActions.SetNum(EmoteActions.Num());
	for (int32 i = 0; i < EmoteActions.Num(); i++)
	{
		LoadedEmoteActions[i] = EmoteActions[i].LoadSynchronous();
	}

	bInputAssetsLoaded = true;

	// ── Log de cada asset para diagnosticar qué falta ─────────────────────────
	auto LogAsset = [](const TCHAR* Name, const UObject* Asset)
	{
		if (Asset)
		{
			UE_LOG(LogTortunabo, Log, TEXT("[Input] ✓ %s loaded: %s"), Name, *Asset->GetPathName());
		}
		else
		{
			UE_LOG(LogTortunabo, Error, TEXT("[Input] ✗ %s FAILED TO LOAD — create this asset in /Game/Blueprints/Gameplay/Controls/"), Name);
		}
	};

	LogAsset(TEXT("IMC_Player"), LoadedMappingContext);
	LogAsset(TEXT("IA_Move"), LoadedMoveAction);
	LogAsset(TEXT("IA_Look"), LoadedLookAction);
	LogAsset(TEXT("IA_Jump"), LoadedJumpAction);
	LogAsset(TEXT("IA_Interact"), LoadedInteractAction);
	LogAsset(TEXT("IA_RotateInventory"), LoadedRotateInventoryAction);
	LogAsset(TEXT("IA_Sprint"),           LoadedSprintAction);
	LogAsset(TEXT("IA_Shell"),            LoadedShellAction);
	LogAsset(TEXT("IA_DropItem"),         LoadedDropItemAction);

	for (int32 i = 0; i < LoadedEmoteActions.Num(); i++)
	{
		const FString EmoteName = FString::Printf(TEXT("IA_Emote%d"), i);
		LogAsset(*EmoteName, LoadedEmoteActions[i]);
	}
}

void ATortugaCharacter::ApplyInputMappingIfLocal()
{
	if (!LoadedMappingContext)
	{
		return;
	}

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				InputSubsystem->ClearAllMappings();
				InputSubsystem->AddMappingContext(LoadedMappingContext, 0);
			}
		}
	}
}

void ATortugaCharacter::ReapplyInputMapping()
{
	CacheInputAssets();
	ApplyInputMappingIfLocal();
}

void ATortugaCharacter::RemoveBigHeadEffect()
{
	if (!bBigHead) { return; }

	GetWorldTimerManager().ClearTimer(BigHeadTimerHandle);
	bBigHead = false;
	ApplyBigHeadVisual(false);

	// Disparar efecto de mareo en todas las máquinas (#2).
	if (HasAuthority() && MareoDurationSeconds > 0.f)
	{
		MulticastApplyMareoEffect(MareoDurationSeconds);
	}
}

void ATortugaCharacter::MulticastApplyMareoEffect_Implementation(float Duration)
{
	// ── Reducir velocidad durante la duración del mareo ───────────────────────
	if (MareoSpeedCap > 0.f)
	{
		if (UTN_StaminaComponent* SC = FindComponentByClass<UTN_StaminaComponent>())
		{
			SC->SetSpeedCap(TNMovementLimits::MareoSource(), MareoSpeedCap);

			FTimerDelegate Del = FTimerDelegate::CreateUObject(this, &ATortugaCharacter::ClearMareoSpeedCap);
			GetWorldTimerManager().SetTimer(MareoTimerHandle, Del, Duration, false);
		}
	}

	// ── Feedback local (camera shake, VFX, audio) — solo cliente local ───────
	if (IsLocallyControlled())
	{
		OnMareoEffect(Duration);
	}
}

void ATortugaCharacter::ClearMareoSpeedCap()
{
	// Solo el tope del mareo: el de llevar a otra, el del caparazón o el de una zona lenta siguen.
	if (UTN_StaminaComponent* SC = FindComponentByClass<UTN_StaminaComponent>())
	{
		SC->ClearSpeedCap(TNMovementLimits::MareoSource());
	}
}

// ── Tinta de calamar (#13) ─────────────────────────────────────────────────────

void ATortugaCharacter::ApplyInkEffect(float Duration)
{
	if (!IsLocallyControlled() || !InkOverlayMaterial || !InkPostProcess) { return; }

	// Registrar el material en el PostProcess local y activarlo.
	// AddOrUpdateBlendable garantiza que no se acumulan entradas duplicadas
	// si ApplyInkEffect se llama varias veces antes de que expire el timer.
	InkPostProcess->AddOrUpdateBlendable(InkOverlayMaterial, 1.f);
	InkPostProcess->bEnabled = true;

	GetWorldTimerManager().ClearTimer(InkEffectTimerHandle);
	FTimerDelegate Del = FTimerDelegate::CreateUObject(this, &ATortugaCharacter::ClearInkEffect);
	GetWorldTimerManager().SetTimer(InkEffectTimerHandle, Del, Duration, false);
}

void ATortugaCharacter::ClearInkEffect()
{
	if (InkPostProcess) { InkPostProcess->bEnabled = false; }
}

void ATortugaCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UE_LOG(LogTortunabo, Log, TEXT("[Input] SetupPlayerInputComponent called on '%s' (LocallyControlled=%s)"),
		*GetName(), IsLocallyControlled() ? TEXT("YES") : TEXT("NO"));

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		CacheInputAssets();
		if (LoadedMoveAction)
		{
			EnhancedInput->BindAction(LoadedMoveAction, ETriggerEvent::Triggered, this, &ATortugaCharacter::Move);
			EnhancedInput->BindAction(LoadedMoveAction, ETriggerEvent::Completed, this, &ATortugaCharacter::OnMoveReleased);
			EnhancedInput->BindAction(LoadedMoveAction, ETriggerEvent::Canceled, this, &ATortugaCharacter::OnMoveReleased);
		}
		if (LoadedLookAction)
		{
			EnhancedInput->BindAction(LoadedLookAction, ETriggerEvent::Triggered, this, &ATortugaCharacter::Look);
		}
		if (LoadedJumpAction)
		{
			EnhancedInput->BindAction(LoadedJumpAction, ETriggerEvent::Started, this, &ATortugaCharacter::Jump);
			EnhancedInput->BindAction(LoadedJumpAction, ETriggerEvent::Completed, this, &ATortugaCharacter::StopJumping);
		}
		if (LoadedInteractAction)
		{
			EnhancedInput->BindAction(LoadedInteractAction, ETriggerEvent::Started, this, &ATortugaCharacter::TryInteract);
			// Soltar la tecla corta las interacciones de mantener (rebuscar). IA_Interact usa el disparador implícito
			// (pulsada mientras se mantiene): con uno de tipo Pressed, Completed llegaría al instante y no se podría mantener.
			EnhancedInput->BindAction(LoadedInteractAction, ETriggerEvent::Completed, this, &ATortugaCharacter::ReleaseInteract);
			EnhancedInput->BindAction(LoadedInteractAction, ETriggerEvent::Canceled, this, &ATortugaCharacter::ReleaseInteract);
			UE_LOG(LogTortunabo, Log, TEXT("[Input] ✓ IA_Interact bound to TryInteract"));
		}
		else
		{
			UE_LOG(LogTortunabo, Error, TEXT("[Input] ✗ IA_Interact NOT bound — asset is null! Create /Game/Blueprints/Gameplay/Controls/IA_Interact"));
		}
		if (LoadedRotateInventoryAction)
		{
			EnhancedInput->BindAction(LoadedRotateInventoryAction, ETriggerEvent::Started, this, &ATortugaCharacter::RotateInventory);
		}
		if (LoadedSprintAction)
		{
			EnhancedInput->BindAction(LoadedSprintAction, ETriggerEvent::Started, this, &ATortugaCharacter::StartSprint);
			EnhancedInput->BindAction(LoadedSprintAction, ETriggerEvent::Completed, this, &ATortugaCharacter::StopSprint);
			EnhancedInput->BindAction(LoadedSprintAction, ETriggerEvent::Canceled, this, &ATortugaCharacter::StopSprint);
		}

		if (LoadedShellAction)
		{
			// Started: es un toggle de pulsacion puntual, no un hold como el sprint.
			EnhancedInput->BindAction(LoadedShellAction, ETriggerEvent::Started, this, &ATortugaCharacter::ToggleShell);
		}
		if (LoadedDropItemAction)
		{
			EnhancedInput->BindAction(LoadedDropItemAction, ETriggerEvent::Started, this, &ATortugaCharacter::DropEquippedItem);
		}
		// ── Emotes 0–9 ────────────────────────────────────────────────────────
		static void (ATortugaCharacter::* const EmoteHandlers[10])() =
		{
			&ATortugaCharacter::OnEmote0, &ATortugaCharacter::OnEmote1,
			&ATortugaCharacter::OnEmote2, &ATortugaCharacter::OnEmote3,
			&ATortugaCharacter::OnEmote4, &ATortugaCharacter::OnEmote5,
			&ATortugaCharacter::OnEmote6, &ATortugaCharacter::OnEmote7,
			&ATortugaCharacter::OnEmote8, &ATortugaCharacter::OnEmote9,
		};
		for (int32 i = 0; i < LoadedEmoteActions.Num() && i < 10; i++)
		{
			if (LoadedEmoteActions[i])
			{
				EnhancedInput->BindAction(LoadedEmoteActions[i], ETriggerEvent::Started, this, EmoteHandlers[i]);
				UE_LOG(LogTortunabo, Log, TEXT("[Input] ✓ IA_Emote%d bound"), i);
			}
		}
	}
	else
	{
		UE_LOG(LogTortunabo, Error, TEXT("[Input] ✗ PlayerInputComponent is NOT an EnhancedInputComponent! Check DefaultInput.ini uses EnhancedPlayerInput."));
	}
}

void ATortugaCharacter::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();

	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		JumpStartHorizontalVelocity = CMC->Velocity;
		JumpStartHorizontalVelocity.Z = 0.f;
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Jump] %s jumped · captured horizontal velocity=%s (speed=%.0f)"),
		*GetName(), *JumpStartHorizontalVelocity.ToString(), JumpStartHorizontalVelocity.Size());

	if (HasAuthority() && JumpSound)
	{
		MulticastPlaySfx(JumpSound);
	}
}

void ATortugaCharacter::Jump()
{
	// Atrapada por una criatura de la playa (arenas movedizas, pinza, arrastre): el salto es forcejeo (#684-#686).
	if (UTN_BeachTrapStatusComponent* TrapStatus = UTN_BeachTrapStatusComponent::FindOn(this); TrapStatus && TrapStatus->IsEscapeArmed())
	{
		TrapStatus->PressEscape();
		return;
	}
	if (bIsKnockedDown || bIsDead || IsInShell()) { return; }
	// Levantándose del derribo: la animación termina antes de volver a saltar.
	if (GetWorld() && GetWorld()->GetTimeSeconds() < GetUpLockUntil) { return; }
	if (CarryComponent && CarryComponent->IsBeingCarried()) { return; }

	// Nadando: salto desde el agua para salir a orillas e isletas.
	if (GetCharacterMovement()->IsSwimming())
	{
		if (CanSwimHop())
		{
			PerformSwimHop();
			if (!HasAuthority())
			{
				ServerSwimHop();
			}
		}
		return;
	}

	// Segundo press de salto en el aire → dive (igual que Fall Guys)
	if (GetCharacterMovement()->IsFalling())
	{
		TryDive();
		return;
	}

	// Sobre la tripa tras el panzazo: casi parada, el salto es un brinco que la levanta (lo decide el movimiento, que lo
	// predice igual que el servidor); deprisa no hace nada. Ya levantada, mientras llega el fin del panzazo, salta normal.
	if (bIsDiving)
	{
		const UTN_TurtleMovementComponent* TurtleMove = GetTurtleMovement();
		if (TurtleMove && TurtleMove->AcceptsInputDuringDive(DiveSerial) && CanJump())
		{
			bJumpAnimActive = true;
			JumpAnimTime    = 0.f;
			Super::Jump();
		}
		return;
	}

	// Grounded y no en dive → salto normal + trigger jump procedural animation
	if (!bIsDiving)
	{
		if (CanJump())
		{
			bJumpAnimActive = true;
			JumpAnimTime    = 0.f;
		}
		Super::Jump();
	}
}

void ATortugaCharacter::PerformAirDashLocally()
{
	bCanAirDash = false;
	const FVector DashVelocity = GetActorForwardVector() * AirDashHorizontalForce
	                           + FVector::UpVector * AirDashVerticalBoost;
	LaunchCharacter(DashVelocity, true, true);
}

bool ATortugaCharacter::CanSwimHop() const
{
	const UCharacterMovementComponent* CMC = GetCharacterMovement();
	return CMC && CMC->IsSwimming() && !bIsKnockedDown && !bIsDead && !IsInShell()
		&& GetWorld() && GetWorld()->GetTimeSeconds() - LastSwimHopTime >= 0.6f;
}

void ATortugaCharacter::PerformSwimHop()
{
	LastSwimHopTime = GetWorld()->GetTimeSeconds();
	const FVector Forward = FVector(GetActorForwardVector().X, GetActorForwardVector().Y, 0.f).GetSafeNormal();
	LaunchCharacter(Forward * SwimHopForward + FVector::UpVector * SwimHopVelocity, true, true);
}

void ATortugaCharacter::ServerSwimHop_Implementation()
{
	if (CanSwimHop())
	{
		PerformSwimHop();
	}
}

void ATortugaCharacter::ServerPerformAirDash_Implementation()
{
	if (!GetCharacterMovement()->IsFalling() || !bCanAirDash || bIsKnockedDown || bIsDead)
	{
		return;
	}
	bCanAirDash = false;
	const FVector DashVelocity = GetActorForwardVector() * AirDashHorizontalForce
	                           + FVector::UpVector * AirDashVerticalBoost;
	LaunchCharacter(DashVelocity, true, true);
}

void ATortugaCharacter::Move(const FInputActionValue& Value)
{
	// Llevada por otra tortuga: moverse es forcejear (2 s seguidos → se libera).
	if (CarryComponent && CarryComponent->IsBeingCarried())
	{
		CarryComponent->SetStruggleInput(Value.Get<FVector2D>().Size() > 0.3f);
		return;
	}

	// Durante el panzazo no se dirige: ni en el aire ni arrastrándose deprisa. Casi parada, moverse la levanta; reptando
	// (sin sitio para ponerse de pie) o ya levantándose, se mueve (UTN_TurtleMovementComponent lo decide, predicho).
	if (bIsDiving)
	{
		const UTN_TurtleMovementComponent* TurtleMove = GetTurtleMovement();
		if (!TurtleMove || !TurtleMove->AcceptsInputDuringDive(DiveSerial)) { return; }
	}
	// Movement is locked during knockdown — momentum from LaunchCharacter takes over
	if (bIsKnockedDown) { return; }
	// Levantándose del derribo (unos 0,75 s): el cuerpo gira del suelo a de pie sin deslizarse.
	if (GetWorld() && GetWorld()->GetTimeSeconds() < GetUpLockUntil) { return; }

	// Cancel any active emote the moment the player moves —
	// EXCEPT emotes 5 (Baile Irlandés) and 6 (Superman) which are walkable.
	// (El knockdown emote ya está cubierto por el guard bIsKnockedDown de arriba.)
	if (ActiveEmoteIndex >= 0 && ActiveEmoteIndex != 5 && ActiveEmoteIndex != 6) { CancelEmote(); }

	const FVector2D MovementVector = Value.Get<FVector2D>();
	if (Controller)
	{
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0.f, Rotation.Yaw, 0.f);

		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		AddMovementInput(ForwardDirection, MovementVector.Y);
		AddMovementInput(RightDirection, MovementVector.X);
	}

	LastMovementInput = MovementVector;
	RefreshSprintRequest();
}

void ATortugaCharacter::OnMoveReleased()
{
	if (CarryComponent)
	{
		CarryComponent->SetStruggleInput(false);
	}
	LastMovementInput = FVector2D::ZeroVector;
	RefreshSprintRequest();
}

void ATortugaCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D LookAxisVector = Value.Get<FVector2D>();
	if (Controller)
	{
		// Sensibilidad independiente por eje.
		const float Yaw   =  LookAxisVector.X * LookSensitivityX;
		// bInvertCameraY: true → invertir eje vertical (arriba/abajo del ratón).
		const float Pitch = LookAxisVector.Y * LookSensitivityY * (bInvertCameraY ? -1.f : 1.f);

		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void ATortugaCharacter::TryInteract()
{
	if (bIsKnockedDown || IsInShell()) { return; }

	// Llevando a alguien: interactuar = lanzarlo hacia donde mira la cámara.
	if (CarryComponent && CarryComponent->IsCarrying())
	{
		CarryComponent->RequestThrow();
		return;
	}

	const bool bDebug = CVarDebugInteraction.GetValueOnGameThread() != 0;

	if (bDebug)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Interact:DEBUG] === E PRESSED === FocusedInteractable: %s"),
			FocusedInteractable.IsValid() ? *FocusedInteractable->GetName() : TEXT("(none)"));
	}

	// Si no hay foco, intentar un scan inmediato
	if (!FocusedInteractable.IsValid())
	{
		UpdateFocusedInteractable();
	}

	// Si tras el scan sigue sin haber interactuable → coger a una tortuga en caparazón
	// o aturdida si hay una delante; si no, usar ítem equipado (lanzar bola, etc.)
	if (!FocusedInteractable.IsValid())
	{
		if (CarryComponent && CarryComponent->TryGrabNearest())
		{
			return;
		}

		if (bDebug)
		{
			UE_LOG(LogTortunabo, Log, TEXT("[Interact:DEBUG] No interactable in focus → TryUseEquippedItem"));
		}
		TryUseEquippedItem();
		return;
	}

	if (bDebug)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Interact:DEBUG] Sending ServerTryInteract → %s (CanInteract client-side: %s)"),
			*FocusedInteractable->GetName(),
			FocusedInteractable->CanInteract(this) ? TEXT("YES") : TEXT("NO"));
	}

	// Interacción de mantener (rebuscar un decorado): el servidor cuenta el tiempo mientras siga pulsada la tecla;
	// ReleaseInteract avisa al soltarla.
	if (FocusedInteractable->GetHoldDuration() > 0.f)
	{
		HoldInteractable = FocusedInteractable;
		ServerBeginHoldInteract(FocusedInteractable.Get());
		return;
	}

	// Si el servidor acaba usando el objeto de la mano (recoger con la mano llena), en VR va hacia la aleta.
	SendVRAimToServer();
	ServerTryInteract(FocusedInteractable.Get());
}

void ATortugaCharacter::RotateInventory()
{
	if (InventoryComponent)
	{
		InventoryComponent->RotateItems();
	}
}

bool ATortugaCharacter::IsInShell() const
{
	return ShellComponent && ShellComponent->IsInShell();
}

void ATortugaCharacter::ToggleShell()
{
	if (bIsKnockedDown || bIsDead) { return; }
	// Llevando o llevada: el caparazón lo gobierna el sistema de carga.
	if (CarryComponent && (CarryComponent->IsCarrying() || CarryComponent->IsBeingCarried())) { return; }

	if (ShellComponent)
	{
		// El componente decide: valida condiciones con autoridad y replica el estado.
		ShellComponent->RequestToggleShell();
	}
}

void ATortugaCharacter::OnShellStateChanged(bool bInShell)
{
	if (!bInShell)
	{
		return;
	}

	// Un emote en curso dentro del caparazon se veria como la tortuga bailando
	// metida en su propio cascaron. CancelEmote es local: ApplyShellState corre
	// en todas las maquinas, asi que cada una cancela el suyo.
	if (ActiveEmoteIndex >= 0 || bEmoteBlendingOut)
	{
		CancelEmote();
	}
}

void ATortugaCharacter::TickEyes(float DeltaTime)
{
	if (GetNetMode() == NM_DedicatedServer) { return; }

	// Parpadeo de dibujo: el párpado baja y sube en 0,16 s cada 2,5-5,5 s y, a veces, dos seguidos.
	EyeBlinkTimer -= DeltaTime;
	if (EyeBlinkTimer <= 0.f)
	{
		bEyeBlinking = true;
		EyeBlinkClock = 0.f;
		EyeBlinkTimer = FMath::FRand() < 0.2f ? 0.32f : FMath::FRandRange(2.5f, 5.5f);
	}
	float Blink = 0.f;
	if (bEyeBlinking)
	{
		EyeBlinkClock += DeltaTime;
		const float K = EyeBlinkClock / 0.16f;
		Blink = K < 0.5f ? K * 2.f : FMath::Max(0.f, 2.f - K * 2.f);
		bEyeBlinking = K < 1.f;
	}
	// Noqueada o muerta: ojos en espiral (y sin parpadear).
	const float Dizzy = (bIsKnockedDown || bIsDead) ? 1.f : 0.f;
	if (Dizzy > 0.f) { Blink = 0.f; }
	if (FMath::Abs(Blink - EyeBlinkApplied) > 0.02f || Dizzy != EyeDizzyApplied)
	{
		EyeBlinkApplied = Blink;
		EyeDizzyApplied = Dizzy;
		UTN_CosmeticLook::SetEyeState(GetMesh(), Blink, Dizzy);
	}
}

void ATortugaCharacter::PlaceOnShellBody(const FTransform& BoxWorld)
{
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	const double HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 70.0;
	// La cápsula (que ya solo solapa) de pie sobre la cara de abajo de la caja: con el caparazón tumbado en el suelo
	// queda donde estaría la tortuga de pie, y la cámara sigue al caparazón.
	const FVector CapsuleLoc = BoxWorld.GetLocation() + FVector(0.0, 0.0, HalfHeight - ATN_ShellBody::BoxHalfExtent().Z);
	SetActorLocation(CapsuleLoc, false, nullptr, ETeleportType::TeleportPhysics);
	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		SkelMesh->SetWorldTransform(ATN_ShellBody::MeshWorldTransform(BoxWorld, DiveMeshDefaultScale * GetActorScale3D()),
			false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void ATortugaCharacter::ResetMeshTransform()
{
	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		SkelMesh->SetRelativeLocationAndRotation(DiveMeshDefaultLoc, DiveMeshDefaultRot, false, nullptr, ETeleportType::TeleportPhysics);
		SkelMesh->SetRelativeScale3D(DiveMeshDefaultScale);
	}
}

void ATortugaCharacter::StartSprint()
{
	if (bIsDiving || IsInShell()) { return; }
	bSprintHeld = true;
	RefreshSprintRequest();
}

void ATortugaCharacter::StopSprint()
{
	bSprintHeld = false;
	RefreshSprintRequest();
}

void ATortugaCharacter::DropEquippedItem()
{
	// Llevando a alguien: soltar = dejarlo delante sin lanzarlo.
	if (CarryComponent && CarryComponent->IsCarrying())
	{
		CarryComponent->RequestDrop();
		return;
	}
	ServerDropEquippedItem();
}

void ATortugaCharacter::TryUseEquippedItem()
{
	// En VR se lanza hacia donde apunta la aleta: el servidor la recibe antes que la acción (los dos son fiables).
	SendVRAimToServer();
	ServerUseEquippedItem();
}

void ATortugaCharacter::RefreshSprintRequest()
{
	if (!StaminaComponent)
	{
		return;
	}

	// Sprint funciona en cualquier dirección de movimiento (delante, lateral, diagonal).
	// Solo se desactiva cuando el jugador solta el stick/WASD por completo.
	static constexpr float MovementInputDeadzone = 0.25f;
	const bool bHasMovementInput = LastMovementInput.SizeSquared() > (MovementInputDeadzone * MovementInputDeadzone);
	// En el caparazón no se esprinta aunque la tecla siga pulsada: el input de movimiento llega igual y, sin esto, la
	// petición de sprint volvía a activarse y gastaba estamina con la tortuga metida dentro.
	const bool bWantsToSprint = bSprintHeld && bHasMovementInput && !IsInShell() && !bIsKnockedDown && !bIsDead;
	StaminaComponent->SetSprintRequested(bWantsToSprint);
	// La velocidad la decide el movimiento con la petición que lleva cada movimiento; al servidor llega con ellos (#250).
	if (UTN_TurtleMovementComponent* TurtleMove = GetTurtleMovement())
	{
		TurtleMove->SetWantsToSprint(bWantsToSprint);
	}
}

void ATortugaCharacter::GrantInfiniteStamina(float DurationSeconds)
{
	if (!StaminaComponent)
	{
		return;
	}

	StaminaComponent->GrantUnlimitedStamina(DurationSeconds);
}

void ATortugaCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	// Re-apply helmet when PlayerState is replicated late (after BeginPlay timer already fired).
	// This covers the race condition where PlayerState arrives long after pawn possession.
	ApplyCosmeticsFromPlayerState();
}

void ATortugaCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	bCanAirDash = true;

	// Caída larga fuera de géiser/tobogán: la tortuga se rompe.
	const float Drop = bTrackingFall ? FallApexZ - GetActorLocation().Z : 0.f;
	const bool bImmune = bFallImmune;
	bTrackingFall = false;
	bFallImmune = false;
	bAutoShelledThisFall = false;
	if (HasAuthority() && !bImmune && !bIsDead && Drop > FatalFallHeight)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Fall] %s cayó %.0f cm → muere"), *GetName(), Drop);
		RequestKill(this);
		return;
	}

	// Lanzada por otra tortuga: rebote vertical y se estira en el aire.
	if (CarryComponent)
	{
		CarryComponent->NotifyLanded();
	}
}

void ATortugaCharacter::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);

	const UCharacterMovementComponent* CMC = GetCharacterMovement();
	if (!CMC)
	{
		return;
	}
	switch (CMC->MovementMode)
	{
	case MOVE_Falling:
		bTrackingFall = true;
		bAutoShelledThisFall = false;
		FallApexZ = GetActorLocation().Z;
		break;
	case MOVE_Swimming:
		// El agua amortigua cualquier caída y termina los vuelos de lanzamiento.
		bTrackingFall = false;
		bFallImmune = false;
		if (CarryComponent)
		{
			CarryComponent->NotifyEnteredWater();
		}
		break;
	case MOVE_None:
		bTrackingFall = false;
		break;
	default:
		break;
	}
}

void ATortugaCharacter::TickFallRules(float /*DeltaTime*/)
{
	if (!bTrackingFall)
	{
		return;
	}
	const float Z = GetActorLocation().Z;
	FallApexZ = FMath::Max(FallApexZ, Z);

	if (!HasAuthority() || bFallImmune || bAutoShelledThisFall || bIsDead || bIsKnockedDown || !ShellComponent)
	{
		return;
	}
	if (CarryComponent && (CarryComponent->IsCarrying() || CarryComponent->IsBeingCarried()))
	{
		return;
	}
	if (FallApexZ - Z > AutoShellFallHeight)
	{
		// Sobre un trampolín, sin bola: rebota como tortuga en el mismo paso aquí y en el cliente dueño (#21).
		if (TNTrampolineRules::HoldsAutoShell(ATN_BeachTrampoline::DropOntoTrampoline(*this, TNTrampolineRules::AutoShellLookDown)))
		{
			return;
		}
		bAutoShelledThisFall = true;
		if (!IsInShell())
		{
			// Se hace bola y cae con física: rebota, rueda y, al pararse, sale sola.
			ShellComponent->ForceEnterShell(true, true);
		}
	}
}

void ATortugaCharacter::TickShellVisual(float DeltaTime)
{
	// Cosmético y local: cada máquina encoge cabeza, patas, brazos y cola a partir
	// del estado replicado del caparazón. Al salir se estiran más despacio, que es
	// lo que se ve durante el rebote tras un lanzamiento.
	const bool bIn = IsInShell() && !bIsDead;
	const float Target = bIn ? 1.f : 0.f;
	const float Seconds = bIn ? ShellRetractSeconds : ShellExtendSeconds;
	ShellVisualAlpha = FMath::FInterpConstantTo(ShellVisualAlpha, Target, DeltaTime, 1.f / FMath::Max(0.01f, Seconds));

	if (ShellVisualAlpha <= KINDA_SMALL_NUMBER && !bShellVisualApplied)
	{
		return;
	}
	bShellVisualApplied = ShellVisualAlpha > KINDA_SMALL_NUMBER;

	const float Ease = ShellVisualAlpha * ShellVisualAlpha * (3.f - 2.f * ShellVisualAlpha);
	const float LimbScale = FMath::Lerp(1.f, 0.05f, Ease);
	const FVector Limb(bShellVisualApplied ? LimbScale : 1.f);
	SetAnimBoneScale(Pata1Bone, Limb);
	SetAnimBoneScale(Pata2Bone, Limb);
	SetAnimBoneScale(Brazo1Bone, Limb);
	SetAnimBoneScale(Brazo2Bone, Limb);
	SetAnimBoneScale(ColaBone, Limb);
	const float HeadBase = bBigHead ? BigHeadScale : 1.f;
	SetAnimBoneScale(CabezaBone, FVector(HeadBase * (bShellVisualApplied ? LimbScale : 1.f)));
}

// ── Replication ────────────────────────────────────────────────────────────────

void ATortugaCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// Replicar a todos los clientes para que el visual sea visible en todos
	DOREPLIFETIME(ATortugaCharacter, bIsKnockedDown);
	DOREPLIFETIME(ATortugaCharacter, bIsDead);
	DOREPLIFETIME(ATortugaCharacter, DeathGroundLocation);
	// Freeze del ragdoll de muerte — JIP-safe: llegan en el bunch inicial.
	DOREPLIFETIME(ATortugaCharacter, bRagdollFrozen);
	DOREPLIFETIME(ATortugaCharacter, RagdollFrozenLoc);
	// SkipOwner: el owner ya arranca el emote localmente en TriggerEmote/CancelEmote.
	DOREPLIFETIME_CONDITION(ATortugaCharacter, ReplicatedEmoteIndex, COND_SkipOwner);
	// DBNO revive state
	DOREPLIFETIME(ATortugaCharacter, bIsReviving);
	DOREPLIFETIME_CONDITION(ATortugaCharacter, ReviveProgress, COND_OwnerOnly);
	// BigHead consumable
	DOREPLIFETIME(ATortugaCharacter, bBigHead);
	// Dive
	DOREPLIFETIME(ATortugaCharacter, bIsDiving);
	DOREPLIFETIME(ATortugaCharacter, DiveSerial);
	DOREPLIFETIME(ATortugaCharacter, DiveSplatDizzyUntil);
	DOREPLIFETIME(ATortugaCharacter, DiveTargetYaw);
	DOREPLIFETIME(ATortugaCharacter, bDiveYawInterpActive);
	// Umbrella protection (#29)
	DOREPLIFETIME(ATortugaCharacter, bHasUmbrellaProtection);
	// La cabeza que sigue a la cámara (#623). SkipOwner: el dueño usa su propio giro del mando.
	DOREPLIFETIME_CONDITION(ATortugaCharacter, ReplicatedViewYaw, COND_SkipOwner);
	// Modo VR del dueño: la tortuga gira con la cabeza (Docs/Modo_VR.md). SkipOwner: el dueño lo pone él mismo al momento
	// (SetVRView) y un valor viejo del servidor, al alternar deprisa, pisaría el suyo.
	DOREPLIFETIME_CONDITION(ATortugaCharacter, bVRPlayer, COND_SkipOwner);
	// Primera persona sin gafas: igual, la tortuga gira con la cámara (también SkipOwner, por lo mismo).
	DOREPLIFETIME_CONDITION(ATortugaCharacter, bFirstPersonPlayer, COND_SkipOwner);
	// Manos VR del dueño (los demás ven los brazos siguiéndolas; el dueño usa las suyas).
	DOREPLIFETIME_CONDITION(ATortugaCharacter, RepVRHandLeft, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ATortugaCharacter, RepVRHandRight, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ATortugaCharacter, RepVRHandsValid, COND_SkipOwner);
}

void ATortugaCharacter::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Base que los demás no encuentran por red (teselas y decorado local de la playa, mallas creadas en ejecución): «sin
	// base», con la posición del mundo de siempre (ReplicatedMovement). Si se mandara, llegaría nula con
	// bServerHasBaseComponent y el motor daría la base por «sin resolver»: sin simular ni suavizar a esta tortuga en los
	// demás clientes. Además, cada cambio de base avisaba en el registro del servidor («SupportsObject ... NOT Supported»).
	FBasedMovementInfo& RepBased = GetReplicatedBasedMovement_Mutable();
	if (RepBased.MovementBase && !UTN_TurtleMovementComponent::IsNetResolvableBase(RepBased.MovementBase))
	{
		RepBased.MovementBase = nullptr;
		RepBased.BoneName = NAME_None;
		RepBased.bServerHasBaseComponent = false;
		RepBased.bRelativeRotation = false;
		RepBased.bServerHasVelocity = false;
		// Fijos mientras siga así: sin base no se usan (la posición y el giro van en ReplicatedMovement) y no se reenvían.
		RepBased.Location = FVector::ZeroVector;
		RepBased.Rotation = FRotator::ZeroRotator;
	}

	// La cabeza que sigue a la cámara (#623): la guiñada de la vista respecto del cuerpo, con el giro del mando (el de un
	// cliente llega con su movimiento), en un byte y solo si se ha movido unos grados. Como RemoteViewPitch16 del motor.
	if (const AController* ViewController = GetController())
	{
		const uint8 ViewYaw = TNHeadLook::EncodeYaw(static_cast<float>(ViewController->GetControlRotation().Yaw - GetActorRotation().Yaw));
		if (TNHeadLook::ShouldSend(ReplicatedViewYaw, ViewYaw))
		{
			ReplicatedViewYaw = ViewYaw;
		}
	}
}

void ATortugaCharacter::GetViewRelativeToBody(float& OutYaw, float& OutPitch) const
{
	if (const AController* ViewController = GetController())
	{
		const FRotator View = ViewController->GetControlRotation();
		OutYaw = static_cast<float>(FRotator::NormalizeAxis(View.Yaw - GetActorRotation().Yaw));
		OutPitch = static_cast<float>(FRotator::NormalizeAxis(View.Pitch));
		return;
	}
	OutYaw = TNHeadLook::DecodeYaw(ReplicatedViewYaw);
	OutPitch = static_cast<float>(FRotator::NormalizeAxis(FRotator::DecompressAxisFromShort(GetRemoteViewPitch())));
}

// ── Big Head Consumable ───────────────────────────────────────────────────────

void ATortugaCharacter::OnRep_bBigHead()
{
	ApplyBigHeadVisual(bBigHead);
}

void ATortugaCharacter::ApplyBigHeadVisual(bool bBig)
{
	if (CabezaBone == NAME_None)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[BigHead] CabezaBone not resolved on %s"), *GetNameSafe(this));
		return;
	}
	const float S = bBig ? BigHeadScale : 1.f;
	SetAnimBoneScale(CabezaBone, FVector(S));
}

// ── Jump Procedural Animation ─────────────────────────────────────────────────
//
// Triggered locally when the character jumps from the ground.
// Cosmetic-only; runs only on the local machine (no replication needed).
// Arms shoot up, legs kick back, head tilts back, tail rises.
// Duration: ~0.8s total (fast rise 0.12s → hold → blend back 0.35s).
// ─────────────────────────────────────────────────────────────────────────────

void ATortugaCharacter::TickJumpAnim(float DeltaTime)
{
	if (!bJumpAnimActive) { return; }

	// Cancelled by dive or incapacitation — snap back to rest
	if (bIsDiving || bIsKnockedDown || bIsDead)
	{
		SetAnimBoneRot(Brazo1Bone, Brazo1RestRot);
		SetAnimBoneRot(Brazo2Bone, Brazo2RestRot);
		SetAnimBoneRot(Pata1Bone,  Pata1RestRot);
		SetAnimBoneRot(Pata2Bone,  Pata2RestRot);
		SetAnimBoneRot(ColaBone,   ColaRestRot);
		SetAnimBoneRot(CabezaBone, CabezaRestRot);
		bJumpAnimActive = false;
		JumpAnimTime    = 0.f;
		return;
	}

	JumpAnimTime += DeltaTime;
	const float T = JumpAnimTime;

	const auto Sat = [](float v) { return FMath::Clamp(v, 0.f, 1.f); };

	const FVector AX = ArmSwingAxis;               // arm up/down (R_z(-90°) corrected via UPROPERTY)
	const FVector AY = FVector(1.f, 0.f, 0.f);    // arm roll / head nod (R_z(-90°) corrected)
	const FVector AZ = FVector(0.f, 0.f, 1.f);    // arm forward/back
	const FVector LY = LegSwingAxis;              // (0,1,0)  leg swing
	const FVector TY = TailUpDownAxis;

	auto Ap  = [this](FName Bone, const FRotator& Rest, float Angle, const FVector& Axis)
	{
		ApplyEmoteAngle(Bone, Rest, Angle, Axis);
	};
	auto Ap2 = [this](FName Bone, const FRotator& Rest,
	                  float A1, const FVector& Ax1, float A2, const FVector& Ax2)
	{
		ApplyEmoteAngles2(Bone, Rest, A1, Ax1, A2, Ax2);
	};

	// Envelope: fast rise (0.12s), hold, then blend back to rest (0.35s from t=0.45)
	const float rise    = Sat(T / 0.12f);
	const float fadeOut = T > 0.45f ? Sat((T - 0.45f) / 0.35f) : 0.f;
	const float env     = FMath::InterpEaseOut(0.f, 1.f, rise, 2.f) * (1.f - fadeOut);

	// Both arms shoot up wide — "weeee!" jump pose
	Ap2(Brazo1Bone, Brazo1RestRot,  env *  90.f,  AX,  env * (-25.f), AZ);
	Ap2(Brazo2Bone, Brazo2RestRot, -env *  90.f,  AX,  env *   25.f,  AZ);

	// Legs kick back (both same direction — frog jump)
	Ap(Pata1Bone, Pata1RestRot, env * 70.f, LY);
	Ap(Pata2Bone, Pata2RestRot, env * 70.f, LY);

	// Head tilts back slightly (looking up with excitement)
	Ap(CabezaBone, CabezaRestRot, env * (-18.f), AY);

	// Tail rises
	Ap(ColaBone, ColaRestRot, env * 22.f, TY);

	if (T >= 0.8f)
	{
		// Snap exactly to rest at end (env ≈ 0 already via fadeOut, but be exact)
		SetAnimBoneRot(Brazo1Bone, Brazo1RestRot);
		SetAnimBoneRot(Brazo2Bone, Brazo2RestRot);
		SetAnimBoneRot(Pata1Bone,  Pata1RestRot);
		SetAnimBoneRot(Pata2Bone,  Pata2RestRot);
		SetAnimBoneRot(ColaBone,   ColaRestRot);
		SetAnimBoneRot(CabezaBone, CabezaRestRot);
		bJumpAnimActive = false;
		JumpAnimTime    = 0.f;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Tótem auto-revive — feedback visual/sonoro en todas las máquinas
// ─────────────────────────────────────────────────────────────────────────────

void ATortugaCharacter::Multicast_OnTotemAutoRevive_Implementation()
{
	if (TotemSelfReviveSound)
	{
		PlaySfxAtSelf(TotemSelfReviveSound);
	}
	else if (UTN_TurtleActionSynthComponent* Synth = UTN_TurtleActionSynthComponent::FindOrAddTo(this))
	{
		Synth->PlayRevive(/*bTotem=*/true);
	}
	if (TotemSelfReviveVFX)
	{
		UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), TotemSelfReviveVFX, GetActorLocation());
	}
	OnTotemAutoRevive();
}
