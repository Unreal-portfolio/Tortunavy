#include "World/TN_PlaceholderArt.h"

#include "Components/StaticMeshComponent.h"
#include "Core/TN_Log.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "UObject/Package.h"

namespace TNPlaceholderArt
{
	bool IsPlaceholderMesh(const UStaticMesh* Mesh)
	{
		// Las mallas de código viven en el paquete transitorio (/Engine/Transient): son arte, no marcadores.
		return Mesh && Mesh->GetOutermost() != GetTransientPackage() && Mesh->GetPathName().StartsWith(EnginePrefix());
	}

	bool IsVisiblePlaceholder(const UStaticMeshComponent* Component)
	{
		return Component && Component->IsVisible() && !Component->bHiddenInGame && IsPlaceholderMesh(Component->GetStaticMesh());
	}

	bool NeedsCodeArt(const UStaticMeshComponent* Component)
	{
		const UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
		return !Mesh || IsPlaceholderMesh(Mesh);
	}

	FBox VisiblePlaceholderBounds(const AActor* Actor)
	{
		FBox Bounds(ForceInit);
		if (!Actor)
		{
			return Bounds;
		}
		TInlineComponentArray<UStaticMeshComponent*> Components(Actor);
		for (const UStaticMeshComponent* Component : Components)
		{
			if (IsVisiblePlaceholder(Component))
			{
				Bounds += Component->Bounds.GetBox();
			}
		}
		return Bounds;
	}

	int32 HidePlaceholders(AActor* Actor, bool bDisableCollision)
	{
		int32 Hidden = 0;
		if (!Actor)
		{
			return Hidden;
		}
		TInlineComponentArray<UStaticMeshComponent*> Components(Actor);
		for (UStaticMeshComponent* Component : Components)
		{
			if (!IsVisiblePlaceholder(Component))
			{
				continue;
			}
			Component->SetVisibility(false, false);
			Component->SetCastShadow(false);
			if (bDisableCollision)
			{
				Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
			++Hidden;
		}
		return Hidden;
	}

	int32 CountVisiblePlaceholders(const AActor* Actor)
	{
		int32 Count = 0;
		if (!Actor)
		{
			return Count;
		}
		TInlineComponentArray<UStaticMeshComponent*> Components(Actor);
		for (const UStaticMeshComponent* Component : Components)
		{
			Count += IsVisiblePlaceholder(Component) ? 1 : 0;
		}
		return Count;
	}

	float FitScale(const FVector& ArtSize, const FVector& TargetSize)
	{
		const double ArtPlan = FMath::Max(ArtSize.X, ArtSize.Y);
		const double TargetPlan = FMath::Max(TargetSize.X, TargetSize.Y);
		if (ArtPlan <= UE_KINDA_SMALL_NUMBER || TargetPlan <= UE_KINDA_SMALL_NUMBER)
		{
			return 1.f;
		}
		double Scale = TargetPlan / ArtPlan;
		if (ArtSize.Z > UE_KINDA_SMALL_NUMBER && TargetSize.Z > UE_KINDA_SMALL_NUMBER)
		{
			Scale = FMath::Min(Scale, TargetSize.Z / ArtSize.Z);
		}
		return static_cast<float>(Scale);
	}

	UMaterialInterface* VertexColorMaterial()
	{
		static TWeakObjectPtr<UMaterialInterface> Cached;
		if (UMaterialInterface* Existing = Cached.Get())
		{
			return Existing;
		}
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Cosmetics/Materials/M_CosmeticVertexColor.M_CosmeticVertexColor"));
		if (!Material)
		{
			UE_LOG(LogTortunabo, Error, TEXT("[PlaceholderArt] Falta M_CosmeticVertexColor: el arte de código sale con el material por defecto."));
			Material = UMaterial::GetDefaultMaterial(MD_Surface);
		}
		Cached = Material;
		return Material;
	}
}
