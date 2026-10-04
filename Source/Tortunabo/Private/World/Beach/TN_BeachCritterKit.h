#pragma once

#include "CoreMinimal.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Art/TN_Art.h"

/**
 * Utilidades de motor de los enemigos de la ronda 3 (ermitaño, pulpo, pulgas y tanque; las mallas están en
 * TN_BeachCritterMeshes.h y las cachés, materiales y partículas en TN_BeachEnemyKit.h). Las piezas se crean con
 * RF_Transient | RF_DuplicateTransient (la copia del PIE no las arrastra) y sin colisión.
 */
namespace TNBeachCritterKit
{
	/** Pivote vacío (para girar un grupo de piezas) enganchado a Parent en RelLoc. */
	inline USceneComponent* AddPivot(AActor* Owner, USceneComponent* Parent, const FVector& RelLoc)
	{
		if (!Owner || !Parent)
		{
			return nullptr;
		}
		USceneComponent* Comp = NewObject<USceneComponent>(Owner, NAME_None, RF_Transient | RF_DuplicateTransient);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetupAttachment(Parent);
		Comp->SetRelativeLocation(RelLoc);
		Comp->RegisterComponent();
		return Comp;
	}

	/** Raíz absoluta (se coloca en el mundo cada fotograma), enganchada a la del actor. */
	inline USceneComponent* AddWorldRoot(AActor* Owner, const FTransform& World)
	{
		USceneComponent* Comp = AddPivot(Owner, Owner ? Owner->GetRootComponent() : nullptr, FVector::ZeroVector);
		if (Comp)
		{
			Comp->SetAbsolute(true, true, true);
			Comp->SetWorldTransform(World);
		}
		return Comp;
	}

	/** Pieza de malla enganchada a Parent en RelLoc, sin colisión (móvil, sombra opcional). */
	inline UStaticMeshComponent* AddPart(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& RelLoc, bool bShadow = true)
	{
		if (!Owner || !Parent || !Mesh)
		{
			return nullptr;
		}
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transient | RF_DuplicateTransient);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCanEverAffectNavigation(false);
		Comp->SetGenerateOverlapEvents(false);
		Comp->SetCastShadow(bShadow);
		Comp->bReceivesDecals = false;
		Comp->SetStaticMesh(Mesh);
		Comp->SetupAttachment(Parent);
		Comp->SetRelativeLocation(RelLoc);
		Comp->RegisterComponent();
		return Comp;
	}

	/**
	 * Lo mismo, como pieza de arte Slot (Docs/Arte_Assets.md, TNArt::ApplyToComponent): con sustituto, la malla de arte va
	 * de hija de la pieza y se mueve, se esconde y se enseña con ella. Pivote: el origen de la pieza.
	 */
	inline UStaticMeshComponent* AddPart(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& RelLoc, bool bShadow, FName Slot)
	{
		UStaticMeshComponent* Comp = AddPart(Owner, Parent, Mesh, RelLoc, bShadow);
		TNArt::ApplyToComponent(Comp, Slot);
		return Comp;
	}

	/**
	 * Instancias de Mesh (Count, escondidas a escala cero) enganchadas a Parent. Con bWorld, el componente queda en el
	 * origen del mundo y las instancias se ponen en coordenadas de mundo; si no, en las de Parent.
	 */
	inline UInstancedStaticMeshComponent* AddInstances(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, int32 Count, bool bWorld, bool bShadow)
	{
		if (!Owner || !Parent || !Mesh || Count <= 0)
		{
			return nullptr;
		}
		UInstancedStaticMeshComponent* Comp = NewObject<UInstancedStaticMeshComponent>(Owner, NAME_None, RF_Transient | RF_DuplicateTransient);
		Comp->SetStaticMesh(Mesh);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCanEverAffectNavigation(false);
		Comp->SetGenerateOverlapEvents(false);
		Comp->SetCastShadow(bShadow);
		Comp->bReceivesDecals = false;
		Comp->bEvaluateWorldPositionOffset = false;
		Comp->SetupAttachment(Parent);
		Comp->RegisterComponent();
		if (bWorld)
		{
			Comp->SetAbsolute(true, true, true);
			Comp->SetWorldTransform(FTransform::Identity);
		}
		TArray<FTransform> Hidden;
		Hidden.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), Count);
		Comp->AddInstances(Hidden, false, bWorld);
		return Comp;
	}

	/** Lo mismo, como pieza de arte Slot (TNArt::ApplyToInstances; WriteInstances les pone su ajuste). */
	inline UInstancedStaticMeshComponent* AddInstances(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, int32 Count, bool bWorld, bool bShadow, FName Slot)
	{
		UInstancedStaticMeshComponent* Comp = AddInstances(Owner, Parent, Mesh, Count, bWorld, bShadow);
		TNArt::ApplyToInstances(Comp, Slot);
		return Comp;
	}

	/** Escribe todas las instancias de una vez (en mundo o relativas al componente), con el ajuste de su malla de arte si la tiene. */
	inline void WriteInstances(UInstancedStaticMeshComponent* Comp, const TArray<FTransform>& Xf, bool bWorld)
	{
		if (Comp && Xf.Num() > 0 && Comp->GetInstanceCount() >= Xf.Num())
		{
			// Se llama cada fotograma: sin MarkRenderStateDirty: TransformChanged ya actualiza instancias y límites al final del fotograma sin rehacer el proxy (#566).
			TNArt::UpdateInstances(Comp, 0, Xf, bWorld, false, false);
		}
	}

	/** Enciende o apaga la visibilidad de una pieza (y sus hijas) solo si cambia. */
	inline void SetShown(USceneComponent* Comp, bool bShow)
	{
		if (Comp && Comp->IsVisible() != bShow)
		{
			Comp->SetVisibility(bShow, true);
		}
	}

	/** Suavizado exponencial independiente del fotograma (Tau en segundos). */
	inline float Ease(float Current, float Target, float DeltaSeconds, float Tau)
	{
		return Current + (Target - Current) * (1.f - FMath::Exp(-DeltaSeconds / FMath::Max(0.001f, Tau)));
	}

	/** Giro (grados) hacia Target como mucho MaxStep grados. */
	inline float TurnToward(float Current, float Target, float MaxStep)
	{
		const float Delta = FMath::FindDeltaAngleDegrees(Current, Target);
		return FMath::UnwindDegrees(Current + FMath::Clamp(Delta, -MaxStep, MaxStep));
	}

	/** Hash estable de tres enteros a [0, 1). */
	inline float Hash3(uint32 A, uint32 B, uint32 C)
	{
		uint32 X = A * 0x9E3779B1u ^ (B + 0x7F4A7C15u) * 0x85EBCA6Bu ^ (C + 0x165667B1u) * 0xC2B2AE35u;
		X ^= X >> 16;
		X *= 0x7FEB352Du;
		X ^= X >> 15;
		X *= 0x846CA68Bu;
		X ^= X >> 16;
		return static_cast<float>(X & 0xFFFFFF) / 16777216.f;
	}
}
