#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Game/TN_VendingStock.h"
#include "TN_EconomySettings.generated.h"

/**
 * @brief Economía de la partida (plan maestro del modo único §4, hoja Economía del Excel): las chapas, las máquinas
 * expendedoras y revivir pagando.
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

	/** Crédito que guarda una máquina por jugador como mucho. */
	UPROPERTY(Config, EditAnywhere, Category = "Máquina expendedora", meta = (ClampMin = "1"))
	int32 MaxVendingCredit = 99;

	/** Segundos que hay que mantener la tecla de interactuar en la máquina para comprar (pulsarla menos cambia de objeto). */
	UPROPERTY(Config, EditAnywhere, Category = "Máquina expendedora", meta = (ClampMin = "0.2", Units = "Seconds"))
	float VendingBuyHoldSeconds = 0.8f;

	/** Lo que vende una máquina sin lista propia (ATN_VendingMachine::Stock vacío): 4 objetos con su precio en chapas. */
	UPROPERTY(Config, EditAnywhere, Category = "Máquina expendedora")
	TArray<FTNVendingOffer> DefaultVendingOffers;

	/**
	 * Chapas que cuesta revivir a una compañera muerta junto a su cuerpo (hoja Economía: 4), manteniendo la tecla de
	 * interactuar en el rescate (ATN_RescuePickup). 0 = el rescate gratis de pulsar, como antes.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Revivir", meta = (ClampMin = "0"))
	int32 ReviveChapaCost = 4;

	/** Segundos que hay que mantener la tecla junto al cuerpo para revivir pagando. */
	UPROPERTY(Config, EditAnywhere, Category = "Revivir", meta = (ClampMin = "0.2", Units = "Seconds"))
	float ReviveHoldSeconds = 2.f;
};
