#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/TN_CosmeticsTypes.h"
#include "TN_CosmeticPreview.generated.h"

class APlayerController;
class UAnimationAsset;
class UMaterialInterface;
class UPointLightComponent;
class USceneCaptureComponent2D;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

/**
 * Escaparate de la tienda y del probador: la tortuga del jugador sobre una peana de arena que gira despacio, con luces
 * de estudio (key, relleno y contra) y una cámara que la pinta en una textura; la UI la enseña con M_UI_Preview.
 *
 * Vive lejos, en el cielo, y solo lo ven sus capturas (bVisibleInSceneCaptureOnly): las capturas no usan el sol ni el
 * cielo del nivel, así que se ve igual en cualquier mapa. También hace las miniaturas del catálogo (un cosmético sobre
 * la tortuga de serie). Es local, no se replica: cada jugador tiene el suyo (Get).
 */
UCLASS(NotPlaceable, Transient)
class TORTUNABO_API ATN_CosmeticPreview : public AActor
{
	GENERATED_BODY()

public:
	ATN_CosmeticPreview();

	/**
	 * El escaparate de este mundo (lo crea la primera vez). Con la pantalla partida (#311) hay uno por jugador local (Slot:
	 * su índice), cada uno lejos de los demás para que sus luces no se mezclen: dos jugadores pueden estar a la vez en la
	 * tienda o en el probador.
	 */
	static ATN_CosmeticPreview* Get(UWorld* World, int32 Slot = 0);

	/** El escaparate del jugador de PC (el 0 sin pantalla partida). */
	static ATN_CosmeticPreview* GetFor(const APlayerController* PC);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Viste a la tortuga del escaparate. */
	void SetLook(const FTN_TurtleLook& InLook);
	const FTN_TurtleLook& GetLook() const { return Look; }

	/** Pose: saludo al elegir algo; con bCelebrate, el grito de alegría de una compra. Luego vuelve a la espera. */
	void PlayPose(bool bCelebrate);

	/** Giro manual (arrastrar con el ratón); el giro automático vuelve a los pocos segundos. */
	void AddSpin(float Degrees);

	/** Captura en vivo (se pinta cada fotograma solo mientras hay un menú abierto: SetLiveCapture). */
	UTextureRenderTarget2D* GetRenderTarget() const { return Target; }
	void SetLiveCapture(bool bEnabled);

	/** Miniatura de un cosmético: la textura se devuelve al momento y se pinta en los fotogramas siguientes. */
	UTextureRenderTarget2D* GetThumbnail(ETNCosmeticCategory Category, FName Id);

private:
	/** Jugador local del escaparate (0: el primero; con la pantalla partida, uno por jugador). */
	int32 Slot = 0;

	struct FThumbRequest
	{
		ETNCosmeticCategory Category = ETNCosmeticCategory::Helmet;
		FName Id = NAME_None;
		TObjectPtr<UTextureRenderTarget2D> Target;
	};

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<USceneComponent> StageRoot;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<USceneComponent> Turntable;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UStaticMeshComponent> Pedestal;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<USkeletalMeshComponent> Turtle;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UStaticMeshComponent> Helmet;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<USceneCaptureComponent2D> Capture;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<USceneCaptureComponent2D> ThumbCapture;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UPointLightComponent> KeyLight;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UPointLightComponent> FillLight;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UPointLightComponent> RimLight;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> Target;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UTextureRenderTarget2D>> Thumbnails;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> DefaultMaterials;

	UPROPERTY(Transient)
	TObjectPtr<UAnimationAsset> IdleAnim;

	UPROPERTY(Transient)
	TObjectPtr<UAnimationAsset> SaluteAnim;

	UPROPERTY(Transient)
	TObjectPtr<UAnimationAsset> CheerAnim;

	TArray<FThumbRequest> PendingThumbs;
	FTN_TurtleLook Look;
	float SpinDeg = 0.f;
	float ManualSpinHold = 0.f;
	float PoseTimeLeft = -1.f;
	bool bLive = false;

	void BuildPedestal();
	void ApplyLookNow(const FTN_TurtleLook& InLook);
	void CaptureThumbnail(const FThumbRequest& Request);
};
