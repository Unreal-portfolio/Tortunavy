#pragma once

#include "CoreMinimal.h"
#include "World/TN_TctSceneryPlan.h"

/**
 * Las marcas de la orilla que tendrá el agua en la próxima subida de Todos contra Todos (#920): lógica pura, sin mundo ni
 * actores. En vez de un plano o un parpadeo del suelo, la cota del siguiente escalón se marca con cosas del propio mundo: una
 * línea de espuma y algas verdes sobre la arena que el agua va a cubrir y, de trecho en trecho, un poste con marcas y una
 * lámpara que se enciende al acercarse la subida (ATN_TctShoreWarning).
 */
namespace TNTctScenery
{
	/** Una marca de la orilla futura. */
	struct FShoreMark
	{
		/** 0 espuma, 1 algas, 2 poste. */
		uint8 Kind = 0;
		uint8 Variant = 0;
		FVector Location = FVector::ZeroVector;
		float YawDeg = 0.f;
		float Scale = 1.f;
	};

	inline constexpr uint8 ShoreKindFoam = 0;
	inline constexpr uint8 ShoreKindAlgae = 1;
	inline constexpr uint8 ShoreKindPost = 2;

	/** Cuánto por debajo de la cota del agua (uu) y cuánto por encima cuenta el suelo como «orilla futura». */
	inline constexpr float ShoreBandBelow = 32.f;
	inline constexpr float ShoreBandAbove = 6.f;

	/**
	 * Marcas de la orilla que tendrá el agua al llegar a TargetZ: espuma, algas y algún poste sobre el suelo medido que está a
	 * pocos centímetros por debajo de esa cota (ShoreBand*), sin llenar los KeepOuts. Ya mezcladas con la semilla: se enseñan
	 * las primeras N y el aviso va creciendo hasta la subida. Determinista: igual en todas las máquinas.
	 */
	TORTUNABO_API TArray<FShoreMark> PlanShoreMarks(const FHeightField& Field, float TargetZ, const TArray<FKeepOut>& KeepOuts, uint32 Seed,
		int32 MaxMarks = 520);
}
