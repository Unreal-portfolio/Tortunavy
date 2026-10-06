#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraModifier.h"
#include "Camera/CameraTypes.h"
#include "TN_GhostCameraModifier.generated.h"

class APawn;
class ATN_SpectatorGhost;

/**
 * Cámaras del fantasma espectador (Docs/Fantasma_Espectador.md), en el PlayerCameraManager del jugador local mientras
 * es fantasma. El ViewTarget sigue siendo la tortuga seguida (como antes: el HUD y el cambio de jugador lo usan), así
 * que la vista «fija» es la cámara de esa tortuga tal cual, con la rotación de su jugador; la «libre» la sustituye por
 * una órbita propia alrededor de la tortuga (ratón o stick derecho; rueda o gatillos para acercar), que no atraviesa el
 * suelo ni las paredes. Se pasa de una a otra con una mezcla suave. Volviendo a la vida, la cámara se aparta un poco y
 * mira el vuelo del fantasma hasta el huevo (luego la tapa la cáscara oscura). Si un gusano de arena se come a la tortuga
 * seguida (fin de la carrera), cualquiera de las dos pasa, con otra mezcla, a la vista lejana de la escena que ve ella
 * (ATN_BeachSandWorm::GetSpectatorView), con el gusano entero.
 *
 * Con gafas (#646; Docs/Modo_VR.md): ni fija ni libre. La posición sigue a la tortuga seguida, sin retardo, y la base de la
 * vista mira a un rumbo fijo (el de la cámara de esa tortuga al empezar a mirarla): solo gira la cabeza del jugador. El brazo
 * de cámara de la otra tortuga llegaba tarde y giraba cuando giraba ese jugador, y mareaba. Sin gafas y simulado no cambia.
 */
UCLASS()
class TORTUNABO_API UTN_GhostCameraModifier : public UCameraModifier
{
	GENERATED_BODY()

public:
	UTN_GhostCameraModifier();

	virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override;

	void SetGhost(ATN_SpectatorGhost* InGhost);

	/** Órbita de la cámara libre (grados: giro y cabeceo, positivo = mirar arriba). */
	void AddOrbitInput(float YawDegrees, float PitchDegrees);

	/** Acercar (positivo) o alejar la cámara libre, en «pasos» de rueda. */
	void AddZoom(float Steps);

	void ToggleFree();
	bool IsFree() const { return bFree; }

	/** ¿Puso este fotograma la vista de VR del fantasma (con gafas)? El rig la coloca entonces donde esta vista, sin el brazo de la otra tortuga. */
	bool IsVRViewActive() const { return bVRViewActive; }

	/** Vista que dejó el último fotograma (el fantasma flota ahí). */
	bool HasLastView() const { return bHasLastView; }
	const FMinimalViewInfo& GetLastView() const { return LastView; }

private:
	TWeakObjectPtr<ATN_SpectatorGhost> Ghost;

	bool bFree = true;
	/** 0 = fija, 1 = libre (mezcla animada). */
	float FreeBlend = 1.f;

	bool bOrbitReady = false;
	float OrbitYaw = 0.f;
	float OrbitPitch = -15.f;
	float Distance = 420.f;
	float TargetDistance = 420.f;

	bool bFocusReady = false;
	FVector Focus = FVector::ZeroVector;

	/** Resultado del fotograma (con un cambio de ViewTarget en curso se llama dos veces por fotograma). */
	uint64 LastFrame = 0;
	bool bOverrodeThisFrame = false;
	FMinimalViewInfo FrameView;

	bool bHasLastView = false;
	FMinimalViewInfo LastView;

	/** Con gafas (#646): rumbo fijo de la vista (el de la cámara seguida al empezar) y si este fotograma se puso la vista de VR. */
	bool bVRYawReady = false;
	float VRYaw = 0.f;
	bool bVRViewActive = false;

	/** Vista lejana del gusano que se come a la seguida: mezcla (0-1) y la última vista que dio. */
	float WormBlend = 0.f;
	FVector WormLocation = FVector::ZeroVector;
	FRotator WormRotation = FRotator::ZeroRotator;

	/** Volviendo a la vida: desde dónde mira y hacia dónde. */
	bool bReviveViewReady = false;
	FMinimalViewInfo ReviveFrom;
	FVector ReviveLookAt = FVector::ZeroVector;
	float ReviveElapsed = 0.f;

	/** Tortuga que se sigue ahora (el ViewTarget, si es una tortuga de otro jugador). */
	APawn* GetFollowedPawn() const;

	/** La cámara de From a To sin atravesar el suelo ni las paredes. */
	FVector ResolveCollision(const FVector& From, const FVector& To, const AActor* IgnoreActor) const;
};
