// ─────────────────────────────────────────────────────────────────────────────
// TortugaCharacter — Sistema de Dive (dash direccional).
//
// Definiciones extraídas de TortugaCharacter.cpp (que superaba las 4000 líneas)
// para mejorar la legibilidad. Es la MISMA clase ATortugaCharacter en otra unidad
// de traducción: sin cambios de lógica ni de replicación.
// ─────────────────────────────────────────────────────────────────────────────

#include "Player/TortugaCharacter.h"
#include "Core/TN_Log.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_TurtleMovementComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace TNDiveNet
{
	static int32 GPredict = 1;
	static FAutoConsoleVariableRef CVarPredict(
		TEXT("TN.Net.DivePredict"),
		GPredict,
		TEXT("1 = el panzazo empieza dentro del movimiento del dueño (predicho, sin corrección al empezar; #24); 0 = lo pide por Server_StartDive y lo lanza el servidor, como antes (para comparar con p.NetShowCorrections 1). En quien la controla."),
		ECVF_Cheat);
}

// ── Dive System ───────────────────────────────────────────────────────────────
//
// Flow:
//   Input (client)  → TryDive() → UTN_TurtleMovementComponent::RequestDive(DiveDir)
//   → la petición va en el siguiente movimiento guardado (marca FLAG_Custom_2 y giro, #24): en ese movimiento, el dueño y
//     el servidor llaman a StartDiveFromMove con las mismas reglas (TNDiveLogic::DecideDiveStart): lanzan, encogen la
//     cápsula y suben DiveSerial igual, sin corrección. TN.Net.DivePredict 0: Server_StartDive(DiveDir), como antes.
//   → Multicast_OnDiveVisual(true)   — all clients apply tilt + capsule resize (el dueño ya la encogió en el movimiento)
//   Al caer de tripa, UTN_TurtleMovementComponent arrastra a la tortuga dentro de la simulación del movimiento
//   (predicha en el cliente dueño): inercia, rozamiento por superficie, pendientes y rebotes. Casi parada, se levanta
//   (cápsula de pie sin atravesar nada) y, con eso, TickDive (servidor) llama a EndDive().
//   → bIsDiving = false              — OnRep_IsDiving fires on clients → restore
//
// Visual pattern mirrors knockdown: same KnockdownVisualComp, same NetworkSmoothingMode
// disable, but pitch is FORWARD (-85°) instead of backward.
// Capsule HalfHeight is reduced to simulate a horizontal hitbox.
// ─────────────────────────────────────────────────────────────────────────────

UTN_TurtleMovementComponent* ATortugaCharacter::GetTurtleMovement() const
{
	return Cast<UTN_TurtleMovementComponent>(GetCharacterMovement());
}

float ATortugaCharacter::GetStandingCapsuleHalfHeight() const
{
	// La cápsula de la clase (el objeto por defecto del Blueprint): nunca está encogida por un panzazo.
	const ATortugaCharacter* DefaultTurtle = GetClass() ? GetClass()->GetDefaultObject<ATortugaCharacter>() : nullptr;
	const UCapsuleComponent* DefaultCapsule = DefaultTurtle ? DefaultTurtle->GetCapsuleComponent() : nullptr;
	if (DefaultCapsule && DefaultCapsule->GetUnscaledCapsuleHalfHeight() > 1.f)
	{
		return DefaultCapsule->GetUnscaledCapsuleHalfHeight();
	}
	return DiveCapsuleOrigHalfHeight;
}

bool ATortugaCharacter::IsBellyPoseActive() const
{
	if (!bIsDiving)
	{
		return false;
	}
	// El dueño y el servidor simulan el movimiento: saben al momento que ya se ha levantado. En el resto de máquinas el
	// componente no lleva fase y manda el panzazo replicado.
	const UTN_TurtleMovementComponent* TurtleMove = GetTurtleMovement();
	return !(TurtleMove && TurtleMove->HasStoodUpFromDive(DiveSerial));
}

bool ATortugaCharacter::IsBellyOnGround() const
{
	const UCharacterMovementComponent* CMC = GetCharacterMovement();
	return IsBellyPoseActive() && CMC && CMC->IsMovingOnGround();
}

void ATortugaCharacter::TryDive()
{
	// Only locally controlled, not already diving, not knocked down
	if (!IsLocallyControlled() || bIsDiving || bIsKnockedDown || bIsDead || IsInShell())
	{
		return;
	}

	// El dash va hacia donde el jugador pulsa WASD (relativo a la cámara, ya
	// convertido a mundo por AddMovementInput). Sin input, hacia donde mira la cámara.
	FVector DiveDir = GetLastMovementInputVector();
	DiveDir.Z = 0.f;
	const FRotator ControlRot = GetControlRotation();
	if (!DiveDir.Normalize())
	{
		DiveDir = FRotationMatrix(FRotator(0.f, ControlRot.Yaw, 0.f)).GetUnitAxis(EAxis::X);
		DiveDir.Z = 0.f;
	}
	if (!DiveDir.IsNearlyZero())
	{
		DiveDir.Normalize();
	}
	else
	{
		// Fallback: control rotation degenerada → frente del actor
		DiveDir = GetActorForwardVector();
		DiveDir.Z = 0.f;
		DiveDir.Normalize();
	}

	UE_LOG(LogTortunabo, Log,
		TEXT("[Dive] DiveDir(input/camera)=(%.2f,%.2f,%.2f) ControlYaw=%.1f"),
		DiveDir.X, DiveDir.Y, DiveDir.Z, ControlRot.Yaw);

	// Predicho (#24): en el siguiente movimiento, que también simula el servidor con las mismas reglas.
	if (TNDiveNet::GPredict != 0)
	{
		if (UTN_TurtleMovementComponent* TurtleMove = GetTurtleMovement())
		{
			TurtleMove->RequestDive(DiveDir);
			return;
		}
	}
	Server_StartDive(DiveDir);
}

bool ATortugaCharacter::IsDiveBlocked() const
{
	return bIsKnockedDown || bIsDead || IsInShell() || (CarryComponent && CarryComponent->IsBeingCarried());
}

TNDiveLogic::FDiveMomentumParams ATortugaCharacter::GetDiveMomentumParams() const
{
	TNDiveLogic::FDiveMomentumParams Params;
	Params.BaseSpeed = DiveForwardSpeed;
	Params.ForwardFactor = DiveMomentumForwardFactor;
	Params.LateralFactor = DiveMomentumLateralFactor;
	Params.BackwardFactor = DiveMomentumBackwardFactor;
	Params.MaxTotalSpeed = DiveMaxTotalSpeed;
	return Params;
}

bool ATortugaCharacter::StartDiveFromMove(uint16 DiveYaw, bool bInAir, bool bLaunchPending, bool bReplaying, const FVector& VelocityBefore,
	FVector& OutLaunchVelocity)
{
	// La misma dirección en el dueño y en el servidor: la que viaja por red (giro en 16 bits).
	const FVector DiveDir = TNDiveLogic::DiveDirFromYaw(DiveYaw);
	TNDiveLogic::FDiveStartContext Context;
	Context.bBlocked = IsDiveBlocked();
	Context.bAlreadyDiving = bIsDiving;
	Context.bInAir = bInAir;
	Context.bLaunchPending = bLaunchPending;
	Context.ForwardSpeed = TNDiveLogic::DiveForwardSpeed(DiveDir, JumpStartHorizontalVelocity, GetDiveMomentumParams());
	Context.MinSpeed = DiveStopSpeedThreshold;
	const TNDiveLogic::EDiveStart Result = TNDiveLogic::DecideDiveStart(Context);
	if (Result != TNDiveLogic::EDiveStart::Accept)
	{
		if (!bReplaying)
		{
			UE_LOG(LogTortunabo, Log, TEXT("[Dive] %s (%s): panzazo pedido que no empieza: %s."), *GetNameSafe(this),
				HasAuthority() ? TEXT("servidor") : TEXT("dueño"), TNDiveLogic::DiveStartName(Result));
		}
		return false;
	}

	OutLaunchVelocity = TNDiveLogic::DiveLaunchVelocity(DiveDir, Context.ForwardSpeed, DiveDownwardSpeed);
	// El estado que mira el movimiento (arrastre al caer, rebote en vuelo, cuerpo tumbado): igual en las dos máquinas y en
	// cada repetición. La cápsula tumbada, en su sitio (en el aire: los pies suben).
	DiveSerial    = TNDiveLogic::NextDiveSerial(DiveSerial);
	bIsDiving     = true;
	DiveLockTimer = 0.f;
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCapsuleHalfHeight(DiveCapsuleHalfHeight);
	}
	// El giro hacia la dirección del panzazo también es del movimiento (UTN_TurtleMovementComponent::
	// ComputeOrientToMovementRotation): al repetir, también.
	DiveTargetYaw        = DiveDir.Rotation().Yaw;
	bDiveYawInterpActive = true;
	if (bReplaying)
	{
		return true;
	}

	if (HasAuthority())
	{
		if (ReplicatedEmoteIndex >= 0)
		{
			ReplicatedEmoteIndex = -1;
			CancelEmoteLocalOnly();
		}
		// Llevando a un compañero en alto, el panzazo lo lanza (ThrowWithDive) en el siguiente TickDive: nada se crea dentro
		// del movimiento del cliente. Con la velocidad de antes de lanzarse (la del salto).
		if (CarryComponent && CarryComponent->IsCarrying())
		{
			PendingDiveThrow.bPending = true;
			PendingDiveThrow.DiveDir = DiveDir;
			PendingDiveThrow.DiveVelocity = OutLaunchVelocity;
			PendingDiveThrow.CarrierVelocity = VelocityBefore;
		}
		Multicast_OnDiveVisual(true);
	}
	else
	{
		// Dueño: lo visual ya (cuando llegue el del servidor, la cápsula ya está).
		Multicast_OnDiveVisual_Implementation(true);
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Dive] %s — dive started en el movimiento (%s), nº %d, dir=%s, %.0f cm/s"), *GetNameSafe(this),
		HasAuthority() ? TEXT("servidor") : TEXT("dueño"), static_cast<int32>(DiveSerial), *DiveDir.ToString(), Context.ForwardSpeed);
	return true;
}

void ATortugaCharacter::ApplyServerDiveCorrection(bool bDiving, uint8 Serial)
{
	if (bIsDiving == bDiving && DiveSerial == Serial)
	{
		return;
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Dive] %s: la corrección del servidor trae su panzazo (%s, nº %d; aquí %s, nº %d)."), *GetNameSafe(this),
		bDiving ? TEXT("sí") : TEXT("no"), static_cast<int32>(Serial), bIsDiving ? TEXT("sí") : TEXT("no"), static_cast<int32>(DiveSerial));
	bIsDiving = bDiving;
	DiveSerial = Serial;
	if (!bIsDiving)
	{
		bDiveYawInterpActive = false;
	}
}

bool ATortugaCharacter::Server_StartDive_Validate(FVector DiveDir)
{
	// Red de seguridad de engine: el cliente legítimo manda una dirección (unit
	// vector o casi). NaN o magnitudes absurdas = paquete manipulado → kick.
	// _Implementation sigue saneando (Z=0, normalize con fallback a forward).
	return !DiveDir.ContainsNaN() && DiveDir.SizeSquared() <= 100.f;
}

void ATortugaCharacter::Server_StartDive_Implementation(FVector DiveDir)
{
	// Server-side guards (los mismos que TryDive en el cliente, más la llevada): si el servidor la metió en el
	// caparazón mientras llegaba el RPC, el LaunchCharacter se quedaría pendiente y saltaría al salir del caparazón.
	if (bIsDiving || bIsKnockedDown || bIsDead || IsInShell()
		|| (CarryComponent && CarryComponent->IsBeingCarried()))
	{
		return;
	}

	// Sanitize direction
	DiveDir.Z = 0.f;
	if (!DiveDir.Normalize())
	{
		DiveDir = GetActorForwardVector();
		DiveDir.Z = 0.f;
		DiveDir.Normalize();
	}

	// Cancel any active emote
	if (ReplicatedEmoteIndex >= 0)
	{
		ReplicatedEmoteIndex = -1;
		CancelEmoteLocalOnly();
	}

	// DASH-05: rotación fluida hacia DiveDir. En lugar de snap instantáneo,
	// se setea DiveTargetYaw + flag bDiveYawInterpActive que TickDive (corre en
	// owner + server) consume cada frame para interpolar el actor hacia el
	// target con velocidad DiveYawInterpSpeed (deg/seg).
	//
	// Replicado: clientes remotos también interpolan tras recibir el target —
	// feel suave en todos los puntos de vista.
	DiveTargetYaw         = DiveDir.Rotation().Yaw;
	bDiveYawInterpActive  = true;

	// ── Momentum preservation: cámara ACTUAL vs salto ORIGINAL ─────────────────
	// La velocity horizontal AL SALTAR (capturada en OnJumped) marca la dirección
	// del salto. Comparamos la cámara ACTUAL (donde apunta el jugador al dashear)
	// contra esa dirección original.
	//
	//   alignment = +1 → cámara apunta donde estaba yendo al saltar  → Forward factor (bonus máx)
	//   alignment =  0 → cámara apunta lateral al salto              → Lateral factor (bonus medio)
	//   alignment = -1 → cámara apunta opuesta al salto              → Backward factor (0 default)
	//
	// Si rotaste la cámara para mirar atrás del salto, el momentum se anula —
	// el jugador "decide" no preservar el momentum cambiando hacia donde mira.
	// La misma cuenta que el panzazo predicho (TNDiveLogic::DiveForwardSpeed). Cap simétrico: permite valores negativos
	// (dash hacia atrás cuando la cámara está opuesta al salto y BackwardFactor es lo bastante negativo).
	const float CombinedForwardSpeed = TNDiveLogic::DiveForwardSpeed(DiveDir, JumpStartHorizontalVelocity, GetDiveMomentumParams());
	UE_LOG(LogTortunabo, Log, TEXT("[Dive] Momentum · JumpStartSpeed=%.0f → Total=%.0f (por Server_StartDive)"),
		JumpStartHorizontalVelocity.Size2D(), CombinedForwardSpeed);
	const FVector DiveVelocity = TNDiveLogic::DiveLaunchVelocity(DiveDir, CombinedForwardSpeed, DiveDownwardSpeed);

	// Llevando a un compañero en alto: el panzazo lo lanza, con el impulso del panzazo (la carrera va dentro) y el del
	// salto sumados al del lanzamiento. Antes de lanzarse ella: la velocidad de ahora es la del salto.
	if (CarryComponent && CarryComponent->IsCarrying())
	{
		const UCharacterMovementComponent* ThrowMove = GetCharacterMovement();
		CarryComponent->ThrowWithDive(DiveDir, DiveVelocity, ThrowMove ? ThrowMove->Velocity : FVector::ZeroVector);
	}

	LaunchCharacter(DiveVelocity, /*bXYOverride=*/true, /*bZOverride=*/true);

	// Activate dive state — triggers OnRep on clients. El número nuevo le dice al movimiento que este panzazo aún no se
	// ha arrastrado (al dar la vuelta se salta el 0, que significa «ninguno»).
	DiveSerial    = TNDiveLogic::NextDiveSerial(DiveSerial);
	bIsDiving     = true;
	DiveLockTimer = 0.f;

	// Apply visual on all machines (server runs Multicast locally too)
	Multicast_OnDiveVisual(true);

	UE_LOG(LogTortunabo, Log, TEXT("[Dive] %s — dive started, dir=%s"), *GetNameSafe(this), *DiveDir.ToString());
}

void ATortugaCharacter::Multicast_OnDiveVisual_Implementation(bool bEnter)
{
	UCharacterMovementComponent* CMC = GetCharacterMovement();

	if (bEnter)
	{
		// Disable CMC smoothing so it won't fight our manual rotation
		if (CMC)
		{
			CMC->NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;
		}

		// Shrink capsule to represent horizontal hitbox. El dueño que predice el panzazo ya la encogió en el movimiento
		// (StartDiveFromMove) y la de pie la pone él al levantarse: si este aviso llega tarde no la vuelve a encoger.
		if (!(GetLocalRole() == ROLE_AutonomousProxy && TNDiveNet::GPredict != 0))
		{
			GetCapsuleComponent()->SetCapsuleHalfHeight(DiveCapsuleHalfHeight);
		}

		// Abort any active emote or blend-out — dive takes full control of limbs.
		// Snap all limb components to rest immediately so the dive pose starts clean.
		if (ActiveEmoteIndex >= 0 || bEmoteBlendingOut)
		{
			SetAnimBoneRot(Brazo1Bone, Brazo1RestRot);
			SetAnimBoneRot(Brazo2Bone, Brazo2RestRot);
			SetAnimBoneRot(Pata1Bone,  Pata1RestRot);
			SetAnimBoneRot(Pata2Bone,  Pata2RestRot);
			SetAnimBoneRot(ColaBone,   ColaRestRot);
			SetAnimBoneRot(CabezaBone, CabezaRestRot);
			bEmoteBlendingOut           = false;
			bKnockdownCompSnapshotValid = false;
			EmoteBlendOutTimer          = 0.f;
			ActiveEmoteIndex            = -1;
			EmoteTime                   = 0.f;
		}
		// Abort jump animation if it was running
		bJumpAnimActive = false;
		JumpAnimTime    = 0.f;

		// DiveTiltAlpha will be driven smoothly by TickDive (starts lerping to 1)
	}
	else
	{
		// La cápsula de pie otra vez, con los pies en su sitio (nunca creciendo en su sitio: ver RestoreDiveCapsule).
		RestoreDiveCapsule();

		// Restore CMC smoothing
		if (CMC)
		{
			CMC->NetworkSmoothingMode = ENetworkSmoothingMode::Exponential;
		}

		// TickDive lerps DiveTiltAlpha back to 0 and restores rotations at that point.
		// No immediate snap needed here — the lerp handles the smooth return.
	}
}

void ATortugaCharacter::RestoreDiveCapsule()
{
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!Capsule || Capsule->GetUnscaledCapsuleHalfHeight() >= DiveCapsuleOrigHalfHeight - 0.5f)
	{
		return;
	}
	// Quien simula el movimiento (el servidor y el dueño): de pie con los pies en su sitio y sin meterse en nada, o igual
	// con los pies en su sitio si algo encima no deja (el movimiento lo vuelve a mirar en cada paso fuera del panzazo).
	if (GetLocalRole() != ROLE_SimulatedProxy)
	{
		if (UTN_TurtleMovementComponent* TurtleMove = GetTurtleMovement())
		{
			TurtleMove->RestoreStandingCapsule();
			return;
		}
	}
	// Las demás máquinas: crece y sube lo mismo, con los pies en su sitio (la posición buena llega por red). Creciendo en su
	// sitio, la malla se hundía en la arena hasta la siguiente actualización y la cápsula podía desincrustarse hacia abajo.
	const float OldScaledHalf = Capsule->GetScaledCapsuleHalfHeight();
	Capsule->SetCapsuleHalfHeight(DiveCapsuleOrigHalfHeight);
	const float Rise = Capsule->GetScaledCapsuleHalfHeight() - OldScaledHalf;
	if (Rise > 0.f)
	{
		AddActorWorldOffset(FVector(0.0, 0.0, Rise), false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void ATortugaCharacter::OnRep_IsDiving()
{
	// Called on REMOTE clients when bIsDiving changes.
	// The server already ran Multicast_OnDiveVisual, but OnRep covers clients
	// that joined late or missed the multicast.
	Multicast_OnDiveVisual_Implementation(bIsDiving);
}

void ATortugaCharacter::EndDive()
{
	if (!HasAuthority()) { return; }

	bIsDiving             = false;
	bDiveYawInterpActive  = false;
	DiveLockTimer         = 0.f;

	// Multicast restore visual
	Multicast_OnDiveVisual(false);

	UE_LOG(LogTortunabo, Log, TEXT("[Dive] %s — dive ended."), *GetNameSafe(this));
}

void ATortugaCharacter::TickDive(float DeltaTime)
{
	// Servidor: el panzazo que empezó en un movimiento lanza lo que llevaba (fuera del movimiento del cliente, #24).
	if (PendingDiveThrow.bPending)
	{
		PendingDiveThrow.bPending = false;
		if (HasAuthority() && CarryComponent && CarryComponent->IsCarrying())
		{
			CarryComponent->ThrowWithDive(PendingDiveThrow.DiveDir, PendingDiveThrow.DiveVelocity, PendingDiveThrow.CarrierVelocity);
		}
	}

	// ── Guard: si el mesh ya está en ragdoll (knockdown post-dash a plátano,
	//    o muerte durante dive), NO tocar SetRelativeRotation. Hacerlo dispara
	//    "Attempting to move a fully simulated skeletal mesh" y desincroniza
	//    los bodies del ragdoll. La interpolación visual del dive deja de
	//    importar — el ragdoll es el visual canonico. ──────────────────────
	{
		USkeletalMeshComponent* SkelMeshGuard = GetMesh();
		if (SkelMeshGuard && SkelMeshGuard->IsSimulatingPhysics())
		{
			DiveTiltAlpha = 0.f;
			bDiveYawInterpActive = false;
			return;
		}
		if (bIsKnockedDown || bIsDead)
		{
			// Cancelar interpolación pendiente sin tocar el giro del mesh — ya lo
			// gestiona ApplyKnockdownVisual / SetDeadVisual. La subida del panzazo sí se deshace.
			if (DiveTiltAlpha > 0.f && SkelMeshGuard)
			{
				SkelMeshGuard->SetRelativeLocation(DiveMeshDefaultLoc);
				SkelMeshGuard->SetRelativeScale3D(DiveMeshDefaultScale);
			}
			DiveTiltAlpha = 0.f;
			bDiveYawInterpActive = false;
			return;
		}
	}

	// ── DASH-05: giro del actor hacia DiveTargetYaw ─────────────────────────────
	// Lo hace el movimiento (UTN_TurtleMovementComponent::ComputeOrientToMovementRotation, GetDiveYawTurn) a
	// DiveYawInterpSpeed, igual en el dueño y en el servidor y en cada paso (#24): girando aquí, en el Tick, cada máquina
	// llegaba a otra orientación (el cliente, además, la perdía al juntar movimientos) y el cuerpo tumbado chocaba distinto
	// con las paredes. Los demás reciben el giro replicado. Ya no se apaga aquí al llegar: el dueño, al juntar movimientos,
	// vuelve a simular desde antes de llegar y se quedaba a medio giro. Lo apaga el fin del panzazo y, mientras, el movimiento
	// solo lo aplica en el vuelo (hasta caer de tripa).

	// ── Whole-character forward tilt (all machines, cosmetic) ─────────────────
	// Rotate GetMesh() (carries all re-attached limbs) AND KnockdownVisualComp
	// (Cuerpo) by the same pitch so the entire character tilts as one solid unit.
	// If Cuerpo is a child of GetMesh() the runtime ancestor check skips the
	// separate rotation to prevent double-rotation.
	// Tumbada mientras dure la pose de panzazo: en el dueño y el servidor acaba en cuanto el movimiento la levanta (sin
	// esperar al fin del panzazo replicado); en el resto, con el fin del panzazo.
	const bool bBellyPose = IsBellyPoseActive();
	if (bBellyPose || DiveTiltAlpha > 0.f)
	{
		const float TargetAlpha = bBellyPose ? 1.f : 0.f;
		// Al levantarse del suelo, algo más despacio: se ve el empujón de brazos de UTN_TurtleAnimInstance.
		const UCharacterMovementComponent* TiltMove = GetCharacterMovement();
		const bool bGettingUpFromGround = !bBellyPose && TiltMove && !TiltMove->IsFalling();
		DiveTiltAlpha = FMath::FInterpTo(DiveTiltAlpha, TargetAlpha, DeltaTime, bGettingUpFromGround ? DiveGetUpTiltSpeed : DiveTiltSpeed);
		if (FMath::Abs(DiveTiltAlpha - TargetAlpha) < 0.005f)
		{
			DiveTiltAlpha = TargetAlpha;
		}

		constexpr float DivePitch = -80.f;
		const float     env       = DiveTiltAlpha;

		// 1) SkeletalMesh — all re-attached limbs follow.
		// DASH-01 v2: DiveMeshDefaultRot post-ANIM-01 incluye Yaw=90 (SKM unificado).
		// Sumar al componente Pitch del rotator producía tilt lateral (orden ZYX:
		// Yaw→Pitch→Roll hacía que el Pitch rote alrededor del eje ya rotado por Yaw).
		// Composición con quaterniones: aplicar BaseQuat primero (orientación del mesh)
		// y después un TiltQuat en espacio PADRE (capsule), cuyo eje Y = actor-right →
		// Pitch puro del actor → tilt hacia adelante real.
		if (USkeletalMeshComponent* SkelMesh = GetMesh())
		{
			const FQuat BaseQuat = DiveMeshDefaultRot.Quaternion();
			// Axis configurable en BP para ajustar sin recompilar: user puede probar
			// (1,0,0) vs (0,1,0) vs (-1,0,0) etc según la orientación default del
			// mesh post-ANIM-01. Default (1,0,0) asume Yaw=90 en rest.
			const FVector TiltAxisNorm = DiveTiltAxis.IsNearlyZero() ? FVector(1.f, 0.f, 0.f) : DiveTiltAxis.GetSafeNormal();
			const FQuat TiltQuat(TiltAxisNorm, FMath::DegreesToRadians(DivePitch * env));
			SkelMesh->SetRelativeRotation(TiltQuat * BaseQuat);

			// La malla gira sobre sus pies y la cápsula encoge a DiveCapsuleHalfHeight: se sube para que los pies
			// queden a DiveBellyPivotHeight del suelo y el cuerpo tumbado se apoye en la tripa en vez de hundirse; y
			// se aplasta un poco contra el suelo (ejes locales de la malla: X ancho, Y tripa-espalda, Z largo). Con la
			// cápsula que haya ahora: al levantarse ya está de pie y la subida baja con el giro, sin dar un salto.
			// Además se echa hacia atrás DiveBodyCenterShift: tumbada, la tripa queda sobre la cápsula (gira sobre ella
			// y la cápsula tapa lo más gordo); la cabeza y las patas las frena el movimiento (Belly Slide|Body).
			const double CurrentCapsuleHalf = GetCapsuleComponent() ? GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() : DiveCapsuleHalfHeight;
			const double Lift = (DiveBellyPivotHeight - CurrentCapsuleHalf - DiveMeshDefaultLoc.Z) * env;
			const double Back = static_cast<double>(DiveBodyCenterShift) * env;
			SkelMesh->SetRelativeLocation(DiveMeshDefaultLoc + FVector(-Back, 0.0, FMath::Max(0.0, Lift)));
			SkelMesh->SetRelativeScale3D(DiveMeshDefaultScale * FMath::Lerp(FVector::OneVector, DiveSquash, static_cast<double>(env)));
		}

		// 2) KnockdownVisualComp (Cuerpo) — only if NOT already a descendant of GetMesh()
		//    AND not GetMesh() itself. Sin este segundo check, cuando
		//    KnockdownVisualComp == GetMesh() (fallback del BeginPlay), este bloque
		//    SOBREESCRIBE la rotación quaternion-correcta del bloque 1 con una suma
		//    de FRotator (incorrecta) → dash rotaba sobre eje equivocado.
		if (KnockdownVisualComp.IsValid() && KnockdownVisualComp.Get() != GetMesh())
		{
			bool bIsChildOfMesh = false;
			for (USceneComponent* Cur = KnockdownVisualComp->GetAttachParent(); Cur; Cur = Cur->GetAttachParent())
			{
				if (Cur == GetMesh()) { bIsChildOfMesh = true; break; }
			}
			if (!bIsChildOfMesh)
			{
				FRotator Rot = MeshDefaultRelativeRotation;
				Rot.Pitch   += DivePitch * env;
				KnockdownVisualComp->SetRelativeRotation(Rot);
			}
		}

		// Snap everything back to exact rest once fade-out completes (la cápsula solo cuando el panzazo ha acabado: si ya
		// se levantó, el movimiento la puso de pie).
		if (!bBellyPose && DiveTiltAlpha == 0.f)
		{
			if (!bIsDiving)
			{
				RestoreDiveCapsule();
			}
			if (USkeletalMeshComponent* SkelMesh = GetMesh())
			{
				SkelMesh->SetRelativeRotation(DiveMeshDefaultRot);
				SkelMesh->SetRelativeLocation(DiveMeshDefaultLoc);
				SkelMesh->SetRelativeScale3D(DiveMeshDefaultScale);
			}
			if (KnockdownVisualComp.IsValid())
			{
				KnockdownVisualComp->SetRelativeRotation(MeshDefaultRelativeRotation);
			}
		}
	}

	// ── Recovery check (server only) ─────────────────────────────────────────
	if (!HasAuthority() || !bIsDiving) { return; }

	DiveLockTimer += DeltaTime;

	// 1) Ya se ha levantado del arrastre (lo decide el movimiento, predicho igual en el cliente dueño): fin del panzazo.
	const UTN_TurtleMovementComponent* TurtleMove = GetTurtleMovement();
	if (TurtleMove && TurtleMove->HasStoodUpFromDive(DiveSerial))
	{
		EndDive();
		return;
	}
	// 2) Al agua: se acaba y nada. Metida en el caparazón (caída larga) o sin movimiento (ragdoll, caja física): el
	// movimiento no corre, así que tampoco se levantaría; se acaba aquí.
	const UCharacterMovementComponent* RecoveryMove = GetCharacterMovement();
	if (RecoveryMove && (RecoveryMove->IsSwimming() || RecoveryMove->MovementMode == MOVE_None))
	{
		EndDive();
		return;
	}
	if (IsInShell())
	{
		EndDive();
		return;
	}
	// 3) Sobre la tripa manda el movimiento (se levantará él); tope de seguridad por si algo lo deja colgado.
	if (TurtleMove && TurtleMove->IsOnBelly())
	{
		if (DiveLockTimer >= DiveMaxSeconds)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Dive] %s — panzazo colgado %.1f s sobre la tripa: se acaba."), *GetNameSafe(this), DiveLockTimer);
			EndDive();
		}
		return;
	}

	// 4) En el aire (o sin arrastre, TN.Dive.Slide 0): como siempre, tras el bloqueo mínimo, si se ha parado (contra una
	// pared en pleno vuelo, por ejemplo) se acaba.
	if (DiveLockTimer < DiveMinLockDuration) { return; }

	const float Speed2D = GetVelocity().Size2D();
	if (Speed2D <= DiveStopSpeedThreshold || DiveLockTimer >= DiveMaxSeconds)
	{
		EndDive();
	}
}

