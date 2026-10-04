#include "UI/HUD/TN_ButtonGlyphWidget.h"
#include "TN_HUDArt.h"
#include "TN_HUDFonts.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto del bloque.
namespace TNButtonGlyphDetail
{
	/** Ancho de las pastillas (LB, L1) y de los gatillos respecto al alto. */
	constexpr float ShoulderAspect = 1.55f;
	constexpr float TriggerAspect = 1.3f;
	/** Alto de la pastilla de los botones superiores respecto al alto del dibujo. */
	constexpr float ShoulderHeight = 0.78f;
	/** Grosor del filo crema respecto al alto. */
	constexpr float RimFraction = 0.075f;
	/** Grosor de los trazos de los símbolos respecto al alto. */
	constexpr float StrokeFraction = 0.085f;
	/** Tamaño de letra (pt) respecto al alto: letras de la cara, de las pastillas y del stick. */
	constexpr float FaceFontFactor = 0.5f;
	constexpr float ShoulderFontFactor = 0.36f;
	constexpr float StickFontFactor = 0.3f;
	/** Segmentos de un círculo dibujado con líneas. */
	constexpr int32 CircleSegments = 28;

	const FLinearColor PlateFill = TNHUDArt::Hex(0x13233B);
	const FLinearColor DPadFill = TNHUDArt::Hex(0x2A4468);

	TArray<FVector2f> CirclePoints(const FVector2f& Center, float Radius)
	{
		TArray<FVector2f> Points;
		Points.Reserve(CircleSegments + 1);
		for (int32 i = 0; i <= CircleSegments; ++i)
		{
			const float Angle = 2.f * PI * static_cast<float>(i) / CircleSegments;
			Points.Add(Center + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}
		return Points;
	}
}

/** Lo que comparten los pasos del dibujo de un fotograma. */
struct UTN_ButtonGlyphWidget::FPaintContext
{
	const FGeometry& Geometry;
	FSlateWindowElementList& Elements;
	int32 Layer;
	FLinearColor Tint;
	FVector2f Size;
	FVector2f Center;
	float Height;

	void Box(const FVector2f& BoxCenter, const FVector2f& BoxSize, const FSlateBrush& Brush, const FLinearColor& Color)
	{
		const FPaintGeometry Paint = Geometry.ToPaintGeometry(BoxSize, FSlateLayoutTransform(BoxCenter - BoxSize * 0.5f));
		FSlateDrawElement::MakeBox(Elements, ++Layer, Paint, &Brush, ESlateDrawEffect::None, Color * Tint);
	}

	void Lines(const TArray<FVector2f>& Points, const FLinearColor& Color, float Thickness)
	{
		FSlateDrawElement::MakeLines(Elements, ++Layer, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color * Tint, true, Thickness);
	}
};

UTN_ButtonGlyphWidget::UTN_ButtonGlyphWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Pinceles redondeados de Slate (sin textura): blancos, el color va en cada dibujo.
	PillBrush = FSlateRoundedBoxBrush(FLinearColor::White);
	RebuildBrushes();
}

bool UTN_ButtonGlyphWidget::CanDraw(const FKey& Key)
{
	return TNInputGlyphs::GlyphFor(Key, ETNPadFamily::Xbox).Shape != ETNGlyphShape::None;
}

bool UTN_ButtonGlyphWidget::SetKey(const FKey& Key, ETNPadFamily Family)
{
	if (Key == ShownKey && Family == ShownFamily)
	{
		return Spec.Shape != ETNGlyphShape::None;
	}
	ShownKey = Key;
	ShownFamily = Family;
	Spec = TNInputGlyphs::GlyphFor(Key, Family);
	SetMinimumDesiredSize(GlyphSize());
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	return Spec.Shape != ETNGlyphShape::None;
}

void UTN_ButtonGlyphWidget::SetGlyphHeight(float InHeight)
{
	GlyphHeight = FMath::Max(12.f, InHeight);
	RebuildBrushes();
	SetMinimumDesiredSize(GlyphSize());
	Invalidate(EInvalidateWidgetReason::Layout);
}

void UTN_ButtonGlyphWidget::RebuildBrushes()
{
	const float Round = GlyphHeight * 0.42f;
	const float Square = GlyphHeight * 0.14f;
	TriggerBrush = FSlateRoundedBoxBrush(FLinearColor::White, FVector4(Round, Round, Square, Square));
}

FVector2D UTN_ButtonGlyphWidget::GlyphSize() const
{
	using namespace TNButtonGlyphDetail;
	switch (Spec.Shape)
	{
		case ETNGlyphShape::Shoulder: return FVector2D(GlyphHeight * ShoulderAspect, GlyphHeight);
		case ETNGlyphShape::Trigger:  return FVector2D(GlyphHeight * TriggerAspect, GlyphHeight);
		default:                      return FVector2D(GlyphHeight, GlyphHeight);
	}
}

int32 UTN_ButtonGlyphWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace TNButtonGlyphDetail;
	const int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (Spec.Shape == ETNGlyphShape::None)
	{
		return Layer;
	}
	const FVector2f Local = FVector2f(AllottedGeometry.GetLocalSize());
	const float Height = FMath::Min(Local.Y, GlyphHeight);
	FPaintContext Ctx{ AllottedGeometry, OutDrawElements, Layer, InWidgetStyle.GetColorAndOpacityTint(), Local, Local * 0.5f, Height };
	switch (Spec.Shape)
	{
		case ETNGlyphShape::Face:
			PaintPlate(Ctx, FVector2f(Height, Height), PillBrush);
			if (Spec.Symbol == ETNGlyphSymbol::None) { PaintLabel(Ctx, FaceFontFactor); }
			else { PaintSymbol(Ctx); }
			break;
		case ETNGlyphShape::Shoulder:
			PaintPlate(Ctx, FVector2f(Height * ShoulderAspect, Height * ShoulderHeight), PillBrush);
			PaintLabel(Ctx, ShoulderFontFactor);
			break;
		case ETNGlyphShape::Trigger:
			PaintPlate(Ctx, FVector2f(Height * TriggerAspect, Height), TriggerBrush);
			PaintLabel(Ctx, ShoulderFontFactor);
			break;
		case ETNGlyphShape::Stick:
			PaintStick(Ctx);
			break;
		case ETNGlyphShape::DPad:
			PaintDPad(Ctx);
			break;
		default:
			PaintMenuOrView(Ctx);
			break;
	}
	return Ctx.Layer;
}

void UTN_ButtonGlyphWidget::PaintPlate(FPaintContext& Ctx, const FVector2f& Size, const FSlateBrush& Brush) const
{
	using namespace TNButtonGlyphDetail;
	// Filo crema y el botón azul marino encima, un poco más pequeño.
	const float Rim = Ctx.Height * RimFraction;
	Ctx.Box(Ctx.Center, Size, Brush, TNHUDArt::Cream);
	Ctx.Box(Ctx.Center, Size - FVector2f(2.f * Rim, 2.f * Rim), Brush, PlateFill);
}

void UTN_ButtonGlyphWidget::PaintLabel(FPaintContext& Ctx, float SizeFactor) const
{
	if (Spec.Label.IsEmpty() || !FSlateApplication::IsInitialized())
	{
		return;
	}
	const FSlateFontInfo Font = TNHUDFonts::Make(TEXT("Black"), FMath::Max(6, FMath::RoundToInt(Ctx.Height * SizeFactor)));
	const FString Text = Spec.Label.ToString();
	const FVector2f TextSize = FVector2f(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font));
	const FPaintGeometry Paint = Ctx.Geometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(Ctx.Center - TextSize * 0.5f));
	FSlateDrawElement::MakeText(Ctx.Elements, ++Ctx.Layer, Paint, Text, Font, ESlateDrawEffect::None, Spec.Accent * Ctx.Tint);
}

void UTN_ButtonGlyphWidget::PaintSymbol(FPaintContext& Ctx) const
{
	using namespace TNButtonGlyphDetail;
	const float R = Ctx.Height * 0.21f;
	const float Stroke = Ctx.Height * StrokeFraction;
	const FVector2f C = Ctx.Center;
	switch (Spec.Symbol)
	{
		case ETNGlyphSymbol::Cross:
			Ctx.Lines({ C + FVector2f(-R, -R), C + FVector2f(R, R) }, Spec.Accent, Stroke);
			Ctx.Lines({ C + FVector2f(-R, R), C + FVector2f(R, -R) }, Spec.Accent, Stroke);
			break;
		case ETNGlyphSymbol::Circle:
			Ctx.Lines(CirclePoints(C, R), Spec.Accent, Stroke);
			break;
		case ETNGlyphSymbol::Square:
		{
			const float H = R;
			Ctx.Lines({ C + FVector2f(-H, -H), C + FVector2f(H, -H), C + FVector2f(H, H), C + FVector2f(-H, H), C + FVector2f(-H, -H) }, Spec.Accent, Stroke);
			break;
		}
		default:
		{
			// Triángulo con el centro de la caja en el centro del botón.
			const FVector2f Top = C + FVector2f(0.f, -R * 1.1f);
			const FVector2f Left = C + FVector2f(-R * 1.1f, R * 0.8f);
			const FVector2f Right = C + FVector2f(R * 1.1f, R * 0.8f);
			Ctx.Lines({ Top, Right, Left, Top }, Spec.Accent, Stroke);
			break;
		}
	}
}

void UTN_ButtonGlyphWidget::PaintStick(FPaintContext& Ctx) const
{
	using namespace TNButtonGlyphDetail;
	PaintPlate(Ctx, FVector2f(Ctx.Height, Ctx.Height), PillBrush);
	// El capuchón del stick: un aro dentro del botón; moverlo lleva además cuatro marcas alrededor.
	const float Stroke = Ctx.Height * StrokeFraction * 0.6f;
	Ctx.Lines(CirclePoints(Ctx.Center, Ctx.Height * 0.3f), TNHUDArt::SeaLight, Stroke);
	if (!Spec.bClick)
	{
		const float Inner = Ctx.Height * 0.36f;
		const float Outer = Ctx.Height * 0.43f;
		for (const FVector2f Dir : { FVector2f(0.f, -1.f), FVector2f(1.f, 0.f), FVector2f(0.f, 1.f), FVector2f(-1.f, 0.f) })
		{
			Ctx.Lines({ Ctx.Center + Dir * Inner, Ctx.Center + Dir * Outer }, TNHUDArt::Cream, Stroke);
		}
	}
	PaintLabel(Ctx, StickFontFactor);
}

void UTN_ButtonGlyphWidget::PaintDPad(FPaintContext& Ctx) const
{
	using namespace TNButtonGlyphDetail;
	// La cruz con el filo crema y el brazo de la dirección en dorado.
	const float Long = Ctx.Height * 0.92f;
	const float Wide = Ctx.Height * 0.36f;
	const float Rim = Ctx.Height * RimFraction;
	Ctx.Box(Ctx.Center, FVector2f(Long, Wide), PillBrush, TNHUDArt::Cream);
	Ctx.Box(Ctx.Center, FVector2f(Wide, Long), PillBrush, TNHUDArt::Cream);
	Ctx.Box(Ctx.Center, FVector2f(Long - 2.f * Rim, Wide - 2.f * Rim), PillBrush, DPadFill);
	Ctx.Box(Ctx.Center, FVector2f(Wide - 2.f * Rim, Long - 2.f * Rim), PillBrush, DPadFill);
	static const FVector2f Dirs[4] = { FVector2f(0.f, -1.f), FVector2f(1.f, 0.f), FVector2f(0.f, 1.f), FVector2f(-1.f, 0.f) };
	const FVector2f Dir = Dirs[FMath::Clamp(Spec.Index, 0, 3)];
	const float Arm = (Long - Wide) * 0.5f - Rim;
	const FVector2f ArmCenter = Ctx.Center + Dir * (Wide * 0.5f + Arm * 0.5f);
	const FVector2f ArmSize = Dir.X != 0.f ? FVector2f(Arm, Wide - 2.f * Rim) : FVector2f(Wide - 2.f * Rim, Arm);
	Ctx.Box(ArmCenter, ArmSize, PillBrush, TNHUDArt::Gold);
}

void UTN_ButtonGlyphWidget::PaintMenuOrView(FPaintContext& Ctx) const
{
	using namespace TNButtonGlyphDetail;
	PaintPlate(Ctx, FVector2f(Ctx.Height, Ctx.Height), PillBrush);
	const float Stroke = Ctx.Height * StrokeFraction * 0.7f;
	const float W = Ctx.Height * 0.2f;
	const FVector2f C = Ctx.Center;
	if (Spec.Shape == ETNGlyphShape::Menu)
	{
		// Tres rayas (Menú de Xbox, Options de PlayStation).
		for (const float Y : { -0.13f, 0.f, 0.13f })
		{
			Ctx.Lines({ C + FVector2f(-W, Y * Ctx.Height), C + FVector2f(W, Y * Ctx.Height) }, TNHUDArt::Cream, Stroke);
		}
		return;
	}
	// Dos ventanas superpuestas (Vista de Xbox; también Create y Share).
	const float H = Ctx.Height * 0.12f;
	const FVector2f Back = C + FVector2f(-H * 0.5f, -H * 0.5f);
	const FVector2f Front = C + FVector2f(H * 0.5f, H * 0.5f);
	Ctx.Lines({ Back + FVector2f(-H, -H), Back + FVector2f(H, -H), Back + FVector2f(H, H), Back + FVector2f(-H, H), Back + FVector2f(-H, -H) }, TNHUDArt::SeaLight, Stroke);
	Ctx.Lines({ Front + FVector2f(-H, -H), Front + FVector2f(H, -H), Front + FVector2f(H, H), Front + FVector2f(-H, H), Front + FVector2f(-H, -H) }, TNHUDArt::Cream, Stroke);
}
