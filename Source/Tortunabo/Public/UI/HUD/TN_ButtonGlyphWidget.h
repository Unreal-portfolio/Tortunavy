#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "UI/TN_InputGlyphs.h"
#include "TN_ButtonGlyphWidget.generated.h"

/**
 * @brief Un botón del mando dibujado como lo lleva impreso (#347): la letra de color de Xbox y de la Steam Deck o el
 * símbolo de PlayStation en un botón redondo, LB/L1 en una pastilla, LT/L2 en un gatillo, el stick, la cruceta con la
 * dirección resaltada y los botones de menú y vista. En el estilo Tortunavy (azul marino con el filo crema) y pintado en
 * código (NativePaint), sin texturas: vale para cualquier botón que se reasigne en Ajustes.
 *
 * Lo usa el aviso de interacción del HUD; con teclado siguen enseñando su tecla dibujada.
 */
UCLASS()
class TORTUNABO_API UTN_ButtonGlyphWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UTN_ButtonGlyphWidget(const FObjectInitializer& ObjectInitializer);

	/** true si Key es un botón del mando que se sabe dibujar. */
	static bool CanDraw(const FKey& Key);

	/** El botón y cómo se dibuja (familia del mando). Devuelve CanDraw(Key). */
	bool SetKey(const FKey& Key, ETNPadFamily Family);

	/** Alto del botón (px de diseño); las pastillas y los gatillos son más anchos. */
	void SetGlyphHeight(float InHeight);

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	FTNGlyphSpec Spec;
	FKey ShownKey;
	ETNPadFamily ShownFamily = ETNPadFamily::Xbox;
	float GlyphHeight = 40.f;

	/** Pastilla (redondos y botones superiores): radio de la mitad del alto. */
	FSlateBrush PillBrush;
	/** Gatillo: arriba más redondo que abajo (se rehace con el alto). */
	FSlateBrush TriggerBrush;

	FVector2D GlyphSize() const;
	void RebuildBrushes();

	struct FPaintContext;
	void PaintPlate(FPaintContext& Ctx, const FVector2f& Size, const FSlateBrush& Brush) const;
	void PaintLabel(FPaintContext& Ctx, float SizeFactor) const;
	void PaintSymbol(FPaintContext& Ctx) const;
	void PaintStick(FPaintContext& Ctx) const;
	void PaintDPad(FPaintContext& Ctx) const;
	void PaintMenuOrView(FPaintContext& Ctx) const;
};
