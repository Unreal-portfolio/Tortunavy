// Artillera de los karts (#295): la del buggy (ATN_BuggyGunnerPawn: cámara, torreta, disparo, cantos del copiloto) más los
// objetos (#304: los usa ella, con E, clic derecho o LT; con Q o LB, hacia atrás) y la inclinación (A/D o el stick
// izquierdo), que cambia cuánto gira el kart (ATN_KartBuggy::SetGunnerLean). Las peticiones salen de este peón por RPC
// validada (la artillera no es dueña del kart).
#pragma once

#include "CoreMinimal.h"
#include "Vehicles/TN_BuggyGunnerPawn.h"
#include "TN_KartGunnerPawn.generated.h"

class UTN_KartInputSet;
struct FInputActionValue;

UCLASS()
class TORTUNABO_API ATN_KartGunnerPawn : public ATN_BuggyGunnerPawn
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void NotifyControllerChanged() override;

	/** Inclinación local de la artillera en [-1, 1] (la suavizada que se manda al servidor). */
	float GetLocalLean() const { return Lean; }

	/** Rapidez con la que la inclinación sigue al mando (1/s) y envíos al servidor por segundo. */
	UPROPERTY(EditDefaultsOnly, Category = "Karts|Artillera")
	float LeanResponse = 8.f;

	UPROPERTY(EditDefaultsOnly, Category = "Karts|Artillera")
	float LeanSendRate = 12.f;

private:
	UTN_KartInputSet* GetKartInput();
	void OnUseItem(const FInputActionValue& Value);
	void OnBackwardPressed(const FInputActionValue& Value);
	void OnBackwardReleased(const FInputActionValue& Value);
	void OnLean(const FInputActionValue& Value);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerUseKartItem(bool bBackward);

	/** Inclinación en centésimas (-100..100). */
	UFUNCTION(Server, Unreliable, WithValidation)
	void ServerSetLean(int8 LeanQ);

	UPROPERTY(Transient)
	TObjectPtr<UTN_KartInputSet> KartInput;

	float LeanInput = 0.f;
	float Lean = 0.f;
	float LeanSendAccumulator = 0.f;
	int8 LastSentLean = 0;
	bool bBackwardHeld = false;
};
