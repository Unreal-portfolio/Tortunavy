#pragma once

#include "CoreMinimal.h"
#include "World/TN_DirectInteractableBase.h"
#include "Core/TN_CosmeticsTypes.h"
#include "TN_ShopKeeper.generated.h"

class UAnimationAsset;
class UBoxComponent;
class UCapsuleComponent;
class UMaterialInterface;
class UPointLightComponent;
class USkeletalMeshComponent;
class UTextRenderComponent;
class UTN_MusicSynthComponent;

/**
 * Tienda del lobby: Don Tortugo, el tendero (una tortuga grande con su propio conjunto), detrás de un mostrador largo
 * con toldo de rayas y cartel. Detrás, una estantería con lo que se vende (cascos, caparazones, botes de pintura de los
 * colores y tarros de ojos); a los lados, un perchero con sombreros, un barril, cajas con conchas, el cofre y un farol.
 * Va pegada a la muralla (su -X). Al hablar con él se abre la tienda (UTN_ShopWidget) en el cliente que interactúa.
 *
 * Cuando el jugador local se acerca, se gira hacia él y le saluda (efecto local de cada cliente). ATN_HQGameMode la
 * coloca sola donde está el tendero de la maqueta si el nivel no tiene una puesta a mano (ver SpawnLobbyShops).
 * Mira a su +X: el mostrador queda delante.
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_ShopKeeper : public ATN_DirectInteractableBase
{
	GENERATED_BODY()

public:
	ATN_ShopKeeper();

	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostRegisterAllComponents() override;

	/** Delante del mostrador: desde ahí se habla con el tendero. */
	virtual FVector GetInteractionPoint() const override;

	/** Baja (o devuelve) la radio de los puestos mientras hay un menú de la tienda o del probador abierto. */
	static void SetRadiosDucked(UWorld* World, bool bDucked);
	virtual void Tick(float DeltaSeconds) override;

	FText GetKeeperName() const { return KeeperName; }
	FText GetShopName() const { return ShopName; }

protected:
	virtual void OnInteracted_Implementation(APawn* Interactor) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop")
	TObjectPtr<USkeletalMeshComponent> Keeper;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop")
	TObjectPtr<UStaticMeshComponent> KeeperHat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop")
	TObjectPtr<UCapsuleComponent> KeeperBlock;

	/** Mostrador, postes, toldo y cartel (malla en ejecución). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop")
	TObjectPtr<UStaticMeshComponent> Stall;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop")
	TObjectPtr<UBoxComponent> CounterBlock;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop")
	TObjectPtr<UTextRenderComponent> Sign;

	/** Luces del puesto: bajo el toldo (el tendero y el mostrador), sobre la estantería y la del farol del lado. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop")
	TObjectPtr<UPointLightComponent> CanopyLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop")
	TObjectPtr<UPointLightComponent> ShelfLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop")
	TObjectPtr<UPointLightComponent> LampLight;

	/** Radio del puesto: la música de la tienda en 3D (se crea en BeginPlay; no en servidor dedicado). */
	UPROPERTY(Transient)
	TObjectPtr<UTN_MusicSynthComponent> Radio;

	/** Volumen de la radio del puesto (0-1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Audio", meta = (ClampMin = "0.0", ClampMax = "1.5"))
	float RadioVolume = 0.8f;

	/** Lo que lleva puesto el tendero (filas de DT_Helmets y DT_Skins). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	FTN_TurtleLook KeeperLook;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	FText KeeperName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	FText ShopName;

	/** Escala del tendero (el jugador lleva 2.5). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	float KeeperScale = 3.4f;

	/**
	 * Tamaño del puesto (mostrador, toldo, cartel, estantería y adornos) sin agrandar al tendero. Por encima de 1, el
	 * mostrador tapa al tendero.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop", meta = (ClampMin = "0.5", ClampMax = "3.0"))
	float StallScale = 1.f;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> KeeperDefaults;

	UPROPERTY(Transient)
	TObjectPtr<UAnimationAsset> IdleAnim;

	UPROPERTY(Transient)
	TObjectPtr<UAnimationAsset> WaveAnim;

	float WaveCooldown = 0.f;
	float WaveTimeLeft = -1.f;
	float LookYaw = 0.f;
	/** Diferencia entre la malla del personaje de la tortuga y la de demo (TNTurtleArt::GetCopyCorrection). */
	FTransform KeeperCorrection = FTransform::Identity;

	/** Transformación del tendero: la de demo (mirando a los clientes, girado hacia el jugador) con KeeperCorrection. */
	FTransform KeeperTransform() const;

	void BuildStall();
	void HideBlockoutKeeper();

	/** Tendero vestido, puesto y cartel a su escala: en el editor (OnConstruction) y en ejecución. */
	void BuildVisuals();
};
