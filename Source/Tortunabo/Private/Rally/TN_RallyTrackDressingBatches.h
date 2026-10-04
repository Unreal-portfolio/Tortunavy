// Lotes de instancias del decorado del Rally y ayudas para apoyar mallas en el suelo. Lo comparten
// TN_RallyTrackDressing.cpp (decorado cercano, público y pórticos), TN_RallyTrackDressingBarrier.cpp (límites) y
// TN_RallyTrackDressingFar.cpp (decorado lejano).
#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMesh.h"

struct FTNRallyFarDecorEntry;

/** Lo que se va a instanciar, agrupado por malla y forma de dibujarse; al final, un componente HISM por grupo. */
struct FTNRallyDressingBatches
{
	enum class ECollision : uint8
	{
		None,
		Block,
		BlockCamera,
		Rail
	};

	struct FBatch
	{
		UStaticMesh* Mesh = nullptr;
		ECollision Collision = ECollision::None;
		bool bShadow = true;
		bool bHidden = false;
		float CullCm = 0.f;
		TArray<FTransform> Transforms;
	};

	TArray<FBatch> Batches;

	/** Instancias del grupo (lo crea si no existe). CullCm 0 = no deja de dibujarse; manda el mayor del grupo. */
	TArray<FTransform>& Get(UStaticMesh* Mesh, ECollision Collision, bool bShadow, float CullCm, bool bHidden = false)
	{
		for (FBatch& Batch : Batches)
		{
			if (Batch.Mesh == Mesh && Batch.Collision == Collision && Batch.bShadow == bShadow && Batch.bHidden == bHidden)
			{
				Batch.CullCm = (Batch.CullCm <= 0.f || CullCm <= 0.f) ? 0.f : FMath::Max(Batch.CullCm, CullCm);
				return Batch.Transforms;
			}
		}
		FBatch& Added = Batches.AddDefaulted_GetRef();
		Added.Mesh = Mesh;
		Added.Collision = Collision;
		Added.bShadow = bShadow;
		Added.bHidden = bHidden;
		Added.CullCm = CullCm;
		return Added.Transforms;
	}
};

namespace TNRallyDressingPlace
{
	/** Trazas de suelo: por encima y por debajo de la cota de referencia, y desnivel máximo para darlo por bueno (cm). */
	constexpr double GroundUpCm = 800.0;
	constexpr double GroundDownCm = 3000.0;
	constexpr double MaxGroundStepCm = 600.0;

	/** Caja de la malla girada y escalada (sin trasladar). */
	inline FBox PlacedBox(const UStaticMesh* Mesh, const FQuat& Rotation, double Scale)
	{
		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		const FBox Local(Bounds.Origin - Bounds.BoxExtent, Bounds.Origin + Bounds.BoxExtent);
		return Local.TransformBy(FTransform(Rotation, FVector::ZeroVector, FVector(Scale)));
	}

	/** Escala uniforme para que la malla girada mida TargetCm en su lado horizontal más largo. */
	inline double UniformScaleFor(const UStaticMesh* Mesh, const FQuat& Rotation, double TargetCm)
	{
		const FVector Size = PlacedBox(Mesh, Rotation, 1.0).GetSize();
		const double Widest = FMath::Max(Size.X, Size.Y);
		return Widest > UE_KINDA_SMALL_NUMBER ? TargetCm / Widest : 1.0;
	}

	/** Transformación que apoya la malla (centrada en planta) sobre Ground. */
	inline FTransform FitUniformOnGround(const UStaticMesh* Mesh, const FVector& Ground, const FQuat& Rotation, double Scale)
	{
		const FBox Placed = PlacedBox(Mesh, Rotation, Scale);
		const FVector Center = Placed.GetCenter();
		return FTransform(Rotation, Ground - FVector(Center.X, Center.Y, Placed.Min.Z), FVector(Scale));
	}
}

namespace TNRallyDressingFar
{
	/** Piezas grandes por defecto del decorado lejano (definido en TN_RallyTrackDressingFar.cpp). */
	TArray<FTNRallyFarDecorEntry> DefaultEntries();
}
