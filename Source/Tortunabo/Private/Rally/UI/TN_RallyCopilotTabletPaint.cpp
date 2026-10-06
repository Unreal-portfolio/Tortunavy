// Dibujo de la tableta del copiloto (UTN_RallyCopilotTablet::NativePaint): funda de goma coral, pantalla azul marino,
// mapa cenital con los caparazones, próxima nota en grande, perfil de los próximos metros con las notas y las cajas de
// munición, y la munición que lleva.

#include "Rally/UI/TN_RallyCopilotTablet.h"

#include "Core/TN_LocText.h"
#include "Rally/TN_RallyHitReport.h"
#include "TN_RallyCopilotTabletDraw.h"

namespace TNRallyTabletPaint
{
	using namespace TNRallyTablet;
	using namespace TNRallyPaceNotes;

	FText AmmoName(ETNRallyAmmo Ammo)
	{
		return TNRallyHitLog::AmmoName(Ammo);
	}

	/** Fuente que hace caber Text en MaxWidth, de MaxSize a MinSize puntos. */
	FSlateFontInfo FitFont(const FTNRallyTabletPainter& Painter, const FText& Text, int32 MaxSize, int32 MinSize, float MaxWidth)
	{
		const FSlateFontInfo Font = TNHUDStyle::Font(TEXT("Bold"), MaxSize);
		const float Width = Painter.Measure(Text, Font).X;
		if (Width <= MaxWidth || Width <= 0.f)
		{
			return Font;
		}
		const int32 Size = FMath::Max(MinSize, FMath::FloorToInt32(MaxSize * MaxWidth / Width));
		return TNHUDStyle::Font(TEXT("Bold"), Size);
	}

	/** Flecha de curva: tallo hacia arriba y punta girada el ángulo de la curva (horquilla = vuelta hacia abajo). */
	void PaintTurnIcon(FTNRallyTabletPainter& Painter, const FPaceNote& Note, const FVector2f& Center, float Size, const FLinearColor& Color)
	{
		const float Side = Note.Direction == ETurnDirection::Left ? -1.f : 1.f;
		const float Angle = FMath::DegreesToRadians(static_cast<float>(FMath::Clamp(Note.AngleDeg, 30.0, 180.0)));
		const FVector2f End(Side * FMath::Sin(Angle), -FMath::Cos(Angle));
		const FVector2f Bend = Center + FVector2f(-Side * 0.12f * Size, 0.f);
		const FVector2f Tip = Bend + End * 0.4f * Size;
		Painter.Lines({ Bend + FVector2f(0.f, 0.42f * Size), Bend, Tip }, Color, 0.13f * Size);
		const FVector2f Back = -End * 0.2f * Size;
		const FVector2f Normal(-Back.Y, Back.X);
		Painter.Lines({ Tip + Back * 0.8f + Normal * 0.7f, Tip, Tip + Back * 0.8f - Normal * 0.7f }, Color, 0.13f * Size);
	}

	/** Olas: dos líneas onduladas. */
	void PaintWaterIcon(FTNRallyTabletPainter& Painter, const FVector2f& Center, float Size, const FLinearColor& Color)
	{
		for (const float Row : { -0.12f, 0.16f })
		{
			TArray<FVector2f> Wave;
			for (int32 Index = 0; Index <= 12; ++Index)
			{
				const float T = Index / 12.f;
				Wave.Add(Center + FVector2f((T - 0.5f) * 0.9f * Size, (Row + 0.08f * FMath::Sin(T * 4.f * PI)) * Size));
			}
			Painter.Lines(Wave, Color, 0.1f * Size);
		}
	}

	void PaintNoteIcon(FTNRallyTabletPainter& Painter, const FPaceNote& Note, const FVector2f& Center, float Size)
	{
		const FLinearColor Color = NoteColor(Note);
		const float Thick = 0.13f * Size;
		switch (Note.Kind)
		{
		case ENoteKind::Turn:
			PaintTurnIcon(Painter, Note, Center, Size, Color);
			break;
		case ENoteKind::Crest:
			Painter.Lines({ Center + FVector2f(-0.42f, 0.25f) * Size, Center + FVector2f(0.f, -0.2f) * Size,
				Center + FVector2f(0.42f, 0.25f) * Size }, Color, Thick);
			break;
		case ENoteKind::Jump:
			// Rampa y la parábola del vuelo.
			Painter.Lines({ Center + FVector2f(-0.45f, 0.3f) * Size, Center + FVector2f(-0.05f, 0.f) * Size }, Color, Thick);
			Painter.Lines({ Center + FVector2f(0.05f, -0.1f) * Size, Center + FVector2f(0.22f, -0.22f) * Size,
				Center + FVector2f(0.36f, -0.12f) * Size, Center + FVector2f(0.45f, 0.1f) * Size }, Color, Thick * 0.6f);
			break;
		case ENoteKind::Water:
			PaintWaterIcon(Painter, Center, Size, Color);
			break;
		default:
			break;
		}
	}

	/** Trozos del eje bajo el nivel del agua, encima de la arena. */
	void PaintMapWater(FTNRallyTabletPainter& Painter, const FTrackNotes& Track, const FTNRallyTabletMapProjection& Projection, float Thickness)
	{
		if (!Track.bHasWater)
		{
			return;
		}
		TArray<FVector2f> Run;
		for (const FVector& Point : Track.Points)
		{
			if (Point.Z < Track.WaterZ)
			{
				Run.Add(Projection(Point));
				continue;
			}
			Painter.Lines(Run, Water, Thickness);
			Run.Reset();
		}
		Painter.Lines(Run, Water, Thickness);
	}

	/** Tramo que cubre el perfil, resaltado sobre el mapa. */
	void PaintMapWindow(FTNRallyTabletPainter& Painter, const FTrackNotes& Track, const FTNRallyTabletMapProjection& Projection,
		double FromArc, double Range, float Thickness)
	{
		const double Span = Track.bClosed ? Range : FMath::Clamp(Track.LengthCm - FromArc, 0.0, Range);
		const int32 Steps = FMath::Clamp(FMath::CeilToInt32(Span / 1000.0), 1, 80);
		TArray<FVector2f> Window;
		for (int32 Index = 0; Index <= Steps; ++Index)
		{
			Window.Add(Projection(LocationAtArc(Track, FromArc + Span * Index / Steps)));
		}
		Painter.Lines(Window, FLinearColor(Highlight.R, Highlight.G, Highlight.B, 0.85f), Thickness);
	}

	/** Raya de salida (crema) y, en punto a punto, de meta (oro), perpendiculares al eje. */
	void PaintMapEnds(FTNRallyTabletPainter& Painter, const FTrackNotes& Track, const FTNRallyTabletMapProjection& Projection, float Length)
	{
		auto Bar = [&](double Arc, const FLinearColor& Color)
		{
			const FVector2f Here = Projection(LocationAtArc(Track, Arc));
			const FVector2f Ahead = Projection(LocationAtArc(Track, Arc + 1000.0));
			const FVector2f Back = Projection(LocationAtArc(Track, Arc - 1000.0));
			const FVector2f Dir = (Ahead - Back).GetSafeNormal();
			const FVector2f Normal(-Dir.Y, Dir.X);
			Painter.Lines({ Here - Normal * Length, Here + Normal * Length }, Color, Length * 0.45f);
		};
		Bar(0.0, TNHUDArt::Cream);
		if (!Track.bClosed)
		{
			Bar(Track.LengthCm, Highlight);
		}
	}

	/** Perfil de los próximos metros: alturas, base y escala vertical. */
	struct FProfile
	{
		TArray<double> Heights;
		double RangeCm = 0.0;
		double BottomZ = 0.0;
		double SpanZ = 1.0;
	};

	FProfile SampleProfile(const FTrackNotes& Track, double FromArc, double LookAhead)
	{
		FProfile Profile;
		Profile.RangeCm = Track.bClosed ? LookAhead : FMath::Clamp(Track.LengthCm - FromArc, 0.0, LookAhead);
		double MinZ = TNumericLimits<double>::Max();
		double MaxZ = TNumericLimits<double>::Lowest();
		for (int32 Index = 0; Index <= ProfileSamples; ++Index)
		{
			const double Z = LocationAtArc(Track, FromArc + Profile.RangeCm * Index / ProfileSamples).Z;
			Profile.Heights.Add(Z);
			MinZ = FMath::Min(MinZ, Z);
			MaxZ = FMath::Max(MaxZ, Z);
		}
		Profile.SpanZ = FMath::Max(MaxZ - MinZ, ProfileMinSpanCm) * 1.2;
		Profile.BottomZ = 0.5 * (MinZ + MaxZ) - 0.5 * Profile.SpanZ;
		return Profile;
	}

	/** Rectángulo de la gráfica dentro de la tarjeta: dos filas de iconos arriba y las distancias abajo. */
	FBox2f ProfilePlot(const FBox2D& Area)
	{
		const FBox2f Box = ToBox(Area);
		return FBox2f(Box.Min + FVector2f(18.f, 74.f), Box.Max - FVector2f(18.f, 30.f));
	}

	float ProfileX(const FBox2f& Plot, double DistanceCm, double LookAhead)
	{
		return Plot.Min.X + static_cast<float>(DistanceCm / LookAhead) * Plot.GetSize().X;
	}

	float ProfileY(const FBox2f& Plot, const FProfile& Profile, double Z)
	{
		return Plot.Max.Y - static_cast<float>((Z - Profile.BottomZ) / Profile.SpanZ) * Plot.GetSize().Y;
	}

	/** Agua al fondo, arena rellena por columnas y la línea del suelo encima. */
	void PaintProfileGround(FTNRallyTabletPainter& Painter, const FTrackNotes& Track, const FProfile& Profile, const FBox2f& Plot,
		double LookAhead)
	{
		const float WaterY = ProfileY(Plot, Profile, Track.WaterZ);
		if (Track.bHasWater && WaterY < Plot.Max.Y)
		{
			const float Top = FMath::Max(WaterY, Plot.Min.Y);
			Painter.Box(FBox2f(FVector2f(Plot.Min.X, Top), Plot.Max), Brushes().Chip, FLinearColor(Water.R, Water.G, Water.B, 0.45f));
		}
		const int32 Num = Profile.Heights.Num();
		const float Column = Plot.GetSize().X / ProfileSamples * static_cast<float>(Profile.RangeCm / LookAhead) + 1.f;
		TArray<FVector2f> Ground;
		// Relleno en una sola línea en zigzag (suelo → fondo → fondo → suelo...) con el grosor de una columna.
		TArray<FVector2f> Fill;
		for (int32 Index = 0; Index < Num; ++Index)
		{
			const float X = ProfileX(Plot, Profile.RangeCm * Index / ProfileSamples, LookAhead);
			const FVector2f Top(X, ProfileY(Plot, Profile, Profile.Heights[Index]));
			const FVector2f Bottom(X, Plot.Max.Y);
			Fill.Add(Index % 2 == 0 ? Top : Bottom);
			Fill.Add(Index % 2 == 0 ? Bottom : Top);
			Ground.Add(Top);
		}
		Painter.Lines(Fill, FLinearColor(RoadEdge.R, RoadEdge.G, RoadEdge.B, 0.6f), Column);
		Painter.Lines(Ground, Road, 4.f);
	}

	/** Una caja de munición: cuadrado de oro con el hueco oscuro, como las cajas de la pista vistas de frente. */
	void PaintBoxIcon(FTNRallyTabletPainter& Painter, const FVector2f& Center, float Size)
	{
		const FVector2f Half(Size * 0.5f);
		Painter.Box(FBox2f(Center - Half, Center + Half), Brushes().Chip, Highlight);
		Painter.Box(FBox2f(Center - Half * 0.5f, Center + Half * 0.5f), Brushes().Chip, TNHUDArt::NavyDeep);
	}

	/** Filas de cajas de los próximos metros (distancias en BoxesAhead), apoyadas en la línea del suelo del perfil. */
	void PaintProfileBoxes(FTNRallyTabletPainter& Painter, const FTrackNotes& Track, const FProfile& Profile, const FBox2f& Plot,
		double FromArc, const TArray<double>& BoxesAhead, double LookAhead)
	{
		constexpr float BoxSize = 22.f;
		for (const double Distance : BoxesAhead)
		{
			if (Distance > Profile.RangeCm)
			{
				continue;
			}
			const float X = ProfileX(Plot, Distance, LookAhead);
			const float GroundY = ProfileY(Plot, Profile, LocationAtArc(Track, FromArc + Distance).Z);
			PaintBoxIcon(Painter, FVector2f(X, FMath::Min(GroundY, Plot.Max.Y) - BoxSize * 0.5f - 3.f), BoxSize);
		}
	}

	/** Marcas de 100 m bajo la gráfica y la meta si cae dentro. */
	void PaintProfileTicks(FTNRallyTabletPainter& Painter, const FProfile& Profile, const FBox2f& Plot, double LookAhead)
	{
		const FSlateFontInfo Font = TNHUDStyle::Font(TEXT("Regular"), 15);
		for (double Distance = ProfileTickCm; Distance < LookAhead; Distance += ProfileTickCm)
		{
			const float X = ProfileX(Plot, Distance, LookAhead);
			Painter.Lines({ FVector2f(X, Plot.Max.Y), FVector2f(X, Plot.Max.Y + 6.f) }, Dim, 2.f);
			Painter.Text(DistanceText(Distance), Font, FVector2f(X, Plot.Max.Y + 8.f), FVector2f(0.5f, 0.f), Dim);
		}
		if (Profile.RangeCm < LookAhead - 1.0)
		{
			const float X = ProfileX(Plot, Profile.RangeCm, LookAhead);
			Painter.Lines({ FVector2f(X, Plot.Min.Y), FVector2f(X, Plot.Max.Y) }, Highlight, 3.f);
			Painter.Text(NSLOCTEXT("Rally", "TabletFinish", "Meta"), TNHUDStyle::Font(TEXT("Bold"), 18),
				FVector2f(X - 6.f, Plot.Min.Y), FVector2f(1.f, 0.f), Highlight);
		}
	}
}

int32 UTN_RallyCopilotTablet::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (View == ETNRallyTabletView::Hidden || !TrackNotes.IsValid())
	{
		return Layer;
	}
	FTNRallyTabletPainter Painter(OutDrawElements, AllottedGeometry, Layer, InWidgetStyle.GetColorAndOpacityTint().A);
	Painter.Fit(TNRallyTabletLayout::FitFor(Presentation, View));
	View == ETNRallyTabletView::Full ? PaintFull(Painter) : PaintCompact(Painter);
	return Painter.Layer;
}

// ── Encaje en la pantalla o en un panel del mundo (#334) ───────────────────────────────────────────────────────

FVector2f TNRallyTabletLayout::DesignSize(ETNRallyTabletView View)
{
	return View == ETNRallyTabletView::Compact ? TNRallyTablet::CompactSize : TNRallyTablet::FullSize;
}

TNRallyTabletLayout::FFit TNRallyTabletLayout::FitFor(ETNRallyTabletPresentation Presentation, ETNRallyTabletView View)
{
	using namespace TNRallyTablet;
	FFit Fit;
	Fit.Design = DesignSize(View);
	if (Presentation == ETNRallyTabletPresentation::World)
	{
		// El panel es la tableta: la ocupa entera, centrada (el dueño del panel le da el tamaño de DesignSize).
		return Fit;
	}
	if (View == ETNRallyTabletView::Compact)
	{
		Fit.MaxWidthFraction = CompactMaxWidthFraction;
		Fit.MaxHeightFraction = CompactMaxHeightFraction;
		Fit.Anchor = FVector2f(1.f, 0.f);
		Fit.Margin = FVector2f(CompactMargin);
		return Fit;
	}
	Fit.MaxWidthFraction = FullMaxWidthFraction;
	Fit.MaxHeightFraction = FullMaxHeightFraction;
	Fit.Anchor = FVector2f(0.5f, 1.f);
	Fit.Margin = FVector2f(0.f, FullBottomMargin);
	return Fit;
}

TNRallyTabletLayout::FPlacement TNRallyTabletLayout::Place(const FVector2f& LocalSize, const FFit& Fit)
{
	FPlacement Placement;
	const FVector2f Design(FMath::Max(Fit.Design.X, 1.f), FMath::Max(Fit.Design.Y, 1.f));
	Placement.Scale = FMath::Max(0.1f, FMath::Min(LocalSize.X * Fit.MaxWidthFraction / Design.X,
		LocalSize.Y * Fit.MaxHeightFraction / Design.Y));
	const FVector2f Free = LocalSize - Design * Placement.Scale - Fit.Margin * 2.f;
	Placement.Origin = Fit.Margin + FVector2f(Free.X * Fit.Anchor.X, Free.Y * Fit.Anchor.Y);
	return Placement;
}

void UTN_RallyCopilotTablet::PaintFull(FTNRallyTabletPainter& Painter) const
{
	using namespace TNRallyTablet;
	const FBrushes& Brush = Brushes();
	// Funda de goma coral con dos tornillos y la cámara, y la pantalla dentro.
	Painter.Box(FBox2f(FVector2f::ZeroVector, FullSize), Brush.Frame, Bumper);
	Painter.Circle(FVector2f(FullBumper * 0.5f, FullSize.Y * 0.5f), 6.f, Brush.Circle, TNHUDArt::Cream);
	Painter.Circle(FVector2f(FullSize.X - FullBumper * 0.5f, FullSize.Y * 0.5f), 6.f, Brush.Circle, TNHUDArt::Cream);
	Painter.Circle(FVector2f(FullSize.X * 0.5f, FullBumper * 0.5f), 5.f, Brush.Circle, TNHUDArt::NavyDeep);
	Painter.Box(FBox2f(FVector2f(FullBumper), FullSize - FVector2f(FullBumper)), Brush.ScreenPanel, Screen);

	PaintHeader(Painter);
	PaintMap(Painter, MakeArea(50.f, 96.f, 520.f, 570.f), false);
	PaintHitLog(Painter, MakeArea(62.f, 548.f, 496.f, 106.f), false);
	PaintNextNote(Painter, MakeArea(594.f, 96.f, 636.f, 170.f), false);
	PaintProfile(Painter, MakeArea(594.f, 282.f, 636.f, 230.f));
	PaintAmmo(Painter, MakeArea(594.f, 528.f, 636.f, 138.f));

	Painter.Text(NSLOCTEXT("Rally", "TabletHint", "Tab · M · Vista: guardar la tableta"), TNHUDStyle::Font(TEXT("Bold"), 14, false),
		FVector2f(FullSize.X * 0.5f, FullSize.Y - FullBumper * 0.5f), FVector2f(0.5f, 0.5f), TNHUDArt::NavyDeep);
}

void UTN_RallyCopilotTablet::PaintHeader(FTNRallyTabletPainter& Painter) const
{
	using namespace TNRallyTablet;
	Painter.Text(NSLOCTEXT("Rally", "TabletTitle", "Copiloto"), TNHUDStyle::Font(TEXT("Bold"), 26),
		FVector2f(52.f, 62.f), FVector2f(0.f, 0.5f), TNHUDArt::Foam);
	const FTNRallyTabletMark* Mine = Marks.FindByPredicate([](const FTNRallyTabletMark& Mark) { return Mark.bMine; });
	if (Mine && Mine->Place > 0)
	{
		const FText Place = FText::Format(NSLOCTEXT("Rally", "PlaceOfTotal", "{0}.º / {1}"), TNLocText::Int(Mine->Place),
			TNLocText::Int(Marks.Num()));
		Painter.Text(Place, TNHUDStyle::Font(TEXT("Bold"), 28), FVector2f(FullSize.X - 52.f, 62.f), FVector2f(1.f, 0.5f), Highlight);
	}
}

void UTN_RallyCopilotTablet::PaintCompact(FTNRallyTabletPainter& Painter) const
{
	using namespace TNRallyTablet;
	const FBrushes& Brush = Brushes();
	Painter.Box(FBox2f(FVector2f::ZeroVector, CompactSize), Brush.Frame, Bumper);
	Painter.Box(FBox2f(FVector2f(CompactBumper), CompactSize - FVector2f(CompactBumper)), Brush.ScreenPanel, Screen);
	PaintMap(Painter, MakeArea(28.f, 28.f, 304.f, 304.f), true);
	PaintHitLog(Painter, MakeArea(36.f, 254.f, 288.f, 70.f), true);
	PaintNextNote(Painter, MakeArea(28.f, 344.f, 304.f, 108.f), true);
	PaintCompactAmmo(Painter, MakeArea(28.f, 464.f, 304.f, 68.f));
}

void UTN_RallyCopilotTablet::PaintHitLog(FTNRallyTabletPainter& Painter, const FBox2D& Area, bool bSmall) const
{
	using namespace TNRallyTablet;
	if (HitLog.Num() == 0)
	{
		return;
	}
	// Las líneas se apagan en sus últimos FadeSeconds; la caja, con la más nueva.
	constexpr float FadeSeconds = 2.f;
	const auto Opacity = [](const TNRallyHitLog::FLine& Line)
	{
		return FMath::Clamp((TNRallyHitLog::LineSeconds - Line.Age) / FadeSeconds, 0.f, 1.f);
	};
	const FBox2f Box = ToBox(Area);
	const float Pad = bSmall ? 8.f : 12.f;
	const float LineHeight = (Box.GetSize().Y - 2.f * Pad) / TNRallyHitLog::MaxLines;
	const FLinearColor Back = Screen.CopyWithNewOpacity(0.78f * Opacity(HitLog[0]));
	Painter.Box(Box, Brushes().CardPanel, Back);
	const FSlateFontInfo Font = TNHUDStyle::Font(TEXT("Bold"), bSmall ? 15 : 20);
	for (int32 Index = 0; Index < HitLog.Num(); ++Index)
	{
		const TNRallyHitLog::FLine& Line = HitLog[Index];
		// La más nueva, entera; las anteriores, algo apagadas.
		const float Dimming = Index == 0 ? 1.f : 0.7f;
		const FVector2f Anchor(Box.Min.X + Pad, Box.Min.Y + Pad + LineHeight * (Index + 0.5f));
		Painter.Text(Line.Text, Font, Anchor, FVector2f(0.f, 0.5f), Line.Color.CopyWithNewOpacity(Dimming * Opacity(Line)));
	}
}

FText UTN_RallyCopilotTablet::NextBoxText() const
{
	return BoxesAhead.Num() > 0
		? FText::Format(NSLOCTEXT("Rally", "TabletNextBox", "Caja a {0}"), TNRallyPaceNotes::DistanceText(BoxesAhead[0]))
		: FText::GetEmpty();
}

void UTN_RallyCopilotTablet::PaintCompactAmmo(FTNRallyTabletPainter& Painter, const FBox2D& Area) const
{
	using namespace TNRallyTablet;
	using namespace TNRallyTabletPaint;
	const FBox2f Box = ToBox(Area);
	Painter.Box(Box, Brushes().CardPanel, Card);
	const bool bHasSpecial = Ammo.Special != ETNRallyAmmo::None && Ammo.SpecialCharges > 0;
	const bool bSpecialSelected = bHasSpecial && Ammo.Selected == Ammo.Special;
	const FText Selected = bSpecialSelected
		? FText::Format(NSLOCTEXT("Rally", "TabletCharges", "{0} ×{1}"), AmmoName(Ammo.Special), TNLocText::Int(Ammo.SpecialCharges))
		: AmmoName(ETNRallyAmmo::Coco);
	const float HalfWidth = Box.GetSize().X * 0.5f - 18.f;
	Painter.Text(Selected, FitFont(Painter, Selected, 22, 14, HalfWidth), Box.Min + FVector2f(14.f, 8.f), FVector2f::ZeroVector,
		bSpecialSelected ? Highlight : TNHUDArt::Foam);
	// Calor de la torreta bajo el nombre.
	const FBox2f HeatBack(Box.Min + FVector2f(14.f, 44.f), FVector2f(Box.Min.X + 14.f + HalfWidth, Box.Min.Y + 54.f));
	Painter.Box(HeatBack, Brushes().Chip, Screen);
	const float Heat = FMath::Clamp(Ammo.Heat01, 0.f, 1.f);
	if (Heat > 0.01f)
	{
		Painter.Box(FBox2f(HeatBack.Min, FVector2f(HeatBack.Min.X + HeatBack.GetSize().X * Heat, HeatBack.Max.Y)), Brushes().Chip,
			Ammo.bOverheated ? TNHUDArt::CoralC : FMath::Lerp(Highlight, TNHUDArt::CoralC, Heat));
	}
	// A la derecha: la especial en reserva o, si no hay, la próxima caja.
	const FText Right = (bHasSpecial && !bSpecialSelected)
		? FText::Format(NSLOCTEXT("Rally", "TabletCharges", "{0} ×{1}"), AmmoName(Ammo.Special), TNLocText::Int(Ammo.SpecialCharges))
		: NextBoxText();
	Painter.Text(Right, FitFont(Painter, Right, 20, 12, HalfWidth), FVector2f(Box.Max.X - 14.f, Box.GetCenter().Y), FVector2f(1.f, 0.5f),
		bHasSpecial && !bSpecialSelected ? TNHUDArt::Foam : Highlight);
}

void UTN_RallyCopilotTablet::PaintMap(FTNRallyTabletPainter& Painter, const FBox2D& Area, bool bSmall) const
{
	using namespace TNRallyTablet;
	using namespace TNRallyTabletPaint;
	Painter.Box(ToBox(Area), Brushes().CardPanel, Card);
	const FTNRallyTabletMapProjection Projection = FTNRallyTabletMapProjection::Make(MapBounds, Area, bSmall ? 22.f : 34.f);
	const int32 Stride = bSmall ? 4 : 2;
	TArray<FVector2f> Axis;
	for (int32 Index = 0; Index < TrackNotes.Points.Num(); Index += Stride)
	{
		Axis.Add(Projection(TrackNotes.Points[Index]));
	}
	Axis.Add(Projection(TrackNotes.bClosed ? TrackNotes.Points[0] : TrackNotes.Points.Last()));

	const float Width = bSmall ? 5.f : 8.f;
	Painter.Lines(Axis, RoadEdge, Width + 5.f);
	Painter.Lines(Axis, Road, Width);
	PaintMapWater(Painter, TrackNotes, Projection, Width);
	if (bHasArc)
	{
		PaintMapWindow(Painter, TrackNotes, Projection, MyArcCm, LookAheadCm, Width * 0.6f);
	}
	PaintMapEnds(Painter, TrackNotes, Projection, bSmall ? 8.f : 12.f);
	PaintMarks(Painter, Projection, bSmall);
}

void UTN_RallyCopilotTablet::PaintMarks(FTNRallyTabletPainter& Painter, const FTNRallyTabletMapProjection& Projection, bool bSmall) const
{
	using namespace TNRallyTablet;
	const float Radius = bSmall ? 10.f : 15.f;
	const FSlateFontInfo Font = TNHUDStyle::Font(TEXT("Bold"), bSmall ? 12 : 15);
	// Primero los rivales y encima el propio, con un aro de oro que late.
	for (const bool bMinePass : { false, true })
	{
		for (const FTNRallyTabletMark& Mark : Marks)
		{
			if (Mark.bMine != bMinePass)
			{
				continue;
			}
			const FVector2f Center = Projection(Mark.Location);
			const float ShellRadius = Mark.bMine ? Radius * 1.3f : Radius;
			if (Mark.bMine)
			{
				const float Pulse = ShellRadius + 6.f + 3.f * FMath::Sin(Clock * 5.f);
				Painter.Lines(TNRallyTabletPaint::RingPoints(Center, Pulse, 28), Highlight, 3.f);
			}
			TNRallyTabletPaint::PaintShell(Painter, Center, ShellRadius, Mark.Color, Mark.bOut);
			if (Mark.Place > 0 && (!bSmall || Mark.bMine))
			{
				Painter.Text(TNLocText::Int(Mark.Place), Font, Center, FVector2f(0.5f, 0.5f), TNHUDArt::Cream);
			}
		}
	}
}

void UTN_RallyCopilotTablet::PaintProfile(FTNRallyTabletPainter& Painter, const FBox2D& Area) const
{
	using namespace TNRallyTablet;
	using namespace TNRallyTabletPaint;
	Painter.Box(ToBox(Area), Brushes().CardPanel, Card);
	if (!bHasArc)
	{
		return;
	}
	const FBox2f Plot = ProfilePlot(Area);
	const FProfile Profile = SampleProfile(TrackNotes, MyArcCm, LookAheadCm);
	PaintProfileGround(Painter, TrackNotes, Profile, Plot, LookAheadCm);
	PaintProfileTicks(Painter, Profile, Plot, LookAheadCm);
	PaintProfileNotes(Painter, Area);
	PaintProfileBoxes(Painter, TrackNotes, Profile, Plot, MyArcCm, BoxesAhead, LookAheadCm);
	// El buggy propio al principio de la tira.
	if (Profile.Heights.Num() > 0)
	{
		const FVector2f Here(Plot.Min.X, ProfileY(Plot, Profile, Profile.Heights[0]) - 10.f);
		PaintShell(Painter, Here, 10.f, MyColor, false);
	}
}

void UTN_RallyCopilotTablet::PaintProfileNotes(FTNRallyTabletPainter& Painter, const FBox2D& Area) const
{
	using namespace TNRallyTablet;
	using namespace TNRallyTabletPaint;
	const FBox2f Plot = ProfilePlot(Area);
	const FSlateFontInfo GradeFont = TNHUDStyle::Font(TEXT("Bold"), 18);
	constexpr float IconSize = 30.f;
	// Dos filas: una nota que caería encima de la anterior sube a la fila de arriba.
	float LastX[2] = { -1000.f, -1000.f };
	for (int32 Index = 0; Index < Ahead.Num(); ++Index)
	{
		const FPaceNote& Note = Ahead[Index].Note;
		const float X = ProfileX(Plot, Ahead[Index].DistanceCm, LookAheadCm);
		const int32 Row = (X - LastX[0] < IconSize + 14.f) ? 1 : 0;
		LastX[Row] = X;
		const FVector2f Center(X, Plot.Min.Y - 20.f - Row * 34.f);
		Painter.Lines({ FVector2f(X, Center.Y + IconSize * 0.5f), FVector2f(X, Plot.Max.Y) }, FLinearColor(1.f, 1.f, 1.f, 0.22f), 1.5f);
		if (Index == 0)
		{
			Painter.Circle(Center, IconSize * 0.62f, Brushes().Circle, FLinearColor(Highlight.R, Highlight.G, Highlight.B, 0.3f));
		}
		PaintNoteIcon(Painter, Note, Center, IconSize);
		if (Note.Kind == ENoteKind::Turn)
		{
			const float Side = Note.Direction == ETurnDirection::Left ? 1.f : -1.f;
			Painter.Text(TNLocText::Int(Note.Grade), GradeFont, Center + FVector2f(Side * IconSize * 0.62f, 0.f),
				FVector2f(Side > 0.f ? 0.f : 1.f, 0.5f), NoteColor(Note));
		}
		if (Note.bDontCut)
		{
			Painter.Text(TNLocText::Literal(TEXT("!")), GradeFont, Center + FVector2f(0.f, -IconSize * 0.55f), FVector2f(0.5f, 1.f),
				TNHUDArt::CoralC);
		}
	}
}

void UTN_RallyCopilotTablet::PaintNextNote(FTNRallyTabletPainter& Painter, const FBox2D& Area, bool bSmall) const
{
	using namespace TNRallyTablet;
	using namespace TNRallyTabletPaint;
	const FBox2f Box = ToBox(Area);
	Painter.Box(Box, Brushes().CardPanel, Card);
	if (Ahead.Num() == 0)
	{
		Painter.Text(NSLOCTEXT("Rally", "TabletNoNotes", "Todo recto"), TNHUDStyle::Font(TEXT("Bold"), bSmall ? 24 : 36),
			Box.GetCenter(), FVector2f(0.5f, 0.5f), Dim);
		return;
	}
	const FNoteAhead& Next = Ahead[0];
	const float IconSize = bSmall ? 46.f : 92.f;
	const float TextX = Box.Min.X + IconSize + (bSmall ? 22.f : 40.f);
	const float TextWidth = Box.Max.X - TextX - 14.f;
	PaintNoteIcon(Painter, Next.Note, FVector2f(Box.Min.X + 12.f + IconSize * 0.5f, Box.GetCenter().Y), IconSize);

	// Distancia en oro; a menos de 50 m late en coral: hay que cantarla ya.
	const bool bNow = Next.DistanceCm < 5000.0;
	const float Blink = bNow ? 0.6f + 0.4f * FMath::Abs(FMath::Sin(Clock * 8.f)) : 1.f;
	const FLinearColor DistanceColor = bNow ? TNHUDArt::CoralLight * FLinearColor(1.f, 1.f, 1.f, Blink) : Highlight;
	Painter.Text(DistanceText(Next.DistanceCm), TNHUDStyle::Font(TEXT("Bold"), bSmall ? 20 : 28),
		FVector2f(TextX, Box.Min.Y + (bSmall ? 10.f : 16.f)), FVector2f::ZeroVector, DistanceColor);

	const FText Call = NoteText(Next.Note);
	Painter.Text(Call, FitFont(Painter, Call, bSmall ? 30 : 52, bSmall ? 16 : 24, TextWidth),
		FVector2f(TextX, Box.Min.Y + (bSmall ? 66.f : 90.f)), FVector2f(0.f, 0.5f), NoteColor(Next.Note));
	if (!bSmall && Ahead.Num() > 1)
	{
		const FText Then = FText::Format(NSLOCTEXT("Rally", "TabletThen", "luego {0} a {1}"), NoteText(Ahead[1].Note),
			DistanceText(Ahead[1].DistanceCm - Next.DistanceCm));
		Painter.Text(Then, FitFont(Painter, Then, 22, 14, TextWidth), FVector2f(TextX, Box.Max.Y - 16.f), FVector2f(0.f, 1.f), Dim);
	}
}

void UTN_RallyCopilotTablet::PaintAmmo(FTNRallyTabletPainter& Painter, const FBox2D& Area) const
{
	using namespace TNRallyTablet;
	using namespace TNRallyTabletPaint;
	const FBrushes& Brush = Brushes();
	const FBox2f Box = ToBox(Area);
	Painter.Box(Box, Brush.CardPanel, Card);
	Painter.Text(NSLOCTEXT("Rally", "TabletAmmo", "Munición"), TNHUDStyle::Font(TEXT("Bold"), 18),
		Box.Min + FVector2f(18.f, 12.f), FVector2f::ZeroVector, Dim);
	// La próxima fila de cajas («la que va a coger»), arriba a la derecha y en el perfil.
	Painter.Text(NextBoxText(), TNHUDStyle::Font(TEXT("Bold"), 18), FVector2f(Box.Max.X - 18.f, Box.Min.Y + 12.f), FVector2f(1.f, 0.f),
		Highlight);

	const FBox2f CocoChip(Box.Min + FVector2f(18.f, 44.f), Box.Min + FVector2f(298.f, 122.f));
	const FBox2f SpecialChip(Box.Min + FVector2f(318.f, 44.f), Box.Max - FVector2f(18.f, 16.f));
	const bool bHasSpecial = Ammo.Special != ETNRallyAmmo::None && Ammo.SpecialCharges > 0;
	const bool bCocoSelected = Ammo.Selected == ETNRallyAmmo::Coco;
	const bool bSpecialSelected = bHasSpecial && Ammo.Selected == Ammo.Special;
	// La elegida lleva un filo de oro.
	if (bCocoSelected) { Painter.Box(CocoChip.ExpandBy(4.f), Brush.Chip, Highlight); }
	Painter.Box(CocoChip, Brush.Chip, TNHUDArt::Navy);
	if (bSpecialSelected) { Painter.Box(SpecialChip.ExpandBy(4.f), Brush.Chip, Highlight); }
	Painter.Box(SpecialChip, Brush.Chip, TNHUDArt::Navy);

	// Coco: sin límite, con el calor de la torreta.
	Painter.Text(AmmoName(ETNRallyAmmo::Coco), TNHUDStyle::Font(TEXT("Bold"), 26), CocoChip.Min + FVector2f(14.f, 8.f),
		FVector2f::ZeroVector, TNHUDArt::Foam);
	const FBox2f HeatBack(CocoChip.Min + FVector2f(14.f, 52.f), FVector2f(CocoChip.Max.X - 14.f, CocoChip.Min.Y + 66.f));
	Painter.Box(HeatBack, Brush.Chip, Screen);
	const float Heat = FMath::Clamp(Ammo.Heat01, 0.f, 1.f);
	if (Heat > 0.01f)
	{
		const FBox2f HeatFill(HeatBack.Min, FVector2f(HeatBack.Min.X + HeatBack.GetSize().X * Heat, HeatBack.Max.Y));
		Painter.Box(HeatFill, Brush.Chip, Ammo.bOverheated ? TNHUDArt::CoralC : FMath::Lerp(Highlight, TNHUDArt::CoralC, Heat));
	}
	if (Ammo.bOverheated)
	{
		Painter.Text(NSLOCTEXT("Rally", "TabletOverheated", "¡Caliente!"), TNHUDStyle::Font(TEXT("Bold"), 18),
			FVector2f(CocoChip.Max.X - 14.f, CocoChip.Min.Y + 12.f), FVector2f(1.f, 0.f), TNHUDArt::CoralC);
	}

	const FText Special = bHasSpecial
		? FText::Format(NSLOCTEXT("Rally", "TabletCharges", "{0} ×{1}"), AmmoName(Ammo.Special), TNLocText::Int(Ammo.SpecialCharges))
		: NSLOCTEXT("Rally", "TabletNoSpecial", "Sin carga especial");
	Painter.Text(Special, TNHUDStyle::Font(bHasSpecial ? TEXT("Bold") : TEXT("Regular"), bHasSpecial ? 28 : 20),
		FVector2f(SpecialChip.Min.X + 16.f, SpecialChip.GetCenter().Y), FVector2f(0.f, 0.5f), bHasSpecial ? TNHUDArt::Foam : Dim);
}
