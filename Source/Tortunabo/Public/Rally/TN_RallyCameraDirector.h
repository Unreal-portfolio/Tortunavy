// Cámara de llegada, podio y espectador del Rally (#306), en la máquina de cada jugador (cosmético, no se replica). Decide el
// plano con TNRallyCamera::DecideShot: al cruzar la meta, plano lateral a cámara lenta (1,5 s; la cámara lenta solo si no hay
// más jugadores) y el podio; después, o si se espera sin buggy, espectador entre los buggies que siguen corriendo y un dron
// que sigue al líder; en los resultados, el podio. Con VR (TNVR::KeepFirstPersonView) no hay cámara lenta ni travelling: corte
// seco al podio fijo y el espectador desde el asiento del buggy seguido, sin dron. Lo crea ATN_RallyPlayerController.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Rally/TN_RallyCameraLogic.h"
#include "TN_RallyCameraDirector.generated.h"

class ACameraActor;
class APawn;
class APlayerController;
class ATN_RallyGameState;
struct FTNRallyStanding;

UCLASS(ClassGroup = (Rally), Transient)
class TORTUNABO_API UTN_RallyCameraDirector : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_RallyCameraDirector();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Espectador: buggy anterior (-1) o siguiente (+1), con el dron al final de la lista (sin VR). */
	void CycleSpectate(int32 Delta);

	TNRallyCamera::EShot GetShot() const { return Shot; }

	/** Mirando la carrera (espectador) y a qué equipo; INDEX_NONE con el dron o sin espectador. */
	bool IsSpectating() const { return Shot == TNRallyCamera::EShot::Spectate && bHasSpectateTarget; }
	bool IsDroneView() const { return IsSpectating() && Spectated.bDrone; }
	int32 GetSpectatedTeam() const { return IsSpectating() && !Spectated.bDrone ? Spectated.Team : INDEX_NONE; }

private:
	APlayerController* GetPlayer() const;
	const FTNRallyStanding* FindMine(const ATN_RallyGameState& RallyState) const;
	/** Ve llegar el buggy propio (o lo encuentra ya en meta) y apunta la hora real. */
	void TrackFinish(const FTNRallyStanding* Mine, double RealNow);
	void SwitchShot(TNRallyCamera::EShot NewShot, const FTNRallyStanding* Mine);

	void ReturnToOwnView();
	void BeginFinishSide(const FTNRallyStanding* Mine);
	void UpdateFinishSide();
	void UpdatePodium(double RealNow);
	void UpdateSpectate(const ATN_RallyGameState& RallyState, float DeltaTime);
	/** Buggies que siguen corriendo, por puesto (sin los que llegaron ni los retirados). */
	TArray<const FTNRallyStanding*> RacingStandings(const ATN_RallyGameState& RallyState) const;
	static TArray<int32> TeamsOf(const TArray<const FTNRallyStanding*>& Racing);
	void ViewBuggy(APawn* Vehicle);
	void ViewDrone(const APawn& Leader, float DeltaTime);

	ACameraActor* EnsureCamera();
	/** Pone la cámara propia como vista (Blend en s; 0 = corte seco). */
	void ViewCamera(float BlendSeconds);
	void SetSlowMotion(bool bEnable);
	bool IsVR() const;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> Camera;

	TWeakObjectPtr<AActor> ViewedActor;
	TWeakObjectPtr<APawn> SideShotVehicle;
	TNRallyCamera::EShot Shot = TNRallyCamera::EShot::Own;
	/** La vista la lleva este componente (si no, la del peón). */
	bool bControlling = false;
	bool bSlowMotion = false;
	bool bSawStanding = false;
	double FinishSeenRealTime = -1.0;
	/** A quién mira el espectador (TNRallyCamera::FollowSpectate): hueco, equipo (para seguirlo si cambia el orden) y dron. */
	TNRallyCamera::FSpectatePick Spectated;
	bool bHasSpectateTarget = false;
	bool bCameraAttached = false;
};
