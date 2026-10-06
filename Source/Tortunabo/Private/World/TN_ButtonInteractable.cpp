#include "World/TN_ButtonInteractable.h"
#include "World/TN_ButtonGroupManager.h"
#include "Components/ChildActorComponent.h"
#include "Core/TN_Log.h"
#include "Core/TN_LevelTargetSubsystem.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

namespace
{
	// Umbral de llegada al target (posición al cuadrado y ángulo) compartido por
	// el target primario y los targets adicionales en ATN_ButtonInteractable::Tick.
	constexpr float TN_ButtonInteractable_ArrivalDistSqThreshold = 4.f;
	constexpr float TN_ButtonInteractable_ArrivalAngleThreshold = 1.f;
}

ATN_ButtonInteractable::ATN_ButtonInteractable()
{
	PrimaryActorTick.bCanEverTick = true;
	PromptText = NSLOCTEXT("Tortunabo", "ButtonPrompt", "Activar");
	bAlwaysRelevant = true; // El cliente nunca pierde el estado de bIsActivated (#B4/#B8)
}

void ATN_ButtonInteractable::BeginPlay()
{
	Super::BeginPlay();

	// Defer un tick para que todos los ChildActors del chunk existan y estén posicionados.
	GetWorldTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateUObject(this, &ATN_ButtonInteractable::DeferredInit));
}

// ─────────────────────────────────────────────────────────────────────────────
// ResolveParentChunk
// ─────────────────────────────────────────────────────────────────────────────

AActor* ATN_ButtonInteractable::ResolveParentChunk() const
{
	if (AActor* Parent = GetParentActor())
	{
		return Parent;
	}
	if (AActor* OwnerActor = GetOwner())
	{
		return OwnerActor;
	}
	if (AActor* AttachParent = GetAttachParentActor())
	{
		return AttachParent;
	}
	return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Target helpers
// ─────────────────────────────────────────────────────────────────────────────

bool ATN_ButtonInteractable::HasValidTarget() const
{
	return MoveTarget != nullptr || ResolvedMoveComponent.IsValid();
}

FTransform ATN_ButtonInteractable::GetTargetTransform() const
{
	if (MoveTarget)
	{
		return MoveTarget->GetActorTransform();
	}
	if (ResolvedMoveComponent.IsValid())
	{
		return ResolvedMoveComponent->GetComponentTransform();
	}
	return FTransform::Identity;
}

void ATN_ButtonInteractable::SetTargetTransform(const FTransform& T)
{
	if (MoveTarget)
	{
		MoveTarget->SetActorTransform(T);
	}
	else if (ResolvedMoveComponent.IsValid())
	{
		ResolvedMoveComponent->SetWorldTransform(T);
	}
}

void ATN_ButtonInteractable::InterpTargetToward(const FTransform& Goal, float DeltaTime)
{
	const FTransform Current = GetTargetTransform();

	const FVector NewLoc = FMath::VInterpConstantTo(
		Current.GetLocation(), Goal.GetLocation(), DeltaTime, MoveSpeed);
	const FRotator NewRot = FMath::RInterpConstantTo(
		Current.GetRotation().Rotator(), Goal.GetRotation().Rotator(), DeltaTime, RotateSpeed);

	if (MoveTarget)
	{
		MoveTarget->SetActorLocationAndRotation(NewLoc, NewRot);
	}
	else if (ResolvedMoveComponent.IsValid())
	{
		ResolvedMoveComponent->SetWorldLocationAndRotation(NewLoc, NewRot);
	}
}

FTransform ATN_ButtonInteractable::GetGoalTransform() const
{
	if (OffsetMode == EButtonOffsetMode::CyclicStates && CyclicResolvedTransforms.Num() > 0)
	{
		const int32 Idx = FMath::Clamp(CurrentStateIndex, 0, CyclicResolvedTransforms.Num() - 1);
		return CyclicResolvedTransforms[Idx];
	}
	return bIsActivated ? ActivatedTransform : OriginalTransform;
}

// ─────────────────────────────────────────────────────────────────────────────
// DeferredInit — resuelve target + captura transform original + calcula activado
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ButtonInteractable::DeferredInit()
{
	ResolveMoveTargetByTag();

	// ── Capturar transform original y calcular transform activado ────────────
	if (HasValidTarget())
	{
		CaptureBaseTransforms();
		PrecomputeCyclicTransforms();

		bInitialized = true;

		UE_LOG(LogTortunabo, Log, TEXT("[Button] '%s' — Original: %s | Offset: %s | Activado: %s"),
			*GetName(),
			*OriginalTransform.GetLocation().ToString(),
			*ActivatedOffset.GetLocation().ToString(),
			*ActivatedTransform.GetLocation().ToString());

		// Si el botón ya estaba activado (por replicación antes de init), empezar a mover
		if (bIsActivated)
		{
			bIsMoving = true;
		}
		// En cíclico, si el índice ya no es 0 (replicado tarde), empezar a mover
		if (OffsetMode == EButtonOffsetMode::CyclicStates && CurrentStateIndex != 0)
		{
			bIsMoving = true;
		}
	}
}

void ATN_ButtonInteractable::ResolveMoveTargetByTag()
{
	// ── Resolver target por tag/nombre si no está asignado ───────────────────
	if (!MoveTarget && !ResolvedMoveComponent.IsValid() && MoveTargetTag != NAME_None)
	{
		AActor* ParentChunk = ResolveParentChunk();

		if (ParentChunk)
		{
			const FString TagStr = MoveTargetTag.ToString();

			// 1. Buscar COMPONENTE por nombre en el chunk BP padre
			for (UActorComponent* Comp : ParentChunk->GetComponents())
			{
				USceneComponent* SC = Cast<USceneComponent>(Comp);
				if (SC && SC->GetName() == TagStr)
				{
					ResolvedMoveComponent = SC;
					UE_LOG(LogTortunabo, Log, TEXT("[Button] '%s' encontró componente '%s' en chunk '%s'."),
						*GetName(), *TagStr, *GetNameSafe(ParentChunk));
					break;
				}
			}

			// 2. Buscar ChildActor por ActorTag
			if (!ResolvedMoveComponent.IsValid() && !MoveTarget)
			{
				TArray<UChildActorComponent*> ChildActorComps;
				ParentChunk->GetComponents<UChildActorComponent>(ChildActorComps);
				for (UChildActorComponent* CAC : ChildActorComps)
				{
					AActor* ChildActor = CAC ? CAC->GetChildActor() : nullptr;
					if (ChildActor && ChildActor != this && ChildActor->ActorHasTag(MoveTargetTag))
					{
						MoveTarget = ChildActor;
						break;
					}
				}
			}

			// 3. Fallback: attached actors
			if (!MoveTarget && !ResolvedMoveComponent.IsValid())
			{
				TArray<AActor*> AttachedActors;
				ParentChunk->GetAttachedActors(AttachedActors, true, true);
				for (AActor* Attached : AttachedActors)
				{
					if (Attached && Attached != this && Attached->ActorHasTag(MoveTargetTag))
					{
						MoveTarget = Attached;
						break;
					}
				}
			}

			// 4. Fallback clientes: TActorIterator con mismo padre
			if (!MoveTarget && !ResolvedMoveComponent.IsValid())
			{
				for (TActorIterator<AActor> It(GetWorld()); It; ++It)
				{
					AActor* Candidate = *It;
					if (Candidate && Candidate != this
						&& (Candidate->GetOwner() == ParentChunk || Candidate->GetParentActor() == ParentChunk)
						&& Candidate->ActorHasTag(MoveTargetTag))
					{
						MoveTarget = Candidate;
						break;
					}
				}
			}
		}

		// 5. Fallback global: UTN_LevelTargetSubsystem. Permite a botones DENTRO
		//    de un actor creado en partida referenciar actores DEL NIVEL que
		//    el editor no podía ver al editar (el actor se crea después).
		//    Patrón canónico Epic: WorldSubsystem singleton + componente
		//    UTN_RegisterAsTargetComponent que los actores del nivel exponen.
		//    O(1) lookup, sin TActorIterator masivo, sin actor manager en nivel.
		if (!MoveTarget && !ResolvedMoveComponent.IsValid())
		{
			if (UTN_LevelTargetSubsystem* Sub = UTN_LevelTargetSubsystem::Get(this))
			{
				MoveTarget = Sub->FindTarget(MoveTargetTag);
				if (MoveTarget)
				{
					UE_LOG(LogTortunabo, Log,
						TEXT("[Button] '%s' resolved global tag '%s' → '%s' via LevelTargetSubsystem"),
						*GetName(), *MoveTargetTag.ToString(), *GetNameSafe(MoveTarget));
				}
			}
		}

		if (HasValidTarget())
		{
			const FString FoundName = MoveTarget
				? GetNameSafe(MoveTarget)
				: (ResolvedMoveComponent.IsValid() ? ResolvedMoveComponent->GetName() : TEXT("?"));
			UE_LOG(LogTortunabo, Log, TEXT("[Button] '%s' encontró target '%s' por tag '%s' (chunk='%s')."),
				*GetName(), *FoundName, *MoveTargetTag.ToString(), *GetNameSafe(ResolveParentChunk()));
		}
		else
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Button] '%s' — MoveTargetTag='%s' no encontrado. "
				"Chunk padre='%s'. Verifica que exista un componente o ChildActor con ese nombre/tag en el BP."),
				*GetName(), *MoveTargetTag.ToString(), *GetNameSafe(ResolveParentChunk()));
		}
	}
}

void ATN_ButtonInteractable::CaptureBaseTransforms()
{
	OriginalTransform = GetTargetTransform();

	// ActivatedTransform = OriginalTransform + ActivatedOffset (suma simple)
	// NO usar multiplicación de FTransform (ActivatedOffset * Original) porque
	// eso escala el offset por la Scale3D del original → valores absurdos
	// cuando el target hereda escala del chunk.
	ActivatedTransform = OriginalTransform;
	ActivatedTransform.SetLocation(OriginalTransform.GetLocation() + ActivatedOffset.GetLocation());
	ActivatedTransform.SetRotation(ActivatedOffset.GetRotation() * OriginalTransform.GetRotation());
	// Mantener la escala original (el offset no debería cambiar la escala)

	// ── Capturar transforms de targets adicionales (#15) ─────────────────────
	AdditionalOriginalTransforms.Reset();
	AdditionalActivatedTransforms.Reset();
	for (AActor* Extra : AdditionalMoveTargets)
	{
		if (!IsValid(Extra))
		{
			AdditionalOriginalTransforms.Add(FTransform::Identity);
			AdditionalActivatedTransforms.Add(FTransform::Identity);
			continue;
		}
		FTransform ExtraOriginal = Extra->GetActorTransform();
		FTransform ExtraActivated = ExtraOriginal;
		ExtraActivated.SetLocation(ExtraOriginal.GetLocation() + ActivatedOffset.GetLocation());
		ExtraActivated.SetRotation(ActivatedOffset.GetRotation() * ExtraOriginal.GetRotation());
		AdditionalOriginalTransforms.Add(ExtraOriginal);
		AdditionalActivatedTransforms.Add(ExtraActivated);
	}
}

void ATN_ButtonInteractable::PrecomputeCyclicTransforms()
{
	// ── Precomputar transforms cíclicos (#Q2-05) ─────────────────────────
	CyclicResolvedTransforms.Reset();
	CyclicAdditionalResolvedTransforms.Reset();
	if (OffsetMode == EButtonOffsetMode::CyclicStates)
	{
		for (const FTransform& StateOffset : CyclicStateTransforms)
		{
			FTransform Resolved = OriginalTransform;
			Resolved.SetLocation(OriginalTransform.GetLocation() + StateOffset.GetLocation());
			Resolved.SetRotation(StateOffset.GetRotation() * OriginalTransform.GetRotation());
			CyclicResolvedTransforms.Add(Resolved);
		}

		// Precomputar para targets adicionales
		CyclicAdditionalResolvedTransforms.Reserve(AdditionalMoveTargets.Num());
		for (int32 i = 0; i < AdditionalMoveTargets.Num(); ++i)
		{
			TArray<FTransform> PerStateResolved;
			if (AdditionalOriginalTransforms.IsValidIndex(i))
			{
				const FTransform& ExtraOriginal = AdditionalOriginalTransforms[i];
				for (const FTransform& StateOffset : CyclicStateTransforms)
				{
					FTransform Resolved = ExtraOriginal;
					Resolved.SetLocation(ExtraOriginal.GetLocation() + StateOffset.GetLocation());
					Resolved.SetRotation(StateOffset.GetRotation() * ExtraOriginal.GetRotation());
					PerStateResolved.Add(Resolved);
				}
			}
			CyclicAdditionalResolvedTransforms.Add(MoveTemp(PerStateResolved));
		}
	}

	// ── Auto-registro con ButtonGroupManager por tag ─────────────────────────
	// Si este botón está en un chunk y el manager está en el nivel, buscamos
	// el manager por su ActorTag y nos registramos. Solo en servidor (authority).
	if (HasAuthority() && ManagerTag != NAME_None)
	{
		for (TActorIterator<ATN_ButtonGroupManager> It(GetWorld()); It; ++It)
		{
			if (It->ActorHasTag(ManagerTag))
			{
				It->RegisterButton(this);
				break;
			}
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Replication
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ButtonInteractable::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_ButtonInteractable, bIsActivated);
	DOREPLIFETIME(ATN_ButtonInteractable, CurrentPresses);
	DOREPLIFETIME(ATN_ButtonInteractable, CurrentStateIndex);
}

void ATN_ButtonInteractable::OnRep_IsActivated()
{
	if (bInitialized)
	{
		bIsMoving = true;
	}
}

void ATN_ButtonInteractable::OnRep_CurrentPresses()
{
	// BP puede reaccionar al cambio de pulsaciones parciales (ej. cambiar color del botón).
	// No hay lógica de movimiento aquí — el movimiento lo controla OnRep_IsActivated.
}

void ATN_ButtonInteractable::OnRep_CurrentStateIndex()
{
	if (bInitialized)
	{
		bIsMoving = true;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Interact — toggle
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ButtonInteractable::Interact(APawn* Interactor)
{
	if (!HasAuthority() || !CanInteract(Interactor))
	{
		return;
	}

	if (!HasValidTarget())
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Button] '%s' — Sin target válido."), *GetName());
		return;
	}

	if (!bInitialized)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Button] '%s' — Aún no inicializado."), *GetName());
		return;
	}

	FlushNetDormancy();

	++CurrentPresses;

	if (CurrentPresses < PressesRequired)
	{
		// Pulsación parcial — dar feedback pero no activar todavía.
		// No llamamos a Super::Interact aquí: rearmaría LastInteractionServerTime
		// (CooldownSeconds, 0.25s por defecto) y descartaría pulsaciones parciales
		// que lleguen a más de ~4 Hz, perdiéndolas en silencio.
		ForceNetUpdate();
		MulticastPlayHalfPressedFeedback();

		UE_LOG(LogTortunabo, Log, TEXT("[Button] '%s' — pulsación %d/%d"),
			*GetName(), CurrentPresses, PressesRequired);
		return;
	}

	// Pulsaciones completas: avanzar ciclo o togglear según modo.
	CurrentPresses = 0;
	bIsMoving      = true;

	if (OffsetMode == EButtonOffsetMode::CyclicStates && CyclicStateTransforms.Num() > 0)
	{
		CurrentStateIndex = (CurrentStateIndex + 1) % CyclicStateTransforms.Num();
		// bIsActivated refleja "estado != 0" para mantener compat con ButtonGroupManager
		bIsActivated = (CurrentStateIndex != 0);
	}
	else
	{
		bIsActivated = !bIsActivated;
	}

	ForceNetUpdate();

	// Notificar al ButtonGroupManager si está suscrito (servidor-only)
	OnActivationChanged.Broadcast(this, bIsActivated);

	MulticastPlayButtonFeedback();

	// Cooldown via padre (TN_DirectInteractableBase)
	Super::Interact(Interactor);

	UE_LOG(LogTortunabo, Log, TEXT("[Button] '%s' → %s (destino: %s)"),
		*GetName(),
		bIsActivated ? TEXT("ACTIVADO") : TEXT("DESACTIVADO"),
		*GetGoalTransform().GetLocation().ToString());
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick — interpola el target hacia el destino actual
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ButtonInteractable::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bIsMoving || !bInitialized || !HasValidTarget())
	{
		return;
	}

	const FTransform Goal = GetGoalTransform();

	InterpTargetToward(Goal, DeltaTime);

	// Verificar si llegó al destino
	const FTransform CurrentT = GetTargetTransform();
	const float DistSq = FVector::DistSquared(CurrentT.GetLocation(), Goal.GetLocation());
	const float AngleDiff = (CurrentT.GetRotation().Rotator() - Goal.GetRotation().Rotator())
		.GetNormalized().GetManhattanDistance(FRotator::ZeroRotator);

	bool bAllArrived = (DistSq < TN_ButtonInteractable_ArrivalDistSqThreshold
		&& AngleDiff < TN_ButtonInteractable_ArrivalAngleThreshold);

	if (bAllArrived)
	{
		SetTargetTransform(Goal);
	}

	// ── Mover targets adicionales (#15) ──────────────────────────────────────
	for (int32 i = 0; i < AdditionalMoveTargets.Num(); ++i)
	{
		AActor* Extra = AdditionalMoveTargets[i];
		if (!IsValid(Extra) || !AdditionalOriginalTransforms.IsValidIndex(i))
		{
			continue;
		}

		FTransform ExtraGoal;
		if (OffsetMode == EButtonOffsetMode::CyclicStates
			&& CyclicAdditionalResolvedTransforms.IsValidIndex(i)
			&& CyclicAdditionalResolvedTransforms[i].Num() > 0)
		{
			const int32 Idx = FMath::Clamp(CurrentStateIndex, 0,
				CyclicAdditionalResolvedTransforms[i].Num() - 1);
			ExtraGoal = CyclicAdditionalResolvedTransforms[i][Idx];
		}
		else
		{
			ExtraGoal = bIsActivated
				? AdditionalActivatedTransforms[i]
				: AdditionalOriginalTransforms[i];
		}

		const FVector ExtraLoc = Extra->GetActorLocation();
		const FVector NewExtraLoc = FMath::VInterpConstantTo(ExtraLoc, ExtraGoal.GetLocation(), DeltaTime, MoveSpeed);
		const FRotator NewExtraRot = FMath::RInterpConstantTo(
			Extra->GetActorRotation(), ExtraGoal.GetRotation().Rotator(), DeltaTime, RotateSpeed);
		Extra->SetActorLocationAndRotation(NewExtraLoc, NewExtraRot);

		// Verificar convergencia del target adicional
		const float ExtraDistSq = FVector::DistSquared(Extra->GetActorLocation(), ExtraGoal.GetLocation());
		const float ExtraAngle  = (Extra->GetActorRotation() - ExtraGoal.GetRotation().Rotator())
			.GetNormalized().GetManhattanDistance(FRotator::ZeroRotator);
		if (ExtraDistSq >= TN_ButtonInteractable_ArrivalDistSqThreshold
			|| ExtraAngle >= TN_ButtonInteractable_ArrivalAngleThreshold)
		{
			bAllArrived = false;
		}
	}

	// Detener Tick solo cuando TODOS los targets (primario + adicionales) han llegado
	if (bAllArrived)
	{
		bIsMoving = false;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Multicast feedback
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ButtonInteractable::MulticastPlayButtonFeedback_Implementation()
{
	// Override en Blueprint para añadir sonido/VFX/animación del botón.
}

void ATN_ButtonInteractable::MulticastPlayHalfPressedFeedback_Implementation()
{
	// Override en Blueprint para feedback de pulsación parcial (ej. click leve, cambio de color).
}
