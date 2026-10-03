#pragma once

#include "CoreMinimal.h"
#include "World/TN_DirectInteractableBase.h"
#include "TN_SkinStatueActor.generated.h"

class UAnimationAsset;
class UBoxComponent;
class UMaterialInterface;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMeshComponent;

/**
 * Tipo de cosmético que gestiona esta estatua.
 * Añade nuevas entradas aquí para soportar futuros accesorios.
 */
UENUM(BlueprintType)
enum class ETNCosmeticType : uint8
{
	Skin   UMETA(DisplayName = "Skin de Personaje"),
	Helmet UMETA(DisplayName = "Sombrero / Accesorio"),
};

/**
 * ATN_SkinStatueActor — Estatua de cosmético en el lobby.
 *
 * Una sola clase para todos los tipos de cosmético (skins, sombreros, futuros accesorios).
 * Coloca instancias Blueprint en LVL_HQ y configura:
 *   - CosmeticType : qué tipo de cosmético aplica
 *   - CosmeticId   : ID que debe coincidir con una fila del DataTable correspondiente
 *                    (NAME_None = estatua especial "quitar cosmético")
 *
 * Al interactuar:
 *   - Si el jugador ya lleva este cosmético → lo desequipa.
 *   - Si lleva otro o ninguno → equipa este.
 *
 * El cambio persiste en MP_GameInstance y sobrevive a los viajes entre mapas.
 *
 * Setup rápido en el editor:
 *   1. Crear BP basado en este actor (BP_Statue_<ID>).
 *   2. Asignar CosmeticType y CosmeticId.
 *   3. Asignar PreviewMesh con el turtle mostrando el cosmético (visual de la estatua).
 *   4. Colocar en LVL_HQ.
 */
UCLASS()
class TORTUNABO_API ATN_SkinStatueActor : public ATN_DirectInteractableBase
{
	GENERATED_BODY()

public:
	ATN_SkinStatueActor();

	virtual void BeginPlay() override;
	virtual void Interact(APawn* Interactor) override;

	/** Tipo de cosmético que gestiona esta estatua. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmetic Statue")
	ETNCosmeticType CosmeticType = ETNCosmeticType::Skin;

	/**
	 * ID del cosmético que aplica esta estatua.
	 * Skin   → RowName en DT_Skins
	 * Helmet → RowName en DT_Helmets
	 * NAME_None → estatua "quitar cosmético" (restaura aspecto por defecto).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmetic Statue")
	FName CosmeticId = NAME_None;

	/**
	 * Mesh preview del personaje mostrando el cosmético.
	 * Sirve de visual de la estatua en el lobby.
	 * Para skins: asigna el skeletal mesh de la tortuga aquí y se le aplicará el material.
	 * Para cascos: puede quedar vacío; el mesh del casco se coloca en HelmetPreviewComp.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cosmetic Statue")
	TObjectPtr<USkeletalMeshComponent> PreviewMesh;

	/**
	 * Punto de anclaje del sombrero en la estatua.
	 * Colócalo sobre la cabeza del personaje en el Blueprint para que el casco
	 * aparezca en la posición correcta (igual que el SceneComponent "Sombrero" del personaje).
	 * HelmetPreviewComp se adjunta a este componente.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cosmetic Statue")
	TObjectPtr<USceneComponent> SombreroSocket;

	/**
	 * Componente donde se muestra el mesh 3D del casco/accesorio en la estatua.
	 * Se posiciona automáticamente en BeginPlay usando los datos de DT_Helmets.
	 * Adjunto a SombreroSocket → posiciona SombreroSocket en el BP para ajustar el origen.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cosmetic Statue")
	TObjectPtr<UStaticMeshComponent> HelmetPreviewComp;

protected:
	// ── Estatua de código (#50): si PreviewMesh está vacío y el Blueprint solo trae marcadores del motor ──────────

	/**
	 * Tortuga de la estatua, vestida con el cosmético de la estatua como la del jugador (UTN_CosmeticLook). Vacía (lo normal):
	 * la del personaje (TNTurtleArt, #581), así que cambia con la malla de BP_TortugaCharacter.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmetic Statue|Art")
	TSoftObjectPtr<USkeletalMesh> StatueTurtleMesh;

	/** Animación de la que sale la pose fija y el segundo de la pose. Vacía: el saludo de los ajustes de arte (TNTurtleArt). */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmetic Statue|Art")
	TSoftObjectPtr<UAnimationAsset> StatuePose;

	UPROPERTY(EditDefaultsOnly, Category = "Cosmetic Statue|Art", meta = (ClampMin = "0.0"))
	float StatuePoseSeconds = 1.f;

	/** Escala de la tortuga: TotugaDemo_Rig mide 53 cm a escala 1 y el personaje la lleva a 2,5 (133 cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmetic Statue|Art", meta = (ClampMin = "0.5", ClampMax = "5.0"))
	float StatueTurtleScale = 4.f;

	/** Peana octogonal de piedra: radio y alto (cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmetic Statue|Art", meta = (ClampMin = "30.0"))
	float PedestalRadius = 120.f;

	UPROPERTY(EditDefaultsOnly, Category = "Cosmetic Statue|Art", meta = (ClampMin = "10.0"))
	float PedestalHeight = 70.f;

private:
	void ApplyPreviewCosmetic();

	/** Monta la tortuga en su peana donde estaban los marcadores y los oculta. False si no hacía falta o no se pudo. */
	bool BuildCodeStatue();

	/** Peana con su caja de colisión (la que bloquea y la que encuentra el escaneo de interacción). */
	void BuildPedestal(const FVector& GroundCenter);

	/** Pose fija: la animación parada en StatuePoseSeconds. */
	void FreezePose();

	/** Viste la tortuga con el cosmético de la estatua (casco o color). */
	void DressCodeStatue();

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> PedestalComp;

	UPROPERTY(Transient)
	TObjectPtr<UBoxComponent> StatueBlocker;

	/** Materiales originales de la tortuga (ApplyLook parte de ellos). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> StatueDefaultMaterials;

	bool bCodeStatue = false;
};
