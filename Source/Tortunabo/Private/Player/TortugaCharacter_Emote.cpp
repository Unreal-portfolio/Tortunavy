// ─────────────────────────────────────────────────────────────────────────────
// TortugaCharacter — Sistema de emotes (rueda, montajes, animación de huesos, audio).
//
// Definiciones extraídas de TortugaCharacter.cpp para mejorar la legibilidad.
// Incluye los helpers de hueso (SetAnimBoneRot/Loc/Scale/Get) que también usan
// otras secciones (head-look, big-head): al ser métodos de la misma clase, la
// llamada entre unidades de traducción es transparente. Sin cambios de lógica
// ni de replicación, solo organización.
// ─────────────────────────────────────────────────────────────────────────────

#include "Player/TortugaCharacter.h"
#include "Core/TN_Log.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Player/TN_ProcAnimInstance.h"
#include "Net/UnrealNetwork.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Sound/SoundBase.h"
#include "Core/TN_CoopPlayerState.h"
#include "UI/HUD/TN_EmoteWheelDataAsset.h"

// ─────────────────────────────────────────────────────────────────────────────
// EMOTE SYSTEM
// ─────────────────────────────────────────────────────────────────────────────

void ATortugaCharacter::TriggerEmote(int32 Index)
{
	if (!IsLocallyControlled() || !IsValidWheelEmoteId(Index)) { return; }

	// Start locally for immediate visual feedback
	StartEmoteLocally(Index);

	// Replicate to server → other clients
	ServerSetEmote(Index);
}

void ATortugaCharacter::RequestWheelEmote(uint8 EmoteID)
{
	TriggerEmote(static_cast<int32>(EmoteID));
}

bool ATortugaCharacter::IsValidWheelEmoteId(int32 EmoteID) const
{
	if (EmoteID < 0)
	{
		return false;
	}

	if (!EmoteWheelDataAsset)
	{
		return EmoteID >= 0 && EmoteID <= 9;
	}

	return EmoteWheelDataAsset->FindEntryById(static_cast<uint8>(EmoteID)) != nullptr;
}

const FTN_EmoteWheelEntry* ATortugaCharacter::ResolveWheelEmoteEntry(int32 EmoteID) const
{
	if (!EmoteWheelDataAsset || EmoteID < 0)
	{
		return nullptr;
	}

	return EmoteWheelDataAsset->FindEntryById(static_cast<uint8>(EmoteID));
}

float ATortugaCharacter::GetWheelEmoteCooldown(int32 EmoteID) const
{
	if (const FTN_EmoteWheelEntry* Entry = ResolveWheelEmoteEntry(EmoteID))
	{
		return Entry->Cooldown;
	}

	return 0.f;
}

void ATortugaCharacter::PlayWheelEmoteMontage(int32 EmoteID)
{
	const FTN_EmoteWheelEntry* Entry = ResolveWheelEmoteEntry(EmoteID);
	if (!Entry || !Entry->Montage)
	{
		return;
	}

	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp)
	{
		return;
	}

	if (UAnimInstance* AnimInstance = MeshComp->GetAnimInstance())
	{
		AnimInstance->Montage_Stop(0.05f, Entry->Montage);
		AnimInstance->Montage_Play(Entry->Montage, 1.f);
	}
}

void ATortugaCharacter::StopWheelEmoteMontage(int32 EmoteID, float BlendOutTime)
{
	const FTN_EmoteWheelEntry* Entry = ResolveWheelEmoteEntry(EmoteID);
	if (!Entry || !Entry->Montage)
	{
		return;
	}

	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp)
	{
		return;
	}

	if (UAnimInstance* AnimInstance = MeshComp->GetAnimInstance())
	{
		AnimInstance->Montage_Stop(FMath::Max(0.f, BlendOutTime), Entry->Montage);
	}
}

void ATortugaCharacter::StartEmoteLocally(int32 Index)
{
	const int32 PreviousEmoteIndex = ActiveEmoteIndex;
	StopWheelEmoteMontage(PreviousEmoteIndex, 0.05f);

	// Reset all bones to rest pose (clears stale location offsets)
	SetAnimBoneRot(Brazo1Bone, Brazo1RestRot); SetAnimBoneLoc(Brazo1Bone, Brazo1RestLoc);
	SetAnimBoneRot(Brazo2Bone, Brazo2RestRot); SetAnimBoneLoc(Brazo2Bone, Brazo2RestLoc);
	SetAnimBoneRot(Pata1Bone,  Pata1RestRot);  SetAnimBoneLoc(Pata1Bone,  Pata1RestLoc);
	SetAnimBoneRot(Pata2Bone,  Pata2RestRot);  SetAnimBoneLoc(Pata2Bone,  Pata2RestLoc);
	SetAnimBoneRot(ColaBone,   ColaRestRot);   SetAnimBoneLoc(ColaBone,   ColaRestLoc);
	SetAnimBoneRot(CabezaBone, CabezaRestRot); SetAnimBoneLoc(CabezaBone, CabezaRestLoc);

	// (Re)start — pressing the same emote while it plays restarts from the top.
	ActiveEmoteIndex   = Index;
	EmoteTime          = 0.f;
	bEmoteBlendingOut  = false;
	EmoteBlendOutTimer = 0.f;

	// El knockdown emote (ID=100) es interno — no tiene audio ni montaje.
	if (Index != KNOCKDOWN_EMOTE_ID)
	{
		PlayEmoteSound(Index);
		PlayWheelEmoteMontage(Index);
	}
}

bool ATortugaCharacter::ServerSetEmote_Validate(int32 Index)
{
	// Red de seguridad de engine: un cliente legítimo solo manda -1 (cancelar) o
	// IDs de rueda (uint8). Índices fuera de ese rango = cliente manipulado → kick.
	// La validación fina (catálogo, cooldown, estado) sigue en _Implementation.
	return Index >= -1 && Index <= 255;
}

void ATortugaCharacter::ServerSetEmote_Implementation(int32 Index)
{
	if (Index >= 0)
	{
		if (!IsValidWheelEmoteId(Index))
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Emote] ID inválido %d en %s"), Index, *GetNameSafe(this));
			ClientRejectEmote(Index);
			return;
		}

		if (ATN_CoopPlayerState* TNPS = GetPlayerState<ATN_CoopPlayerState>())
		{
			if (!TNPS->bIsAlive || TNPS->bIsDBNO || bIsKnockedDown || IsInShell())
			{
				ClientRejectEmote(Index);
				return;
			}

			const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
			const float Cooldown = GetWheelEmoteCooldown(Index);
			if (!TNPS->CanServerPlayEmote(static_cast<uint8>(Index), Now, Cooldown))
			{
				ClientRejectEmote(Index);
				return;
			}

			TNPS->MarkServerEmotePlayed(static_cast<uint8>(Index), Now);
		}
	}

	// Siempre actualizar el valor replicado, incluso si el emote ya acabó localmente.
	// Esto garantiza que TODOS los clientes remotos reciban OnRep.
	ReplicatedEmoteIndex = Index;

	// Listen server: OnRep no dispara en la máquina que posee la variable,
	// así que arrancamos/cancelamos el emote directamente para pawns remotos.
	if (!IsLocallyControlled())
	{
		if (Index >= 0)
		{
			StartEmoteLocally(Index);
		}
		else
		{
			// Solo cancelar si hay emote activo o blend pendiente
			if (ActiveEmoteIndex >= 0 || bEmoteBlendingOut)
			{
				CancelEmote();
			}
		}
	}

	// ── DBNO Revive: emote triggers revive channel if near a downed teammate ──
	if (Index >= 0)
	{
		TryStartReviveChannel();
	}
	else
	{
		CancelReviveChannel();
	}

	UE_LOG(LogTortunabo, Verbose, TEXT("[Emote] ServerSetEmote(%d) on %s  CabezaBone=%s"),
		Index, *GetNameSafe(this),
		CabezaBone != NAME_None ? TEXT("OK") : TEXT("NULL"));
}

void ATortugaCharacter::OnRep_ReplicatedEmoteIndex()
{
	if (ReplicatedEmoteIndex < 0)
	{
		// Cancelación forzada por el servidor (ej: recuperación de knockdown).
		// Cancelar en TODOS los clientes, independientemente del control local.
		// CancelEmoteLocalOnly es un no-op si ya no hay emote activo.
		if (ActiveEmoteIndex >= 0 || bEmoteBlendingOut)
		{
			CancelEmoteLocalOnly();  // NO llama ServerSetEmote(-1) — evita bucle
		}
	}
	else if (!IsLocallyControlled())
	{
		// Emote arrancado remotamente: aplicar en clientes que no controlan este pawn
		StartEmoteLocally(ReplicatedEmoteIndex);
	}
	// Si IsLocallyControlled() y el índice >= 0: el jugador ya arrancó el emote
	// localmente en TriggerEmote() antes de enviar el RPC, no hay que hacer nada.

	UE_LOG(LogTortunabo, Verbose, TEXT("[Emote] OnRep_ReplicatedEmoteIndex(%d) on %s  IsLocal=%s  CabezaBone=%s"),
		ReplicatedEmoteIndex, *GetNameSafe(this),
		IsLocallyControlled() ? TEXT("YES") : TEXT("NO"),
		CabezaBone != NAME_None ? TEXT("OK") : TEXT("NULL"));
}

void ATortugaCharacter::ClientRejectEmote_Implementation(int32 Index)
{
	if (ActiveEmoteIndex == Index || bEmoteBlendingOut)
	{
		CancelEmoteLocalOnly();
	}
}

void ATortugaCharacter::CancelEmoteLocalOnly()
{
	if (ActiveEmoteIndex < 0 && !bEmoteBlendingOut) { return; }

	const int32 EmoteIdToStop = ActiveEmoteIndex;

	SnapshotBrazo1    = GetAnimBoneRot(Brazo1Bone); SnapshotBrazo1Loc = GetAnimBoneLoc(Brazo1Bone);
	SnapshotBrazo2    = GetAnimBoneRot(Brazo2Bone); SnapshotBrazo2Loc = GetAnimBoneLoc(Brazo2Bone);
	SnapshotPata1     = GetAnimBoneRot(Pata1Bone);  SnapshotPata1Loc  = GetAnimBoneLoc(Pata1Bone);
	SnapshotPata2     = GetAnimBoneRot(Pata2Bone);  SnapshotPata2Loc  = GetAnimBoneLoc(Pata2Bone);
	SnapshotCola      = GetAnimBoneRot(ColaBone);   SnapshotColaLoc   = GetAnimBoneLoc(ColaBone);
	SnapshotCabeza    = GetAnimBoneRot(CabezaBone); SnapshotCabezaLoc = GetAnimBoneLoc(CabezaBone);

	// Si cancela el emote de knockdown, capturar también la rotación del cuerpo para restaurarla.
	bKnockdownCompSnapshotValid = (EmoteIdToStop == KNOCKDOWN_EMOTE_ID) && KnockdownVisualComp.IsValid();
	if (bKnockdownCompSnapshotValid)
	{
		SnapshotKnockdownComp = KnockdownVisualComp->GetRelativeRotation();
	}

	ActiveEmoteIndex   = -1;
	bEmoteBlendingOut  = true;
	EmoteBlendOutTimer = 0.f;
	StopEmoteSound();
	StopWheelEmoteMontage(EmoteIdToStop, EmoteBlendOutDuration);
}

void ATortugaCharacter::CancelEmote()
{
	if (ActiveEmoteIndex < 0 && !bEmoteBlendingOut)
	{
		return;
	}

	CancelEmoteLocalOnly();

	// Replicar cancelación a otros clientes (solo desde el jugador que controla)
	if (IsLocallyControlled())
	{
		ServerSetEmote(-1);
	}
}

void ATortugaCharacter::SetAnimBoneRot(FName BoneName, const FRotator& Rot) const
{
	if (BoneName == NAME_None) { return; }
	if (UTN_ProcAnimInstance* Inst = Cast<UTN_ProcAnimInstance>(GetMesh() ? GetMesh()->GetAnimInstance() : nullptr))
		Inst->BoneQuat.Add(BoneName, FQuat(Rot));
}

FRotator ATortugaCharacter::GetAnimBoneRot(FName BoneName) const
{
	if (BoneName == NAME_None || !GetMesh()) { return FRotator::ZeroRotator; }
	if (const UTN_ProcAnimInstance* Inst = Cast<UTN_ProcAnimInstance>(GetMesh()->GetAnimInstance()))
	{
		if (const FQuat* Q = Inst->BoneQuat.Find(BoneName)) { return Q->Rotator(); }
	}
	return GetMesh()->GetBoneQuaternion(BoneName, EBoneSpaces::ComponentSpace).Rotator();
}

void ATortugaCharacter::SetAnimBoneLoc(FName BoneName, const FVector& Loc) const
{
	if (BoneName == NAME_None) { return; }
	if (UTN_ProcAnimInstance* Inst = Cast<UTN_ProcAnimInstance>(GetMesh() ? GetMesh()->GetAnimInstance() : nullptr))
		Inst->BoneLoc.Add(BoneName, Loc);
}

void ATortugaCharacter::SetAnimBoneScale(FName BoneName, const FVector& Scale) const
{
	if (BoneName == NAME_None) { return; }
	if (UTN_ProcAnimInstance* Inst = Cast<UTN_ProcAnimInstance>(GetMesh() ? GetMesh()->GetAnimInstance() : nullptr))
	{
		if (Scale.Equals(FVector::OneVector)) { Inst->BoneScale.Remove(BoneName); }
		else                                  { Inst->BoneScale.Add(BoneName, Scale); }
	}
}

FVector ATortugaCharacter::GetAnimBoneLoc(FName BoneName) const
{
	if (BoneName == NAME_None || !GetMesh()) { return FVector::ZeroVector; }
	if (const UTN_ProcAnimInstance* Inst = Cast<UTN_ProcAnimInstance>(GetMesh()->GetAnimInstance()))
	{
		if (const FVector* L = Inst->BoneLoc.Find(BoneName)) { return *L; }
	}
	return GetMesh()->GetBoneLocation(BoneName, EBoneSpaces::ComponentSpace);
}

void ATortugaCharacter::ApplyEmoteAngle(FName BoneName, const FRotator& Rest,
                                        float AngleDeg, const FVector& Axis) const
{
	const FQuat SwingQuat(Axis.GetSafeNormal(), FMath::DegreesToRadians(AngleDeg));
	SetAnimBoneRot(BoneName, (SwingQuat * FQuat(Rest)).Rotator());
}

void ATortugaCharacter::ApplyEmoteAngles2(FName BoneName, const FRotator& Rest,
                                          float A1, const FVector& Ax1,
                                          float A2, const FVector& Ax2) const
{
	const FQuat Q1(Ax1.GetSafeNormal(), FMath::DegreesToRadians(A1));
	const FQuat Q2(Ax2.GetSafeNormal(), FMath::DegreesToRadians(A2));
	SetAnimBoneRot(BoneName, (Q2 * Q1 * FQuat(Rest)).Rotator());
}

void ATortugaCharacter::ApplyEmoteAngles3(FName BoneName, const FRotator& Rest,
                                          float A1, const FVector& Ax1,
                                          float A2, const FVector& Ax2,
                                          float A3, const FVector& Ax3) const
{
	const FQuat Q1(Ax1.GetSafeNormal(), FMath::DegreesToRadians(A1));
	const FQuat Q2(Ax2.GetSafeNormal(), FMath::DegreesToRadians(A2));
	const FQuat Q3(Ax3.GetSafeNormal(), FMath::DegreesToRadians(A3));
	SetAnimBoneRot(BoneName, (Q3 * Q2 * Q1 * FQuat(Rest)).Rotator());
}

void ATortugaCharacter::TickEmote(float DeltaTime)
{
	// Dive owns all limb components while active — skip emote updates entirely
	if (bIsDiving || DiveTiltAlpha > 0.f) { return; }

	// ── Blend-out: lerp all components back to rest rotations & locations ─────
	if (bEmoteBlendingOut)
	{
		EmoteBlendOutTimer += DeltaTime;
		const float Alpha = FMath::Min(EmoteBlendOutTimer / FMath::Max(EmoteBlendOutDuration, KINDA_SMALL_NUMBER), 1.f);

		auto Blend = [&](FName Bone, const FRotator& SnapR, const FRotator& RestR,
		                 const FVector& SnapL, const FVector& RestL)
		{
			SetAnimBoneRot(Bone, FMath::Lerp(SnapR, RestR, Alpha));
			SetAnimBoneLoc(Bone, FMath::Lerp(SnapL, RestL, Alpha));
		};

		Blend(Brazo1Bone, SnapshotBrazo1, Brazo1RestRot, SnapshotBrazo1Loc, Brazo1RestLoc);
		Blend(Brazo2Bone, SnapshotBrazo2, Brazo2RestRot, SnapshotBrazo2Loc, Brazo2RestLoc);
		Blend(Pata1Bone,  SnapshotPata1,  Pata1RestRot,  SnapshotPata1Loc,  Pata1RestLoc);
		Blend(Pata2Bone,  SnapshotPata2,  Pata2RestRot,  SnapshotPata2Loc,  Pata2RestLoc);
		Blend(ColaBone,   SnapshotCola,   ColaRestRot,   SnapshotColaLoc,   ColaRestLoc);
		Blend(CabezaBone, SnapshotCabeza, CabezaRestRot, SnapshotCabezaLoc, CabezaRestLoc);

		// Restaurar la rotación del cuerpo si era un knockdown emote
		if (!bKnockdownRagdollActive && bKnockdownCompSnapshotValid && KnockdownVisualComp.IsValid())
		{
			KnockdownVisualComp->SetRelativeRotation(
				FMath::Lerp(SnapshotKnockdownComp, MeshDefaultRelativeRotation, Alpha));
		}

		if (Alpha >= 1.f)
		{
			bEmoteBlendingOut = false;
			bKnockdownCompSnapshotValid = false;
			// Asegurar restauración exacta al finalizar blend-out
			if (!bKnockdownRagdollActive && KnockdownVisualComp.IsValid())
			{
				KnockdownVisualComp->SetRelativeRotation(MeshDefaultRelativeRotation);
			}
		}
		return;
	}

	if (ActiveEmoteIndex < 0) { return; }

	EmoteTime += DeltaTime;
	const float T = EmoteTime;

	// Trig helpers (frequency in Hz)
	auto S    = [](float Hz, float t) { return FMath::Sin(2.f * PI * Hz * t); };
	auto Cos  = [](float Hz, float t) { return FMath::Cos(2.f * PI * Hz * t); };
	auto Sat  = [](float v)           { return FMath::Clamp(v, 0.f, 1.f); };

	// ── Ejes de referencia (espacio de componente — SKM unificado) ──────────
	// El nuevo SKM unificado tiene los huesos rotados ~90° Z respecto al blockout.
	// Todos los ejes se han rotado R_z(-90°): (1,0,0)→(0,-1,0), (0,1,0)→(1,0,0).
	// AZ y LZ permanecen iguales (Z es invariante bajo rotaciones Z).
	//
	// BRAZOS: AX sube/baja (+AX arriba), AY roll eje largo, AZ adelante/atrás
	// CABEZA: AX = roll, AY = cabeceo nod, AZ = shake izq/dcha
	// PATAS:  LX = splay lateral (abrir/cerrar), LY = stride (configurable), LZ = twist
	// COLA:   TY = arriba/abajo, TZ = lado a lado, TX = roll
	const FVector AX(0.f, -1.f, 0.f);
	const FVector AY(1.f,  0.f, 0.f);
	const FVector AZ(0.f,  0.f, 1.f);

	// Patas
	const FVector LX(0.f, -1.f, 0.f);       // splay lateral
	const FVector LY = LegSwingAxis;          // stride adelante/atrás (configurable)
	const FVector LZ(0.f,  0.f, 1.f);       // giro pata

	// Cola
	const FVector TY = TailUpDownAxis;
	const FVector TZ = TailSideAxis;
	const FVector TX(0.f, -1.f, 0.f);       // roll cola

	// ── Helpers de aplicación (rotación) ──────────────────────────────────────
	auto Ap = [&](FName Bone, const FRotator& Rest, float Angle, const FVector& Axis)
	{
		ApplyEmoteAngle(Bone, Rest, Angle, Axis);
	};

	auto Ap2 = [&](FName Bone, const FRotator& Rest,
	               float A1, const FVector& Ax1, float A2, const FVector& Ax2)
	{
		ApplyEmoteAngles2(Bone, Rest, A1, Ax1, A2, Ax2);
	};

	auto Ap3 = [&](FName Bone, const FRotator& Rest,
	               float A1, const FVector& Ax1,
	               float A2, const FVector& Ax2,
	               float A3, const FVector& Ax3)
	{
		ApplyEmoteAngles3(Bone, Rest, A1, Ax1, A2, Ax2, A3, Ax3);
	};

	// ── Helper de aplicación (traslación) — para Explosivo y Modo Loco 2 ────
	auto SetLoc = [&](FName Bone, const FVector& RestLoc, const FVector& Offset)
	{
		SetAnimBoneLoc(Bone, RestLoc + Offset);
	};

	bool bEnded = false;

	switch (ActiveEmoteIndex)
	{
	// ──────────────────────────────────────────────────────────────────────────
	// 0  SALUDAR — Brazo1 (dcha): AY 90° posiciona + AX ±75° aleteo.
	//              Brazo2 (izq) baja.  2.5 s
	// ──────────────────────────────────────────────────────────────────────────
	case 0:
	{
		const float setup   = Sat(T / 0.3f);
		const float fadeOut = (T > 2.0f) ? Sat((T - 2.0f) / 0.5f) : 0.f;
		const float env     = setup * (1.f - fadeOut);
		const float wave    = (T > 0.3f) ? S(5.f, T) : 0.f;

		// Brazo1 (dcha): AY 90° posiciona brazo, AX ±75° oscila (aleteo)
		Ap2(Brazo1Bone, Brazo1RestRot,
		    env * 90.f,              AY,
		    env * 75.f * wave,       AX);

		// Brazo2 (izq): baja relajado. EMOTE-01: signo negado — con ANIM-01 el eje AX
		// apunta a (0,-1,0), así que para que la mano quede ABAJO hace falta -80°.
		Ap(Brazo2Bone, Brazo2RestRot, env * -80.f, AX);

		// Cabeza — cabeceo visible mientras saluda
		Ap(CabezaBone, CabezaRestRot, env * 20.f * S(2.f, T), AY);

		// Cola
		Ap(ColaBone, ColaRestRot, env * 10.f * S(1.5f, T), TZ);

		bEnded = (T >= 2.5f);
		break;
	}

	// ──────────────────────────────────────────────────────────────────────────
	// 1  APLAUSO — Brazos en V cerrada, palmaditas rápidas.             3 s
	// ──────────────────────────────────────────────────────────────────────────
	case 1:
	{
		const float rise    = Sat(T / 0.4f);
		const float fadeOut = (T > 2.5f) ? Sat((T - 2.5f) / 0.5f) : 0.f;
		const float env     = rise * (1.f - fadeOut);
		const float bob     = (T > 0.4f) ? 12.f * S(4.f, T) : 0.f;   // 4 Hz, ±12°

		// Brazo1 (dcha): arriba +AX 50° (más cerrado) + palma al frente AY 90°
		Ap2(Brazo1Bone, Brazo1RestRot,
		    env * 110.f + bob * env,   AX,
		    env * 90.f,               AY);
		// Brazo2 (izq): arriba −AX 50° + palma al frente AY +90°
		Ap2(Brazo2Bone, Brazo2RestRot,
		    -(env * 110.f + bob * env), AX,
		    env * 90.f,                AY);

		// Cabeza — mira hacia las manos que aplauden
		Ap(CabezaBone, CabezaRestRot, env * (-25.f), AY);

		// Patas
		Ap(Pata1Bone, Pata1RestRot,  10.f * S(2.f, T) * env, LY);
		Ap(Pata2Bone, Pata2RestRot, -10.f * S(2.f, T) * env, LY);

		// Cola
		Ap(ColaBone, ColaRestRot, env * 20.f * S(3.f, T), TZ);

		bEnded = (T >= 3.f);
		break;
	}

	// ──────────────────────────────────────────────────────────────────────────
	// 2  HELICÓPTERO — Brazos giran en AZ. Patas: ±90° en eje frontal (LX)
	//                  y luego giran en LZ igual que los brazos.      LOOP ∞
	// ──────────────────────────────────────────────────────────────────────────
	case 2:
	{
		// Spin continuo: 8 vueltas/seg
		const float spin = 360.f * T * 8.f;

		// Brazos: spin clásico en AZ (propulsor horizontal)
		Ap(Brazo1Bone, Brazo1RestRot,  spin, AZ);
		Ap(Brazo2Bone, Brazo2RestRot, -spin, AZ);

		// Patas: en los primeros 0.4s se posicionan ±90° en el eje frontal (LX)
		// y acto seguido giran en LZ igual que los brazos — efecto hélice
		const float legSetup = Sat(T / 0.4f);
		Ap2(Pata1Bone, Pata1RestRot,
		     legSetup *  90.f,  LX,   // eje frontal +90°
		     spin,              LZ);  // giro continuo
		Ap2(Pata2Bone, Pata2RestRot,
		     legSetup * -90.f,  LX,   // eje frontal -90° (opuesto)
		     spin,              LZ);  // giro continuo

		// Cabeza — cabeceo al ritmo del spin
		Ap(CabezaBone, CabezaRestRot, 20.f * S(2.f, T), AY);


		// Cola
		Ap(ColaBone, ColaRestRot, 12.f * S(1.f, T), TZ);

		break; // LOOP ∞
	}

	// ──────────────────────────────────────────────────────────────────────────
	// 3  PALMADA POTENTE — Ambos brazos: AX 90° + AZ 90° posición,
	//     luego AY oscila −90° rápido abajo, lento arriba.            LOOP ∞
	// ──────────────────────────────────────────────────────────────────────────
	case 3:
	{
		// 0.3s para que los brazos lleguen a la posición de partida
		const float setup = Sat(T / 0.3f);

		// Palmada asimétrica: baja rápido (25% ciclo), sube normal (50% ciclo), pausa (25%)
		const float clapT     = FMath::Max(T - 0.3f, 0.f);
		const float cycleTime = 0.5f;   // 2 palmadas/segundo
		float palmada = 0.f;
		if (clapT > 0.f)
		{
			const float phase = FMath::Fmod(clapT, cycleTime) / cycleTime;
			if (phase < 0.25f)
			{
				// Fast down: 0 → −90° en 25% del ciclo
				palmada = -90.f * (phase / 0.25f);
			}
			else if (phase < 0.75f)
			{
				// Normal up: −90° → 0 en 50% del ciclo
				palmada = -90.f * (1.f - (phase - 0.25f) / 0.50f);
			}
			// else: 25% pausa en reposo (brazos arriba antes de la siguiente palmada)
		}

		// Brazo1 (dcha): AX 90° sube + AZ 90° centra + AY palmada
		Ap3(Brazo1Bone, Brazo1RestRot,
		    setup *  -90.f,  AX,
		    setup *  -90.f,  AZ,
		    palmada,        AY);
		// Brazo2 (izq): espejo (−AX, −AZ, misma AY)
		Ap3(Brazo2Bone, Brazo2RestRot,
		    setup * 90.f,  AX,
		    setup * 90.f,  AZ,
		    palmada,        AY);

		// Cabeza asiente con cada palmada
		Ap(CabezaBone, CabezaRestRot, 18.f * (palmada / -90.f), AY);

		// Cola menea
		Ap(ColaBone, ColaRestRot, 15.f * S(2.f, T), TZ);

		break; // LOOP ∞
	}

	// ──────────────────────────────────────────────────────────────────────────
	// 4  APLAUDIR — Brazo1 (dcha): AX 120° + AY (20° + ±10° clap).
	//              Brazo2 (izq): reposo abajo. Cola: TY 180° rápido.   2 s
	// ──────────────────────────────────────────────────────────────────────────
	case 4:
	{
		const float setup   = Sat(T / 0.3f);
		const float clap    = S(6.f, T);
		const float colSnap = Sat(T / 0.2f); // cola rápido a posición

		// Brazo1 (dcha): AX 120° sube + AY 20° orienta + AY ±10° palmada rápida
		Ap2(Brazo1Bone, Brazo1RestRot,
		    setup * -120.f,                      AX,
		    setup * (-20.f - 10.f * clap),       AY);

		// Brazo2 (izq): reposo abajo
		Ap(Brazo2Bone, Brazo2RestRot, setup * 80.f, AX);

		// Cabeza — cabeceo al ritmo del aplauso
		Ap(CabezaBone, CabezaRestRot, 15.f * S(6.f, T), AY);

		// Cola: entra rápido por −TY (0.2s), sale por el mismo lado (0.4s a partir de T=1.5)
		// — la vuelta es explícita para que no tome el arco opuesto en el blend-out.
		const float colOut = T > 2.5f ? Sat((T - 2.5f) / 0.1f) : 0.f;
		const float colEnv = colSnap * (1.f - colOut);
		Ap(ColaBone, ColaRestRot, colEnv * -175.f, TY);

		bEnded = (T >= 2.f);
		break;
	}

	// ──────────────────────────────────────────────────────────────────────────
	// 5  BAILE IRLANDÉS — Ambos brazos se juntan por DETRÁS.         LOOP ∞
	// ──────────────────────────────────────────────────────────────────────────
	case 5:
	{
		const float setup = Sat(T / 0.4f);

		// Brazo1 (dcha): atrás (+AZ), baja un poco (−AX)
		Ap2(Brazo1Bone, Brazo1RestRot,
		    setup * (-10.f),    AX,
		    setup *  80.f,      AZ);
		// Brazo2 (izq):  atrás (−AZ), baja un poco (+AX = abajo para izq)
		Ap2(Brazo2Bone, Brazo2RestRot,
		    setup *  10.f,      AX,
		    setup * (-80.f),    AZ);

		// Cabeza — gira al ritmo del baile
		Ap(CabezaBone, CabezaRestRot, 15.f * S(2.5f, T), AZ);

		// Patas
		Ap2(Pata1Bone, Pata1RestRot,
		     30.f * S(2.5f, T),   LY,
		     15.f * S(1.25f, T),  LX);
		Ap2(Pata2Bone, Pata2RestRot,
		    -30.f * S(2.5f, T),   LY,
		    -15.f * S(1.25f, T),  LX);

		// Cola
		Ap(ColaBone, ColaRestRot, 10.f * S(2.5f, T), TZ);

		break; // LOOP ∞
	}

	// ──────────────────────────────────────────────────────────────────────────
	// 6  FLOTAR (SUPERMAN) — bien                                       5 s
	// ──────────────────────────────────────────────────────────────────────────
	case 6:
	{
		const float fadeIn  = Sat(T / 1.f);
		const float fadeOut = (T > 4.f) ? Sat((T - 4.f) / 1.f) : 0.f;
		const float env     = fadeIn * (1.f - fadeOut);
		const float drift   = 3.f * S(0.3f, T);

		Ap2(Brazo1Bone, Brazo1RestRot,
		    env * 50.f + drift,    AX,
		    env * (-80.f),         AZ);
		Ap2(Brazo2Bone, Brazo2RestRot,
		    -(env * 50.f) + drift, AX,
		    env * 80.f,            AZ);

		Ap(CabezaBone, CabezaRestRot, env * (-20.f) + drift, AY);

		Ap(Pata1Bone, Pata1RestRot, (env * 80.f) + drift, LY);
		Ap(Pata2Bone, Pata2RestRot, (env * 80.f) + drift, LY);

		Ap2(ColaBone, ColaRestRot,
		    env * 20.f + drift,  TY,
		    drift * 2.f,         TZ);

		bEnded = (T >= 5.f);
		break;
	}

	// ──────────────────────────────────────────────────────────────────────────
	// 7  SEÑALAR — Brazo1 apunta, Brazo2 baja reposo.                  1.2 s
	// ──────────────────────────────────────────────────────────────────────────
	case 7:
	{
		const float rise    = Sat(T / 0.1f);
		const float fadeOut = (T > 0.8f) ? Sat((T - 0.8f) / 0.4f) : 0.f;
		const float env     = rise * (1.f - fadeOut);

		// Brazo1 (dcha): apunta adelante
		Ap2(Brazo1Bone, Brazo1RestRot,
		    env * 25.f,        AX,
		    env * (-80.f),     AZ);

		// Brazo2 (izq): reposo abajo
		Ap(Brazo2Bone, Brazo2RestRot, env * 80.f, AX);

		Ap(CabezaBone, CabezaRestRot, env * 20.f, AY);
		Ap(ColaBone, ColaRestRot, env * 8.f * S(1.5f, T), TZ);

		bEnded = (T >= 1.2f);
		break;
	}

	// ──────────────────────────────────────────────────────────────────────────
	// 8  MODO LOCO 2 — Caos TOTAL: todos los componentes rotan y se
	//     trasladan a lo bestia (incluye Brazo2).                     LOOP ∞
	// ──────────────────────────────────────────────────────────────────────────
	case 8:
	{
		// Rotación salvaje en TODOS los componentes
		Ap3(Brazo1Bone, Brazo1RestRot,
		     50.f * S(3.0f, T),          AX,
		     40.f * S(2.1f, T),          AY,
		     20.f * S(4.7f, T),          AZ);
		Ap3(Brazo2Bone, Brazo2RestRot,
		    -55.f * S(2.8f, T + 0.1f),   AX,
		    -40.f * S(3.3f, T + 0.2f),   AY,
		     25.f * S(4.1f, T),           AZ);
		Ap3(Pata1Bone, Pata1RestRot,
		     35.f * S(4.0f, T),          LY,
		     20.f * S(2.7f, T),          LX,
		     10.f * S(5.0f, T),          LZ);
		Ap3(Pata2Bone, Pata2RestRot,
		    -35.f * S(4.0f, T + 0.08f),  LY,
		    -20.f * S(2.7f, T + 0.15f),  LX,
		    -10.f * S(5.0f, T + 0.05f),  LZ);
		Ap3(ColaBone, ColaRestRot,
		    15.f * Cos(5.f, T),           TY,
		    25.f * S(7.f, T),             TZ,
		    10.f * S(3.f, T + 0.1f),      TX);
		Ap3(CabezaBone, CabezaRestRot,
		    25.f * S(3.7f, T),            AY,
		    30.f * S(2.3f, T + 0.07f),    AZ,
		    15.f * S(5.5f, T),            AX);

		// Traslación: HIPER-EXAGERADA tipo explosión — ±250 cm
		const float locAmp = 250.f;
		SetLoc(Brazo1Bone, Brazo1RestLoc, FVector(
		    locAmp * S(1.7f, T),  locAmp * S(2.3f, T + 0.1f),  locAmp * S(3.1f, T)));
		SetLoc(Brazo2Bone, Brazo2RestLoc, FVector(
		    locAmp * S(2.1f, T + 0.05f), -locAmp * S(1.9f, T),  locAmp * S(3.5f, T + 0.1f)));
		SetLoc(Pata1Bone, Pata1RestLoc, FVector(
		    locAmp * S(2.9f, T), -locAmp * S(1.3f, T + 0.15f), locAmp * S(3.7f, T)));
		SetLoc(Pata2Bone, Pata2RestLoc, FVector(
		   -locAmp * S(2.3f, T + 0.1f),  locAmp * S(1.7f, T),  locAmp * Cos(3.3f, T)));
		SetLoc(ColaBone, ColaRestLoc, FVector(
		   -locAmp * S(1.1f, T), locAmp * S(2.9f, T + 0.05f), locAmp * S(4.1f, T)));
		SetLoc(CabezaBone, CabezaRestLoc, FVector(
		    locAmp * 0.5f * S(2.5f, T), locAmp * 0.3f * S(3.1f, T + 0.12f), locAmp * 0.4f * S(4.3f, T)));

		break; // LOOP ∞
	}

	// ──────────────────────────────────────────────────────────────────────────
	// 9  FIESTA (MODO LOCO) — Caos puro solo rotación, sin separar.     LOOP ∞
	// ──────────────────────────────────────────────────────────────────────────
	case 9:
		Ap3(Brazo1Bone, Brazo1RestRot,
		     50.f * S(3.0f, T),          AX,
		     40.f * S(2.1f, T),          AY,
		     20.f * S(4.7f, T),          AZ);
		Ap3(Brazo2Bone, Brazo2RestRot,
		    -55.f * S(2.3f, T + 0.13f),  AX,
		    -35.f * S(3.1f, T),          AY,
		     25.f * S(1.7f, T + 0.08f),  AZ);
		Ap3(Pata1Bone, Pata1RestRot,
		     35.f * S(4.0f, T),          LY,
		     20.f * S(2.7f, T),          LX,
		     10.f * S(5.0f, T),          LZ);
		Ap3(Pata2Bone, Pata2RestRot,
		    -35.f * S(4.0f, T + 0.08f),  LY,
		    -20.f * S(2.7f, T + 0.15f),  LX,
		    -10.f * S(5.0f, T + 0.05f),  LZ);
		Ap3(ColaBone, ColaRestRot,
		    15.f * Cos(5.f, T),           TY,
		    25.f * S(7.f, T),             TZ,
		    10.f * S(3.f, T + 0.1f),      TX);
		Ap3(CabezaBone, CabezaRestRot,
		    25.f * S(3.7f, T),            AY,
		    30.f * S(2.3f, T + 0.07f),    AZ,
		    15.f * S(5.5f, T),            AX);
		break; // LOOP ∞

	default:
		// ── KNOCKDOWN EMOTE (ID = 100) ─────────────────────────────────────
		// Tortuga boca arriba: el CUERPO completo rota 90° hacia atrás,
		// patas/brazos apuntan hacia arriba con pequeña oscilación.
		if (ActiveEmoteIndex == KNOCKDOWN_EMOTE_ID)
		{
			// CRÍTICO: si el ragdoll físico está activo, la física dicta el visual.
			// Cualquier SetRelativeRotation del KnockdownVisualComp o BoneQuat
			// override aquí es:
			//   (a) inútil — el guard IsSimulatingPhysics del ProcAnimInstance los suprime
			//   (b) peligroso — el SetRelativeRotation sobre el mesh simulado dispara
			//       el warning "Attempting to move a fully simulated skeletal mesh"
			// Early-exit: la física ragdoll es el visual de knockdown cuando bUsePhysicsRagdoll.
			if (bKnockdownRagdollActive)
			{
				break;
			}
			// DIAGNOSTIC: confirmar que entramos al case (si Kirk sigue mal aunque
			// modifique valores, puede que el emote nunca entre aquí).
			static bool bLoggedKirk = false;
			if (!bLoggedKirk)
			{
				bLoggedKirk = true;
				UE_LOG(LogTortunabo, Warning,
					TEXT("[Diagnostic] KNOCKDOWN case ENTRADA · IsSimulating=%s (si true, BoneQuat suppressed by ProcAnim guard)"),
					(GetMesh() && GetMesh()->IsSimulatingPhysics()) ? TEXT("Y") : TEXT("N"));
			}
			const float Setup    = FMath::Min(T / 0.2f, 1.f);  // Pose completada en 0.2s
			const float EasedSetup = FMath::InterpEaseOut(0.f, 1.f, Setup, 2.f); // Deceleración natural
			const float Struggle = FMath::Sin(T * 4.f) * 5.f;  // ±5° oscilación suave de "lucha"

			// ── Rotar el cuerpo entero (componente "Cuerpo" / KnockdownVisualComp) ──
			// KIRK-FIX: post-ANIM-01 los signos quedaron invertidos. User: "todos hacia
			// abajo en vez de arriba; en vez de -30 → +30, salvo patas que en eje vertical
			// -30 → 30". Flipeo en eje HORIZONTAL para cuerpo/brazos/cabeza/cola; patas
			// cambian a eje VERTICAL (LZ) con sus signos re-orientados.
			if (KnockdownVisualComp.IsValid())
			{
				FRotator TargetRot = MeshDefaultRelativeRotation;
				TargetRot.Pitch += 90.f;  // flipeo: -=90 → +=90 (eje horizontal)
				KnockdownVisualComp->SetRelativeRotation(
					FMath::Lerp(MeshDefaultRelativeRotation, TargetRot, EasedSetup));
			}

			// Patas: cambio de eje LY (horizontal stride) → LZ (vertical twist) + signo invertido
			Ap(Pata1Bone, Pata1RestRot,  EasedSetup * (-80.f - Struggle), LZ);
			Ap(Pata2Bone, Pata2RestRot,  EasedSetup * ( 80.f - Struggle), LZ);

			// Brazos: mismo eje AX, signo negado
			Ap(Brazo1Bone, Brazo1RestRot,  EasedSetup * (-70.f - Struggle * 0.7f), AX);
			Ap(Brazo2Bone, Brazo2RestRot,  EasedSetup * ( 70.f - Struggle * 0.7f), AX);

			// Cabeza: signo negado
			Ap(CabezaBone, CabezaRestRot, EasedSetup * -25.f, TY);

			// Cola: signo negado (expresión original -(−35 + S·0.5) = 35 − S·0.5)
			Ap(ColaBone, ColaRestRot, EasedSetup * (35.f - Struggle * 0.5f), TY);

			// NO termina (bEnded queda false) — se mantiene hasta RecoverFromKnockdown
		}
		else
		{
			bEnded = true;
		}
		break;
	}

	if (bEnded) { CancelEmote(); }
}

// ── Emote Audio ───────────────────────────────────────────────────────────────

UAudioComponent* ATortugaCharacter::CreateProximityAudioComponent(FName Name, float InnerRadius, float OuterRadius)
{
	UAudioComponent* Comp = NewObject<UAudioComponent>(this, Name);
	if (!Comp)
	{
		return nullptr;
	}

	Comp->SetupAttachment(GetRootComponent());
	Comp->bAutoActivate = false;
	Comp->bAlwaysPlay = false;

	// Proximity attenuation matching voice chat range.
	Comp->bAllowSpatialization = true;
	Comp->bOverrideAttenuation = true;
	Comp->AttenuationOverrides.bAttenuate = true;
	Comp->AttenuationOverrides.bSpatialize = true;
	Comp->AttenuationOverrides.FalloffDistance = FMath::Max(OuterRadius - InnerRadius, 100.f);
	Comp->AttenuationOverrides.AttenuationShape = EAttenuationShape::Sphere;
	Comp->AttenuationOverrides.AttenuationShapeExtents = FVector(InnerRadius);
	Comp->AttenuationOverrides.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;

	return Comp;
}

void ATortugaCharacter::PlayEmoteSound(int32 Index)
{
	// Stop any previous emote sound first.
	StopEmoteSound();

	if (!EmoteSounds.IsValidIndex(Index) || !EmoteSounds[Index])
	{
		return;
	}

	// Lazily create the audio component on first use (attached to root, spatialized).
	if (!EmoteAudioComponent)
	{
		EmoteAudioComponent = CreateProximityAudioComponent(TEXT("EmoteAudio"), EmoteAudioInnerRadius, EmoteAudioOuterRadius);
		if (!EmoteAudioComponent)
		{
			return;
		}

		// Auto-restart when sound finishes → forced loop while emote is active.
		EmoteAudioComponent->OnAudioFinished.AddDynamic(this, &ATortugaCharacter::OnEmoteAudioFinished);

		EmoteAudioComponent->RegisterComponent();
	}

	EmoteAudioComponent->SetSound(EmoteSounds[Index]);
	if (EmoteAudioFadeInTime > 0.f)
	{
		EmoteAudioComponent->FadeIn(EmoteAudioFadeInTime);
	}
	else
	{
		EmoteAudioComponent->Play();
	}
}

void ATortugaCharacter::StopEmoteSound()
{
	if (EmoteAudioComponent && EmoteAudioComponent->IsPlaying())
	{
		EmoteAudioComponent->Stop();
	}
}

void ATortugaCharacter::OnEmoteAudioFinished()
{
	// If an emote is still active, restart the sound for seamless looping.
	if (ActiveEmoteIndex >= 0 && EmoteAudioComponent && EmoteAudioComponent->Sound)
	{
		EmoteAudioComponent->Play();
	}
}

// ── Per-emote input handlers ──────────────────────────────────────────────────
void ATortugaCharacter::OnEmote0() { TriggerEmote(0); }
void ATortugaCharacter::OnEmote1() { TriggerEmote(1); }
void ATortugaCharacter::OnEmote2() { TriggerEmote(2); }
void ATortugaCharacter::OnEmote3() { TriggerEmote(3); }
void ATortugaCharacter::OnEmote4() { TriggerEmote(4); }
void ATortugaCharacter::OnEmote5() { TriggerEmote(5); }
void ATortugaCharacter::OnEmote6() { TriggerEmote(6); }
void ATortugaCharacter::OnEmote7() { TriggerEmote(7); }
void ATortugaCharacter::OnEmote8() { TriggerEmote(8); }
void ATortugaCharacter::OnEmote9() { TriggerEmote(9); }

