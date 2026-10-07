#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "Core/TN_CoopScore.h"
#include "Lobby/TN_MysteryBox.h"
#include "TN_PointsEconomy.generated.h"

/**
 * Economía de puntos (#873): la fórmula de los puntos de final de partida, el tiempo objetivo de cada nivel y la caja
 * sorpresa de la tienda. Un solo asset (DA_PointsEconomy en /Game/Blueprints/Gameplay/Economy) que apunta
 * UTN_EconomySettings. Los precios sueltos de las skins y su rareza son columnas de DT_Skins.
 */
UCLASS(BlueprintType)
class TORTUNABO_API UTN_PointsEconomy : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puntuación")
	FTN_EndScoreRules Score;

	/**
	 * Tiempo objetivo (segundos) por nivel, con el nombre corto del mapa (LVL_Demo01). Un punto por cada
	 * Score.SecondsPerTimePoint por debajo, hasta Score.MaxTimePoints.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puntuación")
	TMap<FName, float> LevelTargetSeconds;

	/** Tiempo objetivo de los niveles que no están en LevelTargetSeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puntuación", meta = (ClampMin = "0.0"))
	float DefaultTargetSeconds = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tienda")
	FTN_MysteryBoxRules MysteryBox;

	/** Tiempo objetivo del mapa MapName (sin prefijo de PIE). */
	float GetTargetSeconds(FName MapName) const;

	/** El asset de UTN_EconomySettings; si no carga, los valores de la decisión (el objeto por defecto de la clase). */
	static const UTN_PointsEconomy& Get();
};

/** Dónde está la economía de puntos (Ajustes del proyecto > Tortunavy > Economía; [/Script/Tortunabo.TN_EconomySettings]). */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Economía"))
class TORTUNABO_API UTN_EconomySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UTN_EconomySettings();

	/** DA_PointsEconomy. Ruta blanda: el objeto de los ajustes no retiene assets; UTN_PointsEconomy::Get lo carga. */
	UPROPERTY(Config, EditAnywhere, Category = "Economía")
	TSoftObjectPtr<UTN_PointsEconomy> PointsEconomy;
};
