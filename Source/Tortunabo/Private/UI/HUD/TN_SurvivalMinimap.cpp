// Dibujo del minimapa de Supervivencia (UTN_SurvivalMinimap): la tableta compacta del Rally con el camino del mapa
// generado y la tormenta encima. Las medidas, los colores y los caparazones son los de la tableta (TN_RallyCopilotTabletDraw.h).

#include "UI/HUD/TN_SurvivalMinimap.h"

#include "../../Rally/UI/TN_RallyCopilotTabletDraw.h"
#include "EngineUtils.h"
#include "TN_HUDArt.h"
#include "World/ProcMap/TN_ProcMapEnums.h"
#include "World/ProcMap/TN_PathStorm.h"
#include "World/ProcMap/TN_ProcMapGenerator.h"

namespace TNSurvivalMinimapDetail
{
	/** Pantalla del mapa dentro de la funda (la misma que el mapa de la tableta compacta). */
	const FBox2D MapArea = TNRallyTablet::MakeArea(28.f, 28.f, 304.f, 304.f);
	/** Margen del mapa hasta el borde de la pantalla (unidades de diseño). */
	constexpr float MapPadding = 22.f;
	/** Ancho del camino, como el de la tableta compacta. */
	constexpr float RoadWidth = 5.f;
	constexpr float ShellRadius = 10.f;
	/** Muestras como mucho del camino en el mapa: a este tamaño no se notan más. */
	constexpr int32 MaxPathPoints = 240;
	/** Nube del frente (proporción de TNHUDArt::StormIcon, 128 × 104). */
	const FVector2f CloudSize(54.f, 44.f);

	/** Tramo tragado por la tormenta (una nube ancha sobre el camino oscurecido) y su frente. */
	const FLinearColor StormFill = TNHUDArt::Hex(0x2B2540, 0.62f);
	const FLinearColor StormRoad = TNHUDArt::Hex(0x3E3658);
	constexpr float StormBandWidth = 22.f;
	constexpr float FrontBarHalf = 13.f;
	const FLinearColor StormEdge = TNHUDArt::Hex(0x5B4F86);
	const FLinearColor StormEdgeLight = TNHUDArt::Hex(0xC9C2EE);

	/** Point dentro de Area, a Inset de sus bordes. */
	FVector2f ClampInto(const FBox2D& Area, const FVector2f& Point, float Inset)
	{
		return FVector2f(FMath::Clamp(Point.X, static_cast<float>(Area.Min.X) + Inset, static_cast<float>(Area.Max.X) - Inset),
			FMath::Clamp(Point.Y, static_cast<float>(Area.Min.Y) + Inset, static_cast<float>(Area.Max.Y) - Inset));
	}

	/** Raya corta de lado a lado del camino en Here (salida y meta, como en la tableta). */
	void PaintEndBar(FTNRallyTabletPainter& Painter, const FVector2f& Here, const FVector2f& Toward, const FLinearColor& Color, float Length)
	{
		const FVector2f Dir = (Toward - Here).GetSafeNormal();
		const FVector2f Normal(-Dir.Y, Dir.X);
		Painter.Lines({ Here - Normal * Length, Here + Normal * Length }, Color, Length * 0.45f);
	}
}

void UTN_SurvivalMinimap::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::HitTestInvisible);
	StormCloudBrush.SetResourceObject(TNHUDArt::StormIcon());
	StormCloudBrush.ImageSize = FVector2D(128.f, 104.f);
	StormCloudBrush.DrawAs = ESlateBrushDrawType::Image;
}

void UTN_SurvivalMinimap::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Clock += InDeltaTime;
	UWorld* World = GetWorld();
	LookupTimer -= InDeltaTime;
	if (World && (!Generator.IsValid() || !Storm.IsValid()) && LookupTimer <= 0.f)
	{
		LookupTimer = 1.f;
		if (!Generator.IsValid()) { for (TActorIterator<ATN_ProcMapGenerator> It(World); It; ++It) { Generator = *It; break; } }
		if (!Storm.IsValid()) { for (TActorIterator<ATN_PathStorm> It(World); It; ++It) { Storm = *It; break; } }
	}
	const bool bHadMap = HasMap();
	RefreshPath();
	RefreshStorm();
	// Se mueve todo cada fotograma (tortugas, tormenta y el aro que late).
	if (HasMap() || bHadMap)
	{
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void UTN_SurvivalMinimap::RefreshPath()
{
	const ATN_ProcMapGenerator* Gen = Generator.Get();
	const bool bSurvivalMap = Gen && Gen->IsMapReady() && Gen->GetNetConfig().Mode == ETNProcGameMode::Survival;
	if (!bSurvivalMap)
	{
		Path.Reset();
		PathProgress.Reset();
		PathWet.Reset();
		PathGeneration = -1;
		PathLength = -1.f;
		return;
	}
	const int32 Generation = Gen->GetRequestedGeneration();
	const float Length = Gen->GetMainPathLength();
	if (HasMap() && Generation == PathGeneration && FMath::IsNearlyEqual(Length, PathLength))
	{
		return;
	}
	PathGeneration = Generation;
	PathLength = Length;

	TArray<FTNProcPathPoint> Points;
	Gen->GetMainPathWorld(Points);
	Path.Reset();
	PathProgress.Reset();
	PathWet.Reset();
	MapBounds = FBox2D(ForceInit);
	const float SeaZ = Gen->GetSeaLevelWorldZ();
	const int32 Stride = FMath::Max(1, FMath::DivideAndRoundUp(Points.Num(), TNSurvivalMinimapDetail::MaxPathPoints));
	for (int32 Index = 0; Index < Points.Num(); Index += Stride)
	{
		// La última muestra siempre: es la meta.
		const FTNProcPathPoint& Point = Points[FMath::Min(Index, Points.Num() - 1)];
		Path.Add(Point.Location);
		PathProgress.Add(Gen->GetPathProgress(Point.Location));
		PathWet.Add(Point.Location.Z < SeaZ);
		MapBounds += FVector2D(Point.Location.X, Point.Location.Y);
	}
	if (Points.Num() > 0 && (Points.Num() - 1) % Stride != 0)
	{
		Path.Add(Points.Last().Location);
		PathProgress.Add(Length);
		PathWet.Add(Points.Last().Location.Z < SeaZ);
		MapBounds += FVector2D(Points.Last().Location.X, Points.Last().Location.Y);
	}
}

void UTN_SurvivalMinimap::RefreshStorm()
{
	const ATN_PathStorm* PathStorm = Storm.Get();
	const ATN_ProcMapGenerator* Gen = Generator.Get();
	bHasStorm = PathStorm && Gen && PathStorm->IsStormActive() && HasMap();
	if (bHasStorm)
	{
		// En el cliente, el frente replicado y extrapolado (el mismo con el que la tormenta decide quién está dentro).
		StormFront = PathStorm->GetFrontProgress();
		StormFrontLocation = Gen->GetPathLocationAtProgress(FMath::Clamp(StormFront, 0.f, PathLength), StormFrontDirection);
	}
}

int32 UTN_SurvivalMinimap::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace TNRallyTablet;
	using namespace TNSurvivalMinimapDetail;
	const int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!HasMap())
	{
		return Layer;
	}
	FTNRallyTabletPainter Painter(OutDrawElements, AllottedGeometry, Layer, InWidgetStyle.GetColorAndOpacityTint().A);
	TNRallyTabletLayout::FFit Fit;
	Fit.Design = FVector2f(DesignSide);
	Painter.Fit(Fit);

	// Funda y pantalla de la tableta compacta, con el mapa arriba como el de la conductora.
	const FBrushes& Brush = Brushes();
	Painter.Box(FBox2f(FVector2f::ZeroVector, Fit.Design), Brush.Frame, Bumper);
	Painter.Box(FBox2f(FVector2f(CompactBumper), Fit.Design - FVector2f(CompactBumper)), Brush.ScreenPanel, Screen);
	Painter.Box(ToBox(MapArea), Brush.CardPanel, Card);
	const FTNRallyTabletMapProjection Projection = FTNRallyTabletMapProjection::Make(MapBounds, MapArea, MapPadding);

	// El camino y la tormenta, recortados a la pantalla (sin las esquinas redondeadas).
	const FBox2D Inner = MapArea.ExpandBy(-4.0);
	OutDrawElements.PushClip(FSlateClippingZone(Painter.At(FVector2f(Inner.Min), FVector2f(Inner.GetSize()))));
	PaintPath(Painter, Projection);
	PaintStorm(Painter, Projection);
	OutDrawElements.PopClip();

	PaintStormCloud(Painter, Projection, MapArea);
	PaintMarks(Painter, Projection, MapArea);
	return Painter.Layer;
}

void UTN_SurvivalMinimap::PaintPath(FTNRallyTabletPainter& Painter, const FTNRallyTabletMapProjection& Projection) const
{
	using namespace TNRallyTablet;
	using namespace TNSurvivalMinimapDetail;
	TArray<FVector2f> Axis;
	Axis.Reserve(Path.Num());
	for (const FVector& Point : Path)
	{
		Axis.Add(Projection(Point));
	}
	Painter.Lines(Axis, RoadEdge, RoadWidth + 5.f);
	Painter.Lines(Axis, Road, RoadWidth);

	// Los tramos bajo el nivel del mar, en agua encima de la arena.
	TArray<FVector2f> Wet;
	for (int32 Index = 0; Index <= Axis.Num(); ++Index)
	{
		if (Index < Axis.Num() && PathWet[Index])
		{
			Wet.Add(Axis[Index]);
			continue;
		}
		Painter.Lines(Wet, Water, RoadWidth);
		Wet.Reset();
	}

	PaintEndBar(Painter, Axis[0], Axis[1], TNHUDArt::Cream, 8.f);
	PaintEndBar(Painter, Axis.Last(), Axis.Last(1), Highlight, 8.f);
}

FVector2f UTN_SurvivalMinimap::FrontDirectionOnPanel(const FTNRallyTabletMapProjection& Projection) const
{
	const FVector2f Dir = Projection(StormFrontLocation + StormFrontDirection * 1000.0) - Projection(StormFrontLocation);
	return Dir.IsNearlyZero() ? FVector2f(0.f, -1.f) : Dir.GetSafeNormal();
}

void UTN_SurvivalMinimap::PaintStorm(FTNRallyTabletPainter& Painter, const FTNRallyTabletMapProjection& Projection) const
{
	using namespace TNSurvivalMinimapDetail;
	if (!bHasStorm)
	{
		return;
	}
	// El tramo tragado: de la salida hasta el frente, una nube ancha y el camino oscurecido debajo.
	const FVector2f Front = Projection(StormFrontLocation);
	if (StormFront > 0.f)
	{
		TArray<FVector2f> Swallowed;
		for (int32 Index = 0; Index < Path.Num() && PathProgress[Index] < StormFront; ++Index)
		{
			Swallowed.Add(Projection(Path[Index]));
		}
		Swallowed.Add(Front);
		Painter.Lines(Swallowed, StormFill, StormBandWidth);
		Painter.Lines(Swallowed, StormRoad, RoadWidth);
	}

	// El frente, atravesado en el camino, con una raya clara que late.
	const FVector2f Dir = FrontDirectionOnPanel(Projection);
	const FVector2f Across = FVector2f(-Dir.Y, Dir.X) * FrontBarHalf;
	Painter.Lines({ Front - Across, Front + Across }, StormEdge, 6.f);
	const float Pulse = 0.55f + 0.45f * FMath::Sin(Clock * 4.f);
	Painter.Lines({ Front - Across, Front + Across }, StormEdgeLight * FLinearColor(1.f, 1.f, 1.f, Pulse), 2.f);
}

void UTN_SurvivalMinimap::PaintStormCloud(FTNRallyTabletPainter& Painter, const FTNRallyTabletMapProjection& Projection, const FBox2D& Area) const
{
	using namespace TNSurvivalMinimapDetail;
	if (!bHasStorm)
	{
		return;
	}
	// Detrás del frente, para no tapar a la tortuga que va justo delante; antes de salir, junto a la salida.
	const FVector2f Behind = Projection(StormFrontLocation) - FrontDirectionOnPanel(Projection) * (CloudSize.Y * 0.6f);
	const FVector2f Center = ClampInto(Area, Behind, CloudSize.X * 0.5f) + FVector2f(0.f, 2.f * FMath::Sin(Clock * 3.f));
	FSlateDrawElement::MakeBox(Painter.Out, ++Painter.Layer, Painter.At(Center - CloudSize * 0.5f, CloudSize), &StormCloudBrush,
		ESlateDrawEffect::None, Painter.Fade(FLinearColor::White));
}

void UTN_SurvivalMinimap::PaintMarks(FTNRallyTabletPainter& Painter, const FTNRallyTabletMapProjection& Projection, const FBox2D& Area) const
{
	using namespace TNSurvivalMinimapDetail;
	// Primero las compañeras y encima la tuya, con un aro de oro que late. Fuera del mapa (el corral de LVL_Run), en su borde.
	for (const bool bMinePass : { false, true })
	{
		for (const FTNSurvivalMinimapMark& Mark : Marks)
		{
			if (Mark.bMine != bMinePass)
			{
				continue;
			}
			const float Radius = Mark.bMine ? ShellRadius * 1.3f : ShellRadius;
			const FVector2f Center = ClampInto(Area, Projection(Mark.Location), Radius + 4.f);
			if (Mark.bMine)
			{
				const float Pulse = Radius + 6.f + 3.f * FMath::Sin(Clock * 5.f);
				Painter.Lines(TNRallyTabletPaint::RingPoints(Center, Pulse, 28), TNRallyTablet::Highlight, 3.f);
			}
			TNRallyTabletPaint::PaintShell(Painter, Center, Radius, Mark.Color, Mark.bOut);
		}
	}
}
