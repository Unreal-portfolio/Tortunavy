// Copiloto automático del Rally (#331) en la máquina de la conductora: si su buggy va sin artillera humana (va sola o con un
// bot), canta cada nota de copiloto unos 3 s antes de llegar (mínimo 60 m) con una señal sonora al lado de la curva y una
// placa breve en el salpicadero (UTN_RallyDashboardComponent). Con artillera humana no hace nada. Es cosmético y local: no
// replica nada ni toca el estado de la carrera; solo lee la pista, los puestos replicados y el buggy propio. Regla del
// momento de cantar en TNRallyCopilot (TN_RallyCopilotCalls.h). TN.Rally.Copilot 0/1/2 lo apaga, lo deja en automático o lo
// fuerza también en la plaza de artillera (para probarlo con ?BotDriver).
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Rally/TN_RallyCopilotCalls.h"
#include "Rally/TN_RallyPaceNotes.h"
#include "TimerManager.h"
#include "TN_RallyCopilotComponent.generated.h"

class APlayerController;
class ATN_Buggy;
class ATN_RallyGameState;
class ATN_RallyTrack;
class USoundAttenuation;
class USoundBase;

UCLASS(Transient)
class TORTUNABO_API UTN_RallyCopilotComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_RallyCopilotComponent();

	/**
	 * Señal sonora y placa de una nota en esta máquina, para las ocupantes de Buggy. La usa el copiloto automático y la
	 * usará la artillera al cantar notas (#330).
	 */
	void PresentCall(const TNRallyPaceNotes::FPaceNote& Note, ATN_Buggy* Buggy);

	/**
	 * Aviso rápido de la artillera (#330) en esta máquina: Signal centrada y, en el buggy de la conductora local, la placa
	 * con Headline y el borde en Accent.
	 */
	void PresentQuickCall(const FText& Headline, const TNRallyCopilot::FCallSignal& Signal, const FLinearColor& Accent, ATN_Buggy* Buggy);

	/** Notas cantadas desde que empezó (para las pruebas sin editor). */
	int32 GetCallCount() const { return CallCount; }

	/** Volumen de los pitidos. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Copiloto", meta = (ClampMin = "0"))
	float Volume = 0.8f;

	/** Distancia al lado de la cámara desde la que suena la señal de una curva (cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Copiloto", meta = (ClampMin = "0"))
	float SideOffsetCm = 250.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Copiloto")
	TObjectPtr<USoundBase> BeepSound;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Copiloto")
	TObjectPtr<USoundBase> CrestSound;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Copiloto")
	TObjectPtr<USoundBase> JumpSound;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Copiloto")
	TObjectPtr<USoundBase> WaterSound;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	APlayerController* GetLocalController() const;
	/** El buggy cuyas notas se cantan ahora en esta máquina, o nullptr si el copiloto no toca. */
	ATN_Buggy* FindCopilotBuggy(const ATN_RallyGameState& RallyState) const;
	bool SyncTrack(const ATN_RallyGameState& RallyState);
	bool FollowBuggy(const ATN_Buggy& Buggy);
	void CallNextNote(ATN_Buggy& Buggy);
	void ResetCalls();
	void PlaySignal(const TNRallyCopilot::FCallSignal& Signal);
	void PlayNextBeep();
	USoundBase* SoundFor(TNRallyCopilot::ECallSound Sound) const;

	/** Espacializa sin atenuar: la señal se oye entera, a la izquierda o a la derecha. */
	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> SideAttenuation;

	TWeakObjectPtr<const ATN_RallyTrack> NotesTrack;
	float NotesTrackLengthCm = 0.f;
	TNRallyPaceNotes::FTrackNotes TrackNotes;
	double TrackArcCm = 0.0;
	double NoteArcCm = 0.0;
	bool bHasArc = false;

	/** Arcos de las notas ya cantadas que siguen por delante. */
	TArray<double> CalledArcs;
	double SecondsSinceCall = TNRallyCopilot::MinSecondsBetweenCalls;
	int32 CallCount = 0;

	TNRallyCopilot::FCallSignal PendingSignal;
	int32 PendingBeeps = 0;
	FTimerHandle BeepTimer;
};
