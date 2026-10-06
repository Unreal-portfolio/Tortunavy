#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TN_GhostHUDWidget.generated.h"

class APlayerController;
class UCanvasPanel;
class UImage;
class UTextBlock;
class UWidget;

/**
 * Cartel del fantasma espectador (Docs/Fantasma_Espectador.md), hecho en código con el estilo del HUD (TNHUDArt).
 *
 * - De fantasma (abajo a la derecha): tu icono de tortuga fantasma que flota, con tu nombre en una cinta, y un cartel
 *   con a quién miras, la cámara que llevas (libre o fija), los controles («← → cambiar · C cámara · rueda zoom», o los
 *   del mando si es lo último que has tocado) y quién más está mirando a esa tortuga. La interfaz de la tortuga que
 *   sigues (energía, objetos, cara, puntos) la enseña el HUD de siempre (UTN_RunHUDWidget), y en la tripulación sales
 *   tú con la cara de fantasma.
 * - Con tortuga (abajo a la izquierda, encima del distintivo): los fantasmas que te están mirando («Te mira Ana»).
 *
 * Lo crean los fantasmas en cada jugador local (EnsureFor); sin fantasmas en el mundo no se ve nada.
 */
UCLASS()
class TORTUNABO_API UTN_GhostHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Se asegura de que el jugador local PC tenga el cartel en pantalla (lo vuelve a poner tras un viaje). */
	static void EnsureFor(APlayerController* PC);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BuildTree();
	void SetTextIfChanged(UTextBlock* Block, const FText& Value);

	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Canvas;
	UPROPERTY(Transient) TObjectPtr<UWidget> GhostCard;
	UPROPERTY(Transient) TObjectPtr<UImage> GhostIcon;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> OwnNameText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FollowText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CameraText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> KeysText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> AlsoText;
	UPROPERTY(Transient) TObjectPtr<UWidget> WatchersCard;
	UPROPERTY(Transient) TObjectPtr<UImage> WatchersIcon;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> WatchersText;

	float Time = 0.f;
	float CardPop = 0.f;
	float WatchersPop = 0.f;
	bool bCardShown = false;
	bool bWatchersShown = false;
};
