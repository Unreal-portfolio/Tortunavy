#pragma once

#include "CoreMinimal.h"

struct FTN_CoopScoreBreakdown;
struct FTN_EndTitle;

/**
 * Textos de la pantalla de resultados que salen de datos de la partida (UTN_CoopFlowHUDWidget). Todo NSLOCTEXT
 * (espacio TNHUD) y números con TNLocText; sin mundo, para poder probarlos (Tortunabo.Coop.Score.Text).
 */
namespace TNResultsTexts
{
	/**
	 * Puntos de final de partida (#873): una línea por término con sus puntos (meta, puesto y tiempo solo si llegó; los
	 * títulos solo si tiene alguno) y el total al final. Vacío si no es válida.
	 */
	TORTUNABO_API FText CoopScoreBreakdown(const FTN_CoopScoreBreakdown& Score);

	/** Nombre de un título (TNEndTitleFlags: Saltarín, Tesorero o Curandero). */
	TORTUNABO_API FText TitleName(uint8 Flag);

	/** Título Saltarín (#798): «Saltarín: nombre (N saltos)». Vacío si nadie se lo ha llevado. */
	TORTUNABO_API FText JumperTitle(const FTN_EndTitle& Title);

	/** Título Tesorero (#873): «Tesorero: nombre (N chapas)». Vacío si nadie se lo ha llevado. */
	TORTUNABO_API FText TreasurerTitle(const FTN_EndTitle& Title);

	/** Título Curandero (#873): «Curandero: nombre (N curas)». Vacío si nadie se lo ha llevado. */
	TORTUNABO_API FText HealerTitle(const FTN_EndTitle& Title);

	/** Los tres títulos que se hayan dado, uno por línea. Vacío si ninguno. */
	TORTUNABO_API FText EndTitles(const FTN_EndTitle& Jumper, const FTN_EndTitle& Treasurer, const FTN_EndTitle& Healer);
}
