// HUD propio de los karts, por encima del del Rally (UTN_RallyHUDWidget, que no cambia): arriba en el centro, el cartel del
// objeto (#304) con su icono, la ruleta al coger una caja, las cargas, los segundos de estrella y cómo se usa (o «lo usa tu
// artillera»); debajo, los kilómetros que quedan hasta la meta. La artillera ve además hacia dónde carga su peso (#295).
// WidgetTree construido en código con TN_RaceUIKit; lee el estado replicado del kart del jugador local.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Kart/TN_KartItems.h"
#include "TN_KartHUDWidget.generated.h"

class ATN_KartBuggy;
class UCanvasPanel;
class UImage;
class UProgressBar;
class UTextBlock;
class UTexture2D;
class UWidget;

UCLASS()
class TORTUNABO_API UTN_KartHUDWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BuildTree();
	/** Kart en el que va el jugador local (el que conduce o el de su peón de artillera). */
	ATN_KartBuggy* FindLocalKart(bool& bOutGunner) const;
	void RefreshItem(const ATN_KartBuggy* Kart, bool bGunner);
	void RefreshDistance(const ATN_KartBuggy* Kart, float DeltaSeconds);
	void RefreshLean(bool bGunner);

	/** Icono de cada objeto (los cocos de la carrera y los del HUD); null si no hay. */
	static UTexture2D* ItemIcon(ETNKartItem Item, int32 Charges);

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> Canvas;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> ItemCard;

	UPROPERTY(Transient)
	TObjectPtr<UImage> ItemImage;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ItemText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ItemHint;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DistanceText;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> LeanPanel;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> LeanLeft;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> LeanRight;

	/** Arco del kart local en la pista de esta máquina (-1 = sin calcular) y refresco del texto. */
	double LocalArcCm = -1.0;
	float DistanceAccumulator = 1.f;
};
