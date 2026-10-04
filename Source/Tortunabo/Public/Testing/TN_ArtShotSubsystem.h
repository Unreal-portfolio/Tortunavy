#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TN_ArtShotSubsystem.generated.h"

class ACameraActor;

/**
 * Capturas de arte para revisar a ojo los objetos sustituidos (#49, #50), sin abrir el editor:
 *   -game <mapa> -TNArtShots=<clase>[;<clase>...] [-TNArtShotsOut=<carpeta>] [-TNArtShotsAt=X:Y] [-TNArtShotsWarmup=s]
 *   [-TNQuitWhenDone]
 * Por cada clase (ruta completa: /Game/.../BP_X.BP_X_C) encuadra el primer ejemplar del mapa o, si no hay, uno nuevo
 * delante del jugador (con X:Y, el más cercano a ese punto o uno nuevo allí); guarda <carpeta>/<clase>.png y deja en el log los marcadores del motor que se siguen viendo.
 * Solo fuera de Shipping.
 */
UCLASS()
class TORTUNABO_API UTN_ArtShotSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UTN_ArtShotSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return bActive && !IsTemplate(); }

private:
	/** Encuadra la clase Index (la busca o la crea) con la cámara de capturas. */
	bool FrameShot(int32 Index);

	/** El ejemplar que se fotografía: el primero del mapa o uno nuevo delante del jugador. */
	AActor* FindOrSpawn(UClass* Class);

	void TakeShot(int32 Index);
	void Finish();

	TArray<FString> ClassPaths;
	FString OutDir;
	/** -TNArtShotsAt=X:Y: un claro del mapa donde nada tapa la cámara (la altura la da el suelo). */
	TOptional<FVector2D> SpawnAt;
	bool bActive = false;
	bool bQuitWhenDone = false;
	int32 Current = INDEX_NONE;
	/** Segundos hasta el siguiente paso (calentamiento, encuadre o captura). */
	float Countdown = 0.f;
	bool bFramed = false;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> Camera;

	UPROPERTY(Transient)
	TObjectPtr<AActor> Subject;
};
