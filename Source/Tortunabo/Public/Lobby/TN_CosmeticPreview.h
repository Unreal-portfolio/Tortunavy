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
class UTN_BuggyLookComponent;

/**
 * Escaparate de la tienda y del probador: la tortuga del jugador sobre una peana de arena que gira despacio, con luces
 * de estudio (key, relleno y contra) y una cámara que la pinta en una textura; la UI la enseña con M_UI_Preview.
 *
 * Vive lejos, en el cielo, y solo lo ven sus capturas (bVisibleInSceneCaptureOnly): las capturas no usan el sol ni el
 * cielo del nivel, así que se ve igual en cualquier mapa. También hace las miniaturas del catálogo (un cosmético sobre
 * la tortuga de serie). Es local, no se replica: cada jugador tiene el suyo (Get).
 *
 * Modo buggy (pestaña y página del buggy del Rally): sobre la peana, el buggy de tortuga en miniatura con la tortuga
 * del jugador al volante; las miniaturas de modelos y pinturas salen del mismo buggy.
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

	/** Enseña el buggy (con la tortuga al volante) en vez de la tortuga sola. */
	void SetBuggyMode(bool bInBuggyMode);
	bool IsBuggyMode() const { return bBuggyMode; }

	/** Modelo y pintura del buggy del escaparate. */
	void SetBuggyLook(const FTN_BuggyLook& InLook);
	const FTN_BuggyLook& GetBuggyLook() const { return BuggyLookState; }

#if !UE_BUILD_SHIPPING
	/** Pruebas (TN.Buggy.Photos): captura el escaparate, girado Yaw grados, en un PNG de Size píxeles. */
	bool DebugSavePhoto(const FString& File, int32 Size, float Yaw);
	/** Pruebas: alguna pieza del buggy del escaparate aún precarga sus PSO (se vería con el material por defecto). */
	bool DebugIsBuggyPrecaching() const;
	/**
	 * Pruebas: pide las miniaturas de todos los modelos y pinturas del buggy y, cuando están pintadas, las guarda en
	 * Dir como PNG. Devuelve true cuando ha acabado.
	 */
	bool DebugSaveBuggyThumbs(const FString& Dir);
#endif

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

	/** Buggy en miniatura (escala BuggyScale) sobre la peana. */
	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<USceneComponent> BuggyRoot;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UTN_BuggyLookComponent> Buggy;

	/**
	 * Otro buggy, solo para las miniaturas (no sale en la captura en vivo): se viste con cada modelo o pintura y espera
	 * a que sus PSO estén listos antes de capturar (con la precarga de PSO, una malla recién hecha sale los primeros
	 * fotogramas con el material por defecto, gris).
	 */
	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<USceneComponent> ThumbBuggyRoot;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UTN_BuggyLookComponent> ThumbBuggy;

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
	/** Miniaturas del buggy: de una en una, con el buggy de miniaturas ya vestido y sus PSO listos. */
	TArray<FThumbRequest> PendingBuggyThumbs;
	int32 BuggyThumbFrames = 0;
	/** La miniatura del buggy ya se ha capturado una vez con su cámara (ver TickBuggyThumbs). */
	bool bBuggyThumbPrimed = false;
	FTN_TurtleLook Look;
	FTN_BuggyLook BuggyLookState;
	bool bBuggyMode = false;
	float SpinDeg = 0.f;
	float ManualSpinHold = 0.f;
	float PoseTimeLeft = -1.f;
	bool bLive = false;

	void BuildPedestal();
	void ApplyLookNow(const FTN_TurtleLook& InLook);
	/** Coloca la tortuga en la peana o al volante, la cámara y lo que ven las capturas según el modo. */
	void ApplyMode(bool bBuggy);
	/** Primitivas del buggy (para las listas de las capturas). */
	void BuggyPrimitives(TArray<UPrimitiveComponent*>& Out) const;
	void CaptureThumbnail(const FThumbRequest& Request);
	void CaptureBuggyThumbnail(const FThumbRequest& Request);
	/** Sigue las miniaturas del buggy pendientes (una cada vez que el buggy de miniaturas está listo). */
	void TickBuggyThumbs();
};
