// PlayerController de los karts: el del Rally (HUD, cámaras, voz, cosméticos) más el HUD de los karts (UTN_KartHUDWidget:
// objeto, kilómetros que quedan y peso de la artillera) y, en cada cliente, el aviso al servidor de que ya tiene la pista y
// la colisión del suelo de la generación del mapa (ATN_KartGameState::IsLocalTrackPlayable): el servidor no sienta a nadie
// hasta que todas lo tienen (ATN_KartGameMode).
#pragma once

#include "CoreMinimal.h"
#include "Rally/TN_RallyPlayerController.h"
#include "TN_KartPlayerController.generated.h"

class UTN_KartHUDWidget;

UCLASS()
class TORTUNABO_API ATN_KartPlayerController : public ATN_RallyPlayerController
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, Category = "Karts")
	TSubclassOf<UTN_KartHUDWidget> KartHUDClass;

	ATN_KartPlayerController();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PlayerTick(float DeltaTime) override;

private:
	/** Cliente → servidor: esta máquina tiene la pista y el suelo de la generación Generation del mapa. */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerReportKartTrackReady(int32 Generation);

	UPROPERTY(Transient)
	TObjectPtr<UTN_KartHUDWidget> KartHUD;

	/** Última generación avisada (0 = ninguna). */
	int32 ReportedGeneration = 0;
	float ReadyCheckAccumulator = 0.f;
};
