#pragma once

#include "CoreMinimal.h"
#include "TN_EndTitles.generated.h"

/**
 * Título de fin de partida (#798, #873, tabla «Títulos» de la hoja Puntuación del Excel de diseño): Saltarín (más
 * saltos), Tesorero (más chapas) y Curandero (más jugadoras curadas). Cada uno da puntos a quien se lo lleva
 * (FTN_EndScoreRules::PointsPerTitle). Lo decide el servidor al entrar en Results (ATN_CoopGameState) y se replica; lo
 * enseña la pantalla de resultados.
 */
USTRUCT(BlueprintType)
struct TORTUNABO_API FTN_EndTitle
{
	GENERATED_BODY()

	/** PlayerId de quien se lo lleva; INDEX_NONE si nadie (nadie ha saltado). */
	UPROPERTY(BlueprintReadOnly, Category = "Coop|Titles")
	int32 PlayerId = INDEX_NONE;

	/** Nombre en el momento de darlo (sigue saliendo aunque se vaya de la sala). */
	UPROPERTY(BlueprintReadOnly, Category = "Coop|Titles")
	FString PlayerName;

	/** La cifra que se lo ha dado (saltos, chapas o jugadoras curadas). */
	UPROPERTY(BlueprintReadOnly, Category = "Coop|Titles")
	int32 Count = 0;

	bool IsAwarded() const { return PlayerId != INDEX_NONE && Count > 0; }
};

namespace TNEndTitles
{
	/** Una jugadora y su cifra, en el orden de la sala (PlayerId de menor a mayor: quien entró antes, primero). */
	struct FEntry
	{
		int32 PlayerId = INDEX_NONE;
		int32 Count = 0;
	};

	/**
	 * Índice de quien más tiene (más de 0); en empate, la primera del orden recibido. INDEX_NONE si nadie pasa de 0.
	 */
	inline int32 PickTop(TConstArrayView<FEntry> Entries)
	{
		int32 Best = INDEX_NONE;
		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			if (Entries[i].Count > 0 && (Best == INDEX_NONE || Entries[i].Count > Entries[Best].Count))
			{
				Best = i;
			}
		}
		return Best;
	}
}
