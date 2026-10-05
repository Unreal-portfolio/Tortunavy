#pragma once

#include "CoreMinimal.h"

class USkinnedAsset;

/**
 * Ojos de la tortuga en primera persona y en VR (TortugaCharacter_FirstPerson.cpp, Docs/Modo_VR.md): dónde están respecto
 * del hueso de la cabeza. Con un socket «Eyes» en la malla, ese; si no, entre los dos ojos de TotugaDemo_Rig (que no tiene
 * sockets ni huesos en la cara). El hueso Head de esa malla está en la base del cuello: los ojos van unos 20 cm por encima.
 */
namespace TNFirstPersonEyes
{
	/** Hueso de la cabeza (esqueleto Mixamo de la tortuga). */
	TORTUNABO_API FName HeadBone();

	/** Socket opcional de la malla con el punto entre los ojos (para una malla de arte con otra cara). */
	TORTUNABO_API FName EyesSocket();

	/**
	 * Entre los dos ojos de TotugaDemo_Rig en su postura de referencia (espacio de la malla: delante +Y, arriba +Z,
	 * unidades de la malla): a la altura de su centro y algo por delante, dentro de la cabeza que no se pinta para uno mismo.
	 */
	TORTUNABO_API FVector DemoEyes();

	/**
	 * Los ojos de Asset y su hueso Head en la postura de referencia (espacio de la malla, unidades de la malla). Del socket
	 * EyesSocket si lo tiene (bFromSocket) y si no, DemoEyes. false si la malla no tiene hueso Head.
	 */
	TORTUNABO_API bool EyesInRefPose(const USkinnedAsset* Asset, FTransform& OutHeadRef, FVector& OutEyes, bool& bFromSocket);
}
