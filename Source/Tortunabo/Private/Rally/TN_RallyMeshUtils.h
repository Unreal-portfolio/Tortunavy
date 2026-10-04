// Ayudas para colocar mallas existentes a un tamaño dado sin conocer su escala de origen (bordes, puertas y cajas del Rally).
#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMesh.h"

namespace TNRallyMesh
{
	/** Rocas de borde: malla ya existente del proyecto. */
	inline const TCHAR* BorderRockPath = TEXT("/Game/Blueprints/Characters/Meshes/Piedra1.Piedra1");
	/** Caja de munición: la concha cerrada del proyecto. */
	inline const TCHAR* AmmoShellPath = TEXT("/Game/Blueprints/Characters/Meshes/ConchaCerrada.ConchaCerrada");
	/** Postes y viga del arco: no hay poste ni valla como malla estática en Content (SM_Palo es un Blueprint). */
	inline const TCHAR* PostPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	inline const TCHAR* BeamPath = TEXT("/Engine/BasicShapes/Cube.Cube");

	/**
	 * Transformación relativa que encaja los límites de Mesh en una caja de tamaño Size (cm) centrada en Center y girada
	 * Rotation (escala no uniforme por eje local de la malla).
	 */
	inline FTransform FitToBox(const UStaticMesh* Mesh, const FVector& Center, const FVector& Size, const FRotator& Rotation = FRotator::ZeroRotator)
	{
		FVector Origin = FVector::ZeroVector;
		FVector Extent(50.0);
		if (Mesh)
		{
			const FBoxSphereBounds Bounds = Mesh->GetBounds();
			Origin = Bounds.Origin;
			Extent = Bounds.BoxExtent;
		}
		const FVector Scale(
			Extent.X > KINDA_SMALL_NUMBER ? Size.X / (2.0 * Extent.X) : 1.0,
			Extent.Y > KINDA_SMALL_NUMBER ? Size.Y / (2.0 * Extent.Y) : 1.0,
			Extent.Z > KINDA_SMALL_NUMBER ? Size.Z / (2.0 * Extent.Z) : 1.0);
		const FQuat Quat = Rotation.Quaternion();
		return FTransform(Quat, Center - Quat.RotateVector(Origin * Scale), Scale);
	}
}
