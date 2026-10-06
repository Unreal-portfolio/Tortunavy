#pragma once

#include "CoreMinimal.h"

class APlayerController;
class UMaterialInterface;
class UTexture2D;

/**
 * Manchas de tinta en la pantalla de la tortuga alcanzada por la tinta de calamar, dibujadas en código (#787). Es el
 * recambio del material de post-proceso de BP_TortugaCharacter (InkOverlayMaterial), que trae el DefaultPostProcessMaterial
 * del motor y no tapa nada. Una imagen de Slate por jugador local, encima del mundo y por debajo del HUD, que se apaga
 * sola al acabar (se desvanece el último trozo): no depende de que la tortuga siga viva para quitarla.
 */
namespace TNInkScreen
{
	/** true si Material no sirve para la tinta: nulo o un material de relleno del motor. */
	bool NeedsFallback(const UMaterialInterface* Material);

	/** Tapa la pantalla del jugador local de PC Duration segundos (alarga la que haya). Nada en máquinas sin pantalla. */
	void Show(const APlayerController* PC, float Duration);

	/** Quita ya la tinta del jugador local de PC. */
	void Hide(const APlayerController* PC);

	/** Opacidad (0-1) de la tinta a falta de SecondsLeft segundos: entera y, en el último trozo, desvaneciéndose. */
	float OpacityAt(double SecondsLeft);

	/** La textura de las manchas (16:9, en caché); sin bEvenHeadless, null en máquinas sin pantalla. */
	UTexture2D* SplatTexture(bool bEvenHeadless = false);
}
