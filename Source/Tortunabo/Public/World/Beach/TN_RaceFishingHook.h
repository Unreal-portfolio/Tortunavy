#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_RaceItemActor.h"
#include "TN_RaceFishingHook.generated.h"

class ATortugaCharacter;
class UStaticMeshComponent;

namespace TNRaceFishingHook
{
	/** Fases del anzuelo (ATN_RaceFishingHook::Phase). */
	constexpr uint8 PhaseFlying = 0;
	constexpr uint8 PhaseTowing = 1;
	constexpr uint8 PhaseSnapped = 2;
	/** Anzuelos a la vez en la playa como mucho. */
	constexpr int32 MaxInWorld = 6;
}

/**
 * Caña de pescar (objeto de carrera ETNRaceItem::CanaPescar, #786). La tortuga lanza el anzuelo a la tortuga de delante más
 * cercana a 25 m o menos (TNRaceItemRules::PickRodTarget; sin nadie, el objeto no se gasta). El anzuelo tarda 0,35 s en
 * llegar; si engancha, el servidor remolca a la pescadora hacia ella durante 1,5 s por el aire (un solo
 * UTN_TurtleMovementComponent::LaunchFromServer calculado para caer un poco por delante de la enganchada, sin correcciones
 * al dueño) y la suelta al acabar. El protector solar (y el vuelo en el pelícano) lo anulan: el anzuelo rebota y cae.
 *
 * Red: lo replicado es quién pesca (OwnerTurtle), a quién (Target), la fase y su hora del servidor; el sedal, el anzuelo y
 * la caña se colocan en cada máquina a partir de las dos tortugas, sin posiciones por la red.
 */
UCLASS()
class TORTUNABO_API ATN_RaceFishingHook : public ATN_RaceItemActor
{
	GENERATED_BODY()

public:
	ATN_RaceFishingHook();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * Servidor: Fisher lanza el anzuelo (ya validado que puede usar el objeto). true si lo ha lanzado (entonces el uso gasta
	 * el objeto); false sin crear nada si no hay ninguna tortuga por delante a su alcance o si ya hay demasiados anzuelos.
	 */
	static bool ServerCast(ATortugaCharacter* Fisher);

protected:
	virtual void ServerTick(float DeltaSeconds) override;
	virtual void BuildVisuals() override;
	virtual void VisualTick(float DeltaSeconds) override;
	virtual void OnFinished() override;
	virtual bool UsesTrack() const override { return false; }

private:
	/** La tortuga enganchada (o a la que se lanzó). */
	UPROPERTY(Replicated)
	TObjectPtr<ATortugaCharacter> Target = nullptr;

	/** 0 el anzuelo vuela, 1 remolca, 2 se ha soltado o rebotado (TNRaceFishingHook::Phase*). */
	UPROPERTY(ReplicatedUsing = OnRep_Phase)
	uint8 Phase = 0;

	/** Hora del servidor en que empezó la fase. */
	UPROPERTY(Replicated)
	float PhaseTime = 0.f;

	UFUNCTION()
	void OnRep_Phase();

	/** Servidor: cambia de fase con su hora. */
	void ServerSetPhase(uint8 NewPhase);

	/** Servidor: el anzuelo llega; remolca a la pescadora o rebota. */
	void ServerHookArrives();

	/** Punta de la caña y punto de la espalda de la enganchada (mundo, esta máquina). */
	FVector RodTip() const;
	FVector TargetBack() const;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> RodMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> LineMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> HookMesh;

	/** Fase de la que ya ha sonado su efecto en esta máquina. */
	uint8 PlayedPhase = 255;

	/** Dónde estaba el anzuelo al soltarse (para dejarlo caer). */
	FVector SnapFrom = FVector::ZeroVector;
};
