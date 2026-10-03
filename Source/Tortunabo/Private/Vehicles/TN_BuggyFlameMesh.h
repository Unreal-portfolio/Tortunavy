// Llama del turbo del buggy (#294): cono construido en ejecución con color de vértice y el material emisivo de los
// brillos del mapa (M_ProcGlow: opaco, emisivo = color del vértice x 2, florece). BasicShapeMaterial no es emisivo y la
// llama se veía como un cono de plástico naranja a la sombra. Mismo tamaño que /Engine/BasicShapes/Cone (100 cm de alto y
// de diámetro, centrado, punta en +Z), así TNBuggy::BoostFlameTransform no cambia.
#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UStaticMesh;

namespace TNBuggyFlameMesh
{
	/** Material de la llama (M_ProcGlow); null si no está. */
	UMaterialInterface* GlowMaterial();

	/** Cono de llama con BaseColor en la boca del escape que aclara a amarillo en la punta; una malla por color, compartida. */
	UStaticMesh* GlowCone(const FLinearColor& BaseColor);

	/** Color de la punta para un color de base: hacia el amarillo blanquecino del centro de la llama. */
	FLinearColor TipColor(const FLinearColor& BaseColor);
}
