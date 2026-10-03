#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/TN_CosmeticsTypes.h"
#include "Player/TN_TurtleAnimInstance.h"
#include "TN_RacePodiumStage.generated.h"

class UDirectionalLightComponent;
class UMaterialInterface;
class UPointLightComponent;
class USceneCaptureComponent2D;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

/**
 * @brief Podio del modo carrera: un escenario 3D de verdad construido en código lejos del recorrido (1,5 km por encima
 * del mapa), con sus luces, que una SceneCapture2D pinta en una textura; la pantalla del campeón
 * (UTN_RaceChampionWidget) la enseña a la derecha como fondo animado.
 *
 * Todo a escala TNBeach::Scale (28 veces el tamaño real): la primera tortuga sobre un vaso de plástico boca abajo, la
 * segunda sobre una caja de zumo aplastada con su pajita y la tercera sentada en una chancla; alrededor, un tapón, una
 * lata tumbada, conchas y una estrella de mar en la arena; detrás, la orilla con espuma que va y viene, el mar hasta el
 * horizonte e islas con palmeras de 280 m. El cielo no se captura (queda transparente): lo pinta la pantalla detrás.
 *
 * Las tortugas llevan la malla del personaje de la tortuga (TNTurtleArt::ApplyBody, la de BP_TortugaCharacter) con su
 * aspecto (UTN_CosmeticLook: casco, caparazón, piel, ojos y las piezas de Arte) y la animación del jugador (UTN_TurtleAnimInstance) con las poses de celebración: Trofeo (la primera, que
 * levanta la concha con las dos manos: la concha va entre sus manos), Decepcionada (la segunda) y Pataleta (la tercera,
 * sentada). La cara (boca, ojos y colorete de M_TurtleBody) acompaña a cada pose.
 *
 * Es local (no se replica: cada máquina tiene el suyo) y solo lo ven sus capturas (bVisibleInSceneCaptureOnly) con el
 * canal de luz 2, así que ni se ve en el juego ni le llega el sol del nivel. Ajustes por consola: TN.Race.PodiumFOV y
 * TN.Race.PodiumDistance (encuadre).
 */
UCLASS(NotPlaceable, Transient)
class TORTUNABO_API ATN_RacePodiumStage : public AActor
{
	GENERATED_BODY()

public:
	ATN_RacePodiumStage();

	/** El podio de este mundo (lo crea la primera vez). */
	static ATN_RacePodiumStage* Get(UWorld* World);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Viste a las tortugas: Looks[0] la primera, [1] la segunda y [2] la tercera (las que falten no salen). */
	void SetPodium(const TArray<FTN_TurtleLook>& Looks);

	/** Captura cada fotograma (solo mientras se ve la pantalla del campeón). */
	void SetLive(bool bInLive);

	UTextureRenderTarget2D* GetRenderTarget() const { return Target; }

	/** Punto de la imagen (0..1) encima de la cabeza de la tortuga del puesto Place (0-2); false si no sale. */
	bool GetNameTagUV(int32 Place, FVector2D& OutUV) const;

	/** Relación de aspecto de la captura (ancho / alto). */
	static constexpr float CaptureAspect = 16.f / 9.f;

private:
	void BuildSet();
	void PlaceCamera();
	void TickFaces(float DeltaSeconds);
	void TickTrophy();

	UPROPERTY(VisibleAnywhere, Category = "Podium")
	TObjectPtr<USceneComponent> StageRoot;

	/** Arena, podio (vaso, caja de zumo, chancla) y decorado de la arena. */
	UPROPERTY(VisibleAnywhere, Category = "Podium")
	TObjectPtr<UStaticMeshComponent> SetMesh;

	UPROPERTY(VisibleAnywhere, Category = "Podium")
	TObjectPtr<UStaticMeshComponent> SeaMesh;

	/** Espuma de la orilla (va y viene). */
	UPROPERTY(VisibleAnywhere, Category = "Podium")
	TObjectPtr<UStaticMeshComponent> FoamMesh;

	/** Islas con palmeras en el horizonte. */
	UPROPERTY(VisibleAnywhere, Category = "Podium")
	TObjectPtr<UStaticMeshComponent> IslandMesh;

	UPROPERTY(VisibleAnywhere, Category = "Podium")
	TArray<TObjectPtr<USkeletalMeshComponent>> Turtles;

	UPROPERTY(VisibleAnywhere, Category = "Podium")
	TArray<TObjectPtr<UStaticMeshComponent>> Helmets;

	/** La concha que levanta la primera y su luz. */
	UPROPERTY(VisibleAnywhere, Category = "Podium")
	TObjectPtr<UStaticMeshComponent> TrophyMesh;

	UPROPERTY(VisibleAnywhere, Category = "Podium")
	TObjectPtr<UPointLightComponent> TrophyLight;

	UPROPERTY(VisibleAnywhere, Category = "Podium")
	TObjectPtr<UDirectionalLightComponent> SunLight;

	UPROPERTY(VisibleAnywhere, Category = "Podium")
	TObjectPtr<UPointLightComponent> KeyLight;

	UPROPERTY(VisibleAnywhere, Category = "Podium")
	TObjectPtr<UPointLightComponent> FillLight;

	UPROPERTY(VisibleAnywhere, Category = "Podium")
	TObjectPtr<UPointLightComponent> RimLight;

	UPROPERTY(VisibleAnywhere, Category = "Podium")
	TObjectPtr<USceneCaptureComponent2D> Capture;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> Target;

	/** Materiales originales de la malla de la tortuga (los mismos para las tres). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> DefaultMaterials;

	/** Puestos con tortuga y la pose que le toca a cada uno. */
	TArray<bool> bPlaceUsed;
	TArray<ETNTurtleCelebration> PlacePose;
	/** Parpadeo de cada tortuga: cuándo empieza el siguiente y cuánto lleva. */
	TArray<float> NextBlink;
	TArray<float> BlinkAge;

	float Time = 0.f;
	bool bLive = false;
	bool bBuilt = false;
};
