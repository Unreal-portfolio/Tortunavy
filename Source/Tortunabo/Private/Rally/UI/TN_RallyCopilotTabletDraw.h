// Piezas de dibujo de la tableta del copiloto (UTN_RallyCopilotTablet): un lienzo de diseño a escala sobre la geometría
// del widget, pinceles redondeados de Slate, medidas de la maqueta y colores de la paleta Tortunavy. Solo lo incluyen
// los .cpp de la tableta.
#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Rally/TN_RallyPaceNotes.h"
#include "Rally/UI/TN_RallyCopilotTablet.h"
#include "../../UI/HUD/TN_HUDArt.h"
#include "../../UI/HUD/TN_HUDStyle.h"

namespace TNRallyTablet
{
	// ── Maqueta (unidades de diseño; a 1080p la escala queda cerca de 1) ─────────────────────────────────────────

	/** Tableta grande: abajo en el centro, como si la artillera la sujetara. */
	inline const FVector2f FullSize(1280.f, 720.f);
	inline constexpr float FullMaxWidthFraction = 0.92f;
	inline constexpr float FullMaxHeightFraction = 0.66f;
	inline constexpr float FullBottomMargin = 24.f;
	/** Funda de goma (coral) alrededor de la pantalla. */
	inline constexpr float FullBumper = 26.f;

	/** Versión de la conductora sola: arriba a la derecha (libre en el HUD del Rally). */
	inline const FVector2f CompactSize(360.f, 560.f);
	inline constexpr float CompactMaxWidthFraction = 0.2f;
	inline constexpr float CompactMaxHeightFraction = 0.5f;
	inline constexpr float CompactMargin = 32.f;
	inline constexpr float CompactBumper = 14.f;

	/** Muestras del perfil en la tira de los próximos metros. */
	inline constexpr int32 ProfileSamples = 96;
	/** Desnivel mínimo de la escala vertical del perfil, para no exagerar los baches (cm). */
	inline constexpr double ProfileMinSpanCm = 1500.0;
	/** Marcas de distancia del perfil (cm). */
	inline constexpr double ProfileTickCm = 10000.0;

	// ── Colores ───────────────────────────────────────────────────────────────────────────────────────────────

	inline const FLinearColor Bumper = TNHUDArt::CoralC;
	inline const FLinearColor Screen = TNHUDArt::NavyDeep;
	inline const FLinearColor Card = TNHUDArt::Hex(0x12305A, 0.85f);
	inline const FLinearColor Road = TNHUDArt::SandC;
	inline const FLinearColor RoadEdge = TNHUDArt::WetSand;
	inline const FLinearColor Water = TNHUDArt::Sea;
	inline const FLinearColor Highlight = TNHUDArt::Gold;
	inline const FLinearColor Dim = TNHUDStyle::TextDim;

	/** Color de una curva por su grado: cerradas en coral, medias en oro y abiertas en espuma. */
	inline FLinearColor GradeColor(int32 Grade)
	{
		if (Grade <= 2) { return TNHUDArt::CoralC; }
		return Grade <= 4 ? TNHUDArt::Gold : TNHUDArt::Foam;
	}

	inline FLinearColor NoteColor(const TNRallyPaceNotes::FPaceNote& Note)
	{
		switch (Note.Kind)
		{
		case TNRallyPaceNotes::ENoteKind::Turn: return GradeColor(Note.Grade);
		case TNRallyPaceNotes::ENoteKind::Water: return TNHUDArt::SeaLight;
		case TNRallyPaceNotes::ENoteKind::Jump: return TNHUDArt::CoralLight;
		default: return TNHUDArt::SandLight;
		}
	}

	// ── Pinceles (sin texturas: el relleno lo da el tinte de cada dibujo) ─────────────────────────────────────

	struct FBrushes
	{
		FSlateBrush Frame;
		FSlateBrush ScreenPanel;
		FSlateBrush CardPanel;
		FSlateBrush Chip;
		FSlateBrush Circle;
		FSlateBrush ShellRim;

		FBrushes()
			: Frame(FSlateRoundedBoxBrush(FLinearColor::White, 48.f, TNHUDArt::Cream, 4.f))
			, ScreenPanel(FSlateRoundedBoxBrush(FLinearColor::White, 24.f, TNHUDArt::SeaLight, 2.5f))
			, CardPanel(FSlateRoundedBoxBrush(FLinearColor::White, 14.f, TNHUDStyle::Edge, 1.5f))
			, Chip(FSlateRoundedBoxBrush(FLinearColor::White, 8.f))
			, Circle(FSlateRoundedBoxBrush(FLinearColor::White, 8.f))
			, ShellRim(FSlateRoundedBoxBrush(FLinearColor::White, 8.f, TNHUDArt::Cream, 3.f))
		{
			Circle.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
			ShellRim.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
		}
	};

	inline const FBrushes& Brushes()
	{
		static const FBrushes Instance;
		return Instance;
	}

	inline FBox2f ToBox(const FBox2D& Box)
	{
		return FBox2f(FVector2f(Box.Min), FVector2f(Box.Max));
	}

	inline FBox2D MakeArea(float X, float Y, float W, float H)
	{
		return FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H));
	}
}

/** Proyección cenital del eje sobre un panel del mapa, con escala uniforme y centrada. */
struct FTNRallyTabletMapProjection
{
	FVector2D WorldCenter = FVector2D::ZeroVector;
	FVector2f PanelCenter = FVector2f::ZeroVector;
	double PanelPerCm = 0.0;

	/** Planta del mundo → panel: la X del mundo hacia arriba y la Y hacia la derecha (vista cenital de UE). */
	FVector2f operator()(const FVector& World) const
	{
		return PanelCenter + FVector2f(static_cast<float>((World.Y - WorldCenter.Y) * PanelPerCm),
			static_cast<float>(-(World.X - WorldCenter.X) * PanelPerCm));
	}

	static FTNRallyTabletMapProjection Make(const FBox2D& WorldBounds, const FBox2D& Area, float Padding)
	{
		FTNRallyTabletMapProjection Projection;
		Projection.PanelCenter = FVector2f(Area.GetCenter());
		if (!WorldBounds.bIsValid)
		{
			return Projection;
		}
		const FVector2D Extent = WorldBounds.GetSize();
		const FVector2D Panel = Area.GetSize() - FVector2D(2.0 * Padding);
		Projection.WorldCenter = WorldBounds.GetCenter();
		Projection.PanelPerCm = FMath::Min(Panel.X / FMath::Max(Extent.Y, 1.0), Panel.Y / FMath::Max(Extent.X, 1.0));
		return Projection;
	}
};

/**
 * Lienzo de diseño: todo se dibuja en unidades de la maqueta (TNRallyTablet::FullSize o CompactSize) y Fit lo coloca y
 * lo escala sobre la geometría del widget. Cada dibujo va en su propia capa, en el orden en que se pide.
 */
struct FTNRallyTabletPainter
{
	FSlateWindowElementList& Out;
	const FGeometry& Geometry;
	int32 Layer = 0;
	float Alpha = 1.f;
	float Scale = 1.f;
	FVector2f Origin = FVector2f::ZeroVector;
	FVector2f DesignSize = FVector2f(1.f, 1.f);

	FTNRallyTabletPainter(FSlateWindowElementList& InOut, const FGeometry& InGeometry, int32 InLayer, float InAlpha)
		: Out(InOut), Geometry(InGeometry), Layer(InLayer), Alpha(InAlpha)
	{
	}

	/** Coloca y escala la maqueta según el encaje (TNRallyTabletLayout::FitFor: pantalla o panel del mundo). */
	void Fit(const TNRallyTabletLayout::FFit& InFit)
	{
		const TNRallyTabletLayout::FPlacement Placement = TNRallyTabletLayout::Place(FVector2f(Geometry.GetLocalSize()), InFit);
		DesignSize = InFit.Design;
		Scale = Placement.Scale;
		Origin = Placement.Origin;
	}

	FPaintGeometry At(const FVector2f& Position, const FVector2f& Size) const
	{
		return Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Scale, Origin + Position * Scale));
	}

	FLinearColor Fade(const FLinearColor& Color) const
	{
		return FLinearColor(Color.R, Color.G, Color.B, Color.A * Alpha);
	}

	void Box(const FBox2f& Rect, const FSlateBrush& Brush, const FLinearColor& Fill)
	{
		FSlateDrawElement::MakeBox(Out, ++Layer, At(Rect.Min, Rect.GetSize()), &Brush, ESlateDrawEffect::None, Fade(Fill));
	}

	void Circle(const FVector2f& Center, float Radius, const FSlateBrush& Brush, const FLinearColor& Fill)
	{
		Box(FBox2f(Center - FVector2f(Radius), Center + FVector2f(Radius)), Brush, Fill);
	}

	void Lines(const TArray<FVector2f>& Points, const FLinearColor& Color, float Thickness)
	{
		if (Points.Num() < 2) { return; }
		// El grosor de las líneas va en píxeles: no lo escala la transformación, así que se le aplica a mano.
		FSlateDrawElement::MakeLines(Out, ++Layer, At(FVector2f::ZeroVector, DesignSize), Points, ESlateDrawEffect::None,
			Fade(Color), true, FMath::Max(1.f, Thickness * Scale * Geometry.Scale));
	}

	FVector2f Measure(const FText& Content, const FSlateFontInfo& Font) const
	{
		if (!FSlateApplication::IsInitialized()) { return FVector2f::ZeroVector; }
		const FVector2D Size = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Content, Font);
		return FVector2f(Size);
	}

	/** Texto con el punto Anchor en la posición relativa Align de su caja (0,0 arriba a la izquierda; 0.5,0.5 centro). */
	void Text(const FText& Content, const FSlateFontInfo& Font, const FVector2f& Anchor, const FVector2f& Align, const FLinearColor& Color)
	{
		if (Content.IsEmpty()) { return; }
		const FVector2f Size = Measure(Content, Font);
		const FVector2f TopLeft = Anchor - FVector2f(Size.X * Align.X, Size.Y * Align.Y);
		FSlateDrawElement::MakeText(Out, ++Layer, At(TopLeft, Size), Content, Font, ESlateDrawEffect::None, Fade(Color));
	}
};
