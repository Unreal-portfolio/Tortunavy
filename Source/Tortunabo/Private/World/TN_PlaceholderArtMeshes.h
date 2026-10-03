#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Templates/Function.h"
#include "UObject/Package.h"
#include "World/TN_PlaceholderArt.h"
#include "World/ProcMap/TN_ProcMapMeshKit.h"
#include "World/ProcMap/TN_ProcMapRuntimeMesh.h"

/**
 * Piezas del arte de código de TN_PlaceholderArt.h: mallas en ejecución con M_CosmeticVertexColor (el alfa del vértice
 * es el brillo, como el decorado de la playa) compartidas por nombre entre todos los ejemplares, y componentes sin
 * colisión para colgarlas.
 */
namespace TNPlaceholderArt
{
	/** Alfa del vértice de las mallas de arte: el de los propios buffers (brillo por pieza, como el decorado de la playa). */
	constexpr float BufferAlpha = -2.f;

	/** Alfa 0 en todos los vértices: mate (las recetas del lobby, cuyo alfa de paleta es 1). */
	constexpr float MatteAlpha = 0.f;

	/**
	 * Malla guardada por nombre: la primera vez se construye con Build; después se reutiliza mientras algún componente
	 * la use (referencia débil: si nadie la usa, el recolector la libera y se rehace). Alpha: BufferAlpha o MatteAlpha.
	 */
	inline UStaticMesh* CachedArtMesh(const FString& Key, TFunctionRef<void(TNProcMesh::FTNProcMeshBuffers&)> Build, float Alpha = BufferAlpha)
	{
		static TMap<FString, TWeakObjectPtr<UStaticMesh>> Cache;
		if (const TWeakObjectPtr<UStaticMesh>* Found = Cache.Find(Key))
		{
			if (UStaticMesh* Existing = Found->Get())
			{
				return Existing;
			}
		}
		TNProcMesh::FTNProcMeshBuffers Buffers;
		Build(Buffers);
		UStaticMesh* Mesh = TNProcRuntimeMesh::MakeStaticMesh(GetTransientPackage(), Buffers, VertexColorMaterial(), false, 0.f, 1.f, Alpha);
		Cache.Add(Key, Mesh);
		return Mesh;
	}

	/** Componente de arte enganchado a Parent, sin colisión ni efecto en la navegación. */
	inline UStaticMeshComponent* AddArtPart(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const FTransform& Relative)
	{
		if (!Owner || !Parent || !Mesh)
		{
			return nullptr;
		}
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transient);
		Part->SetMobility(EComponentMobility::Movable);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		Part->SetGenerateOverlapEvents(false);
		Part->bReceivesDecals = false;
		Part->SetStaticMesh(Mesh);
		Part->SetupAttachment(Parent);
		Part->SetRelativeTransform(Relative);
		Part->RegisterComponent();
		return Part;
	}

	/** Medidas de la caja local de una malla (cero sin malla). */
	inline FVector MeshSize(const UStaticMesh* Mesh)
	{
		return Mesh ? Mesh->GetBoundingBox().GetSize() : FVector::ZeroVector;
	}
}
