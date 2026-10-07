#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Subsystems/WorldSubsystem.h"
#include "TN_AirdropSubsystem.generated.h"

class ATN_AirdropPoint;
class ATN_SupplyDrop;

/**
 * Ajustes del airdrop (#860), en Ajustes del proyecto > Tortunavy - Airdrop (Config/DefaultGame.ini). Cuándo cae y cómo:
 * el gestor del servidor (UTN_AirdropSubsystem) los lee al empezar cada partida.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Tortunavy - Airdrop"))
class TORTUNABO_API UTN_AirdropSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Si el gestor lanza airdrops solo (la consola puede forzarlos igual). */
	UPROPERTY(Config, EditAnywhere, Category = "Airdrop")
	bool bEnabled = true;

	/** Segundos desde que empieza la partida hasta el primer airdrop. */
	UPROPERTY(Config, EditAnywhere, Category = "Airdrop", meta = (ClampMin = "0.0"))
	float FirstDropSeconds = 90.f;

	/** Segundos entre un airdrop y el siguiente. */
	UPROPERTY(Config, EditAnywhere, Category = "Airdrop", meta = (ClampMin = "5.0"))
	float IntervalSeconds = 180.f;

	/** Airdrops por partida como mucho (los forzados por consola no cuentan). */
	UPROPERTY(Config, EditAnywhere, Category = "Airdrop", meta = (ClampMin = "0"))
	int32 MaxDrops = 3;

	/** Segundos de aviso (marca y haz en el suelo, cartel en el HUD) antes de que aparezca la caja. */
	UPROPERTY(Config, EditAnywhere, Category = "Airdrop", meta = (ClampMin = "0.0"))
	float WarnSeconds = 8.f;

	/** Segundos que tarda en caer (velocidad constante con paracaídas). */
	UPROPERTY(Config, EditAnywhere, Category = "Airdrop", meta = (ClampMin = "0.0"))
	float FallSeconds = 12.f;

	/** Altura (cm) desde la que cae, si el punto no dice otra. */
	UPROPERTY(Config, EditAnywhere, Category = "Airdrop", meta = (ClampMin = "100.0"))
	float DropHeight = 4000.f;

	/** Clase del airdrop (una hija en Blueprint con su arte, sonidos o tabla de botín); vacía = ATN_SupplyDrop. */
	UPROPERTY(Config, EditAnywhere, Category = "Airdrop")
	TSoftClassPtr<ATN_SupplyDrop> DropClass;
};

/** Reglas puras del gestor (sin mundo): las recorren las pruebas Tortunabo.Supply.Airdrop.*. */
namespace TNAirdropSchedule
{
	/** Si toca lanzar uno más: quedan airdrops (Launched < MaxDrops) y hay algún punto libre. */
	TORTUNABO_API bool CanLaunch(int32 Launched, int32 MaxDrops, int32 FreePoints);

	/**
	 * Índice del punto elegido con Roll en [0, 1) entre los libres (Busy[i] = ya tiene una caja sin abrir), evitando el
	 * último usado (LastIndex) si hay otro libre. INDEX_NONE si no hay ninguno libre.
	 */
	TORTUNABO_API int32 PickPoint(TConstArrayView<bool> Busy, int32 LastIndex, float Roll);
}

/**
 * Gestor del airdrop (#860), solo en el servidor: cuando empieza la partida, si hay puntos de airdrop en el mapa
 * (ATN_AirdropPoint), lanza el primero a los FirstDropSeconds y luego uno cada IntervalSeconds, hasta MaxDrops
 * (UTN_AirdropSettings). Cada vez elige al azar un punto sin otra caja por abrir. Sin puntos no hace nada (mapas sin
 * airdrop, lobby). Consola: TN.Airdrop.Force.
 */
UCLASS()
class TORTUNABO_API UTN_AirdropSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/**
	 * Servidor: lanza ya un airdrop en un punto libre al azar (no cuenta para MaxDrops). Sin puntos libres, en Fallback si
	 * se da. Null si no se ha podido.
	 */
	ATN_SupplyDrop* ForceDrop(const FVector* Fallback = nullptr);

	/** Servidor: lanza ya un airdrop sobre el suelo bajo Location (sin punto; no cuenta para MaxDrops). */
	ATN_SupplyDrop* LaunchAt(const FVector& Location);

	/** Airdrops lanzados solos en esta partida. */
	int32 GetLaunchedCount() const { return Launched; }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** Turno del temporizador: lanza uno si toca y quedan. */
	void OnTimer();

	/** Lanza en un punto libre al azar; null si no hay ninguno. */
	ATN_SupplyDrop* LaunchAtFreePoint();

	ATN_SupplyDrop* Launch(const FVector& Ground, float Height);

	/** Suelo bajo Location (o Location si no hay suelo a mano). */
	FVector GroundBelow(const FVector& Location) const;

	FTimerHandle Timer;
	int32 Launched = 0;
	int32 LastPoint = INDEX_NONE;

	/** Airdrops lanzados (para saber qué puntos tienen una caja sin abrir). */
	TArray<TWeakObjectPtr<ATN_SupplyDrop>> Drops;
};
