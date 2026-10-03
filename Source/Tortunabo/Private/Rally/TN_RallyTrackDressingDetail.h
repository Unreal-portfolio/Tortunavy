// Ayudas internas de las reglas puras del decorado del Rally (definidas en TN_RallyTrackDressingPlan.cpp) que comparte el
// reparto del decorado lejano (TN_RallyTrackDressingFarPlan.cpp).
#pragma once

#include "CoreMinimal.h"
#include "Rally/TN_RallyTrackDressing.h"

namespace TNRallyDressing::Detail
{
	/** Número estable en [0, 1) por semilla y dos índices: igual en todas las máquinas y sin depender del orden. */
	double Rand01(int32 Seed, int32 A, int32 B);

	/** Vector unitario en planta a la derecha de Direction. */
	FVector RightOf(const FVector& Direction);

	/** Cada cuántas muestras toca una pieza para PerKm piezas por kilómetro (0 = ninguna). */
	int32 StrideFor(const FTrackData& Track, double PerKm);

	/** True si una huella de RadiusCm en Location pisa alguna de Spots. */
	bool OverlapsSpots(const TArray<FSpot>& Spots, const FVector& Location, double RadiusCm);
}
