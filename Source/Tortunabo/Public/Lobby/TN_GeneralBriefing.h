#pragma once

#include "CoreMinimal.h"
#include "World/TN_DirectInteractableBase.h"
#include "Core/TN_CosmeticsTypes.h"
#include "World/ProcMap/TN_ProcMapEnums.h"
#include "TN_GeneralBriefing.generated.h"

class UAnimationAsset;
class UBoxComponent;
class UCapsuleComponent;
class UMaterialInterface;
class UPointLightComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * El general del cuartel: el General Galápago (una tortuga grande con gorra de capitán) detrás de una mesa de madera
 * con una maqueta del castillo de arena del lobby hecha en código (murallas, torres, puerta, huevos, tienda, botellas y
 * banderitas). Al hablar con él se abre la sesión informativa (UTN_BriefingWidget) en el cliente que interactúa:
 * «Misión» (el anfitrión elige ahí el modo y la dificultad de la próxima partida), «Cómo se juega», «Modos de juego»,
 * «Reglas» y «Controles» (con las teclas reales de Enhanced Input).
 *
 * La misión (TNLobbyMission) vive en la GameInstance del anfitrión; el general la replica (MissionMode,
 * MissionDifficulty) para que todos la vean al momento en el diálogo y en la pizarra que tiene junto a la mesa.
 *
 * Como el tendero: se gira hacia el jugador local cuando se acerca y le saluda (efecto local). ATN_HQGameMode lo coloca
 * donde está el general de la maqueta (y esconde esa tortuga y su mesa) si el nivel no tiene uno puesto a mano.
 * Mira a su +X: la mesa queda delante.
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_GeneralBriefing : public ATN_DirectInteractableBase
{
	GENERATED_BODY()

public:
	ATN_GeneralBriefing();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostRegisterAllComponents() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Delante de la mesa: desde ahí se habla con el general. */
	virtual FVector GetInteractionPoint() const override;

	FText GetGeneralName() const { return GeneralName; }
	FText GetHeadquartersName() const { return HeadquartersName; }

	/** Modo de la próxima partida tal como llega a esta máquina (réplica de la GameInstance del anfitrión). */
	ETNProcGameMode GetMissionMode() const { return MissionMode; }

	/** Dificultad de la próxima partida tal como llega a esta máquina. */
	ETNProcDifficulty GetMissionDifficulty() const { return MissionDifficulty; }

	/**
	 * Mapa de la misión (TNLobbyMission::GetHostMissionMap: la arena en Todos contra Todos) de la próxima partida, tal como
	 * llega a esta máquina.
	 */
	FName GetMissionMapVariant() const { return MissionMapVariant; }

	/** Servidor: copia la misión de la GameInstance del anfitrión y la replica (solo si ha cambiado). */
	void SyncMissionFromGameInstance();

protected:
	virtual void OnInteracted_Implementation(APawn* Interactor) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "General")
	TObjectPtr<USkeletalMeshComponent> General;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "General")
	TObjectPtr<UStaticMeshComponent> GeneralHat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "General")
	TObjectPtr<UCapsuleComponent> GeneralBlock;

	/** Mesa, maqueta del castillo, mástil con bandera y cartel (malla en ejecución). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "General")
	TObjectPtr<UStaticMeshComponent> Table;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "General")
	TObjectPtr<UBoxComponent> TableBlock;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "General")
	TObjectPtr<UTextRenderComponent> Sign;

	/** Pone el nombre del cuartel en el cartel y lo encoge para que quepa en el tablero. */
	void FitSignText();

	/** Pizarra en su caballete junto a la mesa, con la orden del día: el modo y la dificultad de la próxima partida. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "General")
	TObjectPtr<UTextRenderComponent> MissionBoard;

	/** Luz del farol colgado dentro de la tienda militar. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "General")
	TObjectPtr<UPointLightComponent> TentLight;

	/** Lo que lleva puesto el general (filas de DT_Helmets y DT_Skins). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "General")
	FTN_TurtleLook GeneralLook;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "General")
	FText GeneralName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "General")
	FText HeadquartersName;

	/** Escala del general (el jugador lleva 2,5). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "General")
	float GeneralScale = 3.4f;

private:
	UPROPERTY(ReplicatedUsing = OnRep_Mission)
	ETNProcGameMode MissionMode = ETNProcGameMode::Coop;

	UPROPERTY(ReplicatedUsing = OnRep_Mission)
	ETNProcDifficulty MissionDifficulty = ETNProcDifficulty::Normal;

	UPROPERTY(ReplicatedUsing = OnRep_Mission)
	FName MissionMapVariant;

	UFUNCTION()
	void OnRep_Mission();

	/** Escribe la orden del día en la pizarra y la encoge para que quepa. */
	void RefreshMissionBoard();

	/** Cambio de idioma (TNLanguage::OnApplied): el cartel y la pizarra se vuelven a ajustar al ancho del texto nuevo. */
	void HandleLanguageApplied();

	FDelegateHandle LanguageHandle;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> GeneralDefaults;

	UPROPERTY(Transient)
	TObjectPtr<UAnimationAsset> IdleAnim;

	UPROPERTY(Transient)
	TObjectPtr<UAnimationAsset> SaluteAnim;

	float SaluteCooldown = 0.f;
	float SaluteTimeLeft = -1.f;
	float LookYaw = 0.f;
	/** Diferencia entre la malla del personaje de la tortuga y la de demo (TNTurtleArt::GetCopyCorrection). */
	FTransform GeneralCorrection = FTransform::Identity;

	/** Transformación del general: la de demo (mirando a los reclutas, girado hacia el jugador) con GeneralCorrection. */
	FTransform GeneralTransform() const;
	/** Le pone la malla del personaje de la tortuga (TNTurtleArt::ApplyBody) y sus animaciones. */
	void DressGeneral();

	void BuildTable();
	/** Esconde el general de la maqueta y su mesa («Boolean») en cada máquina. */
	void HideBlockout();
};
