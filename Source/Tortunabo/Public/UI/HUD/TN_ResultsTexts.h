#pragma once

#include "CoreMinimal.h"

struct FTN_CoopScoreBreakdown;

/**
 * Textos de la pantalla de resultados que salen de datos de la partida (UTN_CoopFlowHUDWidget). Todo NSLOCTEXT
 * (espacio TNHUD) y números con TNLocText; sin mundo, para poder probarlos (Tortunabo.Coop.Score.Text).
 */
namespace TNResultsTexts
{
	/** Puntuación final del Coop (#789): una línea por término con sus puntos y el total al final. Vacío si no es válida. */
	TORTUNABO_API FText CoopScoreBreakdown(const FTN_CoopScoreBreakdown& Score);
}
