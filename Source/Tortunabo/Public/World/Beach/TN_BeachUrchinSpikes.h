#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachCreatureRules.h"
#include "World/Beach/TN_BeachElement.h"
#include "World/Beach/TN_BeachTrapCommon.h"
#include "TN_BeachUrchinSpikes.generated.h"

class ACharacter;
class UNiagaraSystem;
class USoundBase;
class UStaticMeshComponent;

/**
 * Erizo enterrado (ETNBeachElement::UrchinSpikes, #687, Excel_DayT «Erizo bajo tierra»): de un erizo grande enterrado
 * solo asoman las puntas. Al pisarlo, tras un aviso muy corto (las puntas tiemblan), los pinchos salen de golpe:
 * derriban a quien esté encima (TNBeach::KnockDownTurtle) y le dejan una ralentización temporal en lugar del veneno.
 * Después recarga escondido y sin derribar (TNBeachCreatureRules::UrchinSpikes). Es una trampa fija: no comparte nada con
 * el erizo que rueda (ATN_BeachSeaUrchin).
 *
 * Red: el servidor decide cuándo salta (TriggerAt, hora del servidor, replicada) y a quién derriba; cada máquina anima
 * los pinchos y suena con esa hora y el reloj del servidor suavizado.
 */
UCLASS()
class TORTUNABO_API ATN_BeachUrchinSpikes : public ATN_BeachElement
{
	GENERATED_BODY()

public:
	ATN_BeachUrchinSpikes();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual float GetTickWakeDistance() const override { return 8000.f; }
	virtual bool IsTickBusy() const override;

	/** Derribo (s) y ralentización (fracción de la velocidad y segundos). */
	UPROPERTY(EditAnywhere, Category = "Erizo")
	float KnockSeconds = 1.1f;

	UPROPERTY(EditAnywhere, Category = "Erizo")
	float SlowFactor = 0.6f;

	UPROPERTY(EditAnywhere, Category = "Erizo")
	float SlowSeconds = 3.f;

	/** Tiempos del aviso, de los pinchos fuera y de la recarga. */
	UPROPERTY(EditAnywhere, Category = "Erizo")
	float TellSeconds = 0.15f;

	UPROPERTY(EditAnywhere, Category = "Erizo")
	float OutSeconds = 1.2f;

	UPROPERTY(EditAnywhere, Category = "Erizo")
	float RechargeSeconds = 3.f;

	/** Al saltar los pinchos, en cada máquina. */
	UPROPERTY(EditDefaultsOnly, Category = "Erizo|Feedback")
	TObjectPtr<USoundBase> SpringSound;

	UPROPERTY(EditDefaultsOnly, Category = "Erizo|Feedback")
	TObjectPtr<UNiagaraSystem> SpringVFX;

protected:
	virtual void ApplySpec() override;

	UFUNCTION()
	void OnRep_TriggerAt();

	/** Hora del servidor del último salto (< 0: nunca). */
	UPROPERTY(ReplicatedUsing = OnRep_TriggerAt)
	float TriggerAt = -1.f;

	UPROPERTY(VisibleAnywhere, Category = "Erizo")
	TObjectPtr<UStaticMeshComponent> MoundMesh;

	UPROPERTY(VisibleAnywhere, Category = "Erizo")
	TObjectPtr<UStaticMeshComponent> SpikesMesh;

private:
	TNBeachCreatureRules::UrchinSpikes::FTimes Times() const;
	void ServerTick(double Now);
	void VisualTick(float DeltaSeconds, double Now);
	/** Pies de la tortuga sobre los pinchos (en planta dentro de Reach y a ras de arena). */
	bool IsOnSpikes(const ACharacter* Turtle, double Reach) const;

	double Radius = 260.0;
	double SpikeHeight = 120.0;
	TSet<TWeakObjectPtr<ACharacter>> Struck;
	TNBeachCreatureRules::UrchinSpikes::EPhase ShownPhase = TNBeachCreatureRules::UrchinSpikes::EPhase::Hidden;
	FTNTrapClock Clock;
};
