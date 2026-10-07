#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "TN_EconomySettings.generated.h"

/**
 * @brief Economía de la partida (plan maestro del modo único §4, hoja Economía del Excel): las chapas.
 *
 * Es dato, no código: se edita sin recompilar en Config/DefaultGame.ini ([/Script/Tortunabo.TN_EconomySettings]) o en Ajustes
 * del proyecto > Tortunavy - Economía. Lo leen el servidor (cobros) y los clientes (lo que se enseña): el .ini empaquetado es
 * el mismo en ambos.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Tortunavy - Economía"))
class TORTUNABO_API UTN_EconomySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** @brief Ajustes vigentes (el CDO, con lo que diga el .ini). Nunca nulo. */
	static const UTN_EconomySettings& Get() { return *GetDefault<UTN_EconomySettings>(); }

	/** Crédito que da cada chapa al entrar por la ranura de una máquina (precio pivote de la hoja Economía). */
	UPROPERTY(Config, EditAnywhere, Category = "Chapas", meta = (ClampMin = "1"))
	int32 ChapaValue = 1;

	/** Chapas que puede llevar una tortuga como mucho (la hoja dice «apilable», sin tope: este solo evita desbordes). */
	UPROPERTY(Config, EditAnywhere, Category = "Chapas", meta = (ClampMin = "1"))
	int32 MaxChapas = 99;
};
