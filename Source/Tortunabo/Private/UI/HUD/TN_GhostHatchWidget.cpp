#include "UI/HUD/TN_GhostHatchWidget.h"
#include "TN_HUDArt.h"
#include "TN_HUDFonts.h"
#include "../Loading/STN_EggLoadingScreen.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Math/RandomStream.h"
#include "Misc/App.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "TimerManager.h"
#include "UI/Loading/TN_LoadingScreenSubsystem.h"
#include "UI/TN_ScreenHost.h"

namespace TNGhostHatchDetail
{
	/** Por encima del HUD, las ruedas y los menús de la partida; por debajo del menú de pausa y de la pantalla de carga. */
	constexpr int32 ZOrder = UTN_GhostHatchWidget::ViewportZOrder;
	/**
	 * Modo carrera: lo que recorre cada mitad al entrar (fracción del alto de la pantalla: su unión empieza fuera, más allá
	 * de los dientes) y lo que tarda en fundirse cuando se abre sin fiesta.
	 */
	constexpr float CurtainTravel = 0.58f;
	constexpr float CurtainFadeSeconds = 0.25f;
	/** Dientes de la unión en zigzag y su alto (fracción del alto de la pantalla). */
	constexpr int32 SeamTeeth = 12;
	constexpr float ToothHeight = 0.034f;
	/** Los tres «pum» del huevo, en fracción del rato entre el ¡pum! y la eclosión (casi a la vez que los del mundo). */
	constexpr float KnockFractions[3] = { 0.2f, 0.5f, 0.8f };
	/** Lo que tarda una grieta en abrirse, la apertura de las mitades y cuándo se quita todo después de abrirse. */
	constexpr float CrackGrowSeconds = 0.2f;
	constexpr float OpenSeconds = 0.6f;
	constexpr float FinishAfterOpen = 0.95f;
	/** Si la tortuga tarda en llegar a esta máquina, se abre igualmente pasado este rato. */
	constexpr float WaitPawnTimeout = 2.5f;
	/** Segundos que se ve el «¡PUM!». */
	constexpr float PumSeconds = 0.7f;

	/** Cáscara oscura: casi negra por fuera, gris violácea junto a la unión (donde le llega la luz de dentro). */
	const FLinearColor ShellOuter = TNHUDArt::Hex(0x07080F);
	const FLinearColor ShellInner = TNHUDArt::Hex(0x262A45);
	const FLinearColor ShellRim = TNHUDArt::Hex(0x4B5585);
	const FLinearColor ShellSpeck = TNHUDArt::Hex(0x3A4066, 0.5f);
	/** Luz de dentro del huevo: núcleo casi blanco y resplandor dorado. */
	const FLinearColor LightCore = TNHUDArt::Hex(0xFFF4D6);
	const FLinearColor LightGlow = TNHUDArt::Hex(0xFFC766);
	const FLinearColor ShardTint = TNHUDArt::Hex(0x3A3F60);

	/** Triángulos en el espacio de la geometría del widget, listos para MakeCustomVerts (como la pantalla de carga). */
	struct FMeshBuilder
	{
		TArray<FSlateVertex> Verts;
		TArray<SlateIndex> Indices;
		FSlateRenderTransform Transform;

		explicit FMeshBuilder(const FGeometry& Geometry)
			: Transform(Geometry.GetAccumulatedRenderTransform())
		{
		}

		int32 AddVertex(const FVector2f& Local, const FLinearColor& Color)
		{
			// Slate guarda el color de vértice en sRGB (como el tinte de las cajas).
			FSlateVertex Vertex = FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform, Local, FVector2f(0.5f, 0.5f), Color.ToFColor(true));
			Vertex.MaterialTexCoords = FVector2f::ZeroVector;
			Vertex.PixelSize[0] = 0;
			Vertex.PixelSize[1] = 0;
			return Verts.Add(Vertex);
		}

		void AddQuad(int32 A, int32 B, int32 C, int32 D)
		{
			Indices.Add(static_cast<SlateIndex>(A));
			Indices.Add(static_cast<SlateIndex>(B));
			Indices.Add(static_cast<SlateIndex>(C));
			Indices.Add(static_cast<SlateIndex>(A));
			Indices.Add(static_cast<SlateIndex>(C));
			Indices.Add(static_cast<SlateIndex>(D));
		}
	};

	/** Postura de una mitad: giro (radianes) alrededor de Pivot y desplazamiento (px). */
	struct FHalfPose
	{
		FVector2f Offset = FVector2f::ZeroVector;
		float Angle = 0.f;
		FVector2f Pivot = FVector2f::ZeroVector;

		FVector2f Apply(const FVector2f& Point) const
		{
			const FVector2f Delta = Point - Pivot;
			const float Cosine = FMath::Cos(Angle);
			const float Sine = FMath::Sin(Angle);
			return Pivot + FVector2f(Delta.X * Cosine - Delta.Y * Sine, Delta.X * Sine + Delta.Y * Cosine) + Offset;
		}
	};

	void SetImageBrush(FSlateBrush& Brush, UTexture2D* Texture)
	{
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.Tiling = ESlateBrushTileType::NoTile;
		if (Texture)
		{
			Brush.SetResourceObject(Texture);
			Brush.ImageSize = FVector2D(static_cast<double>(Texture->GetSizeX()), static_cast<double>(Texture->GetSizeY()));
		}
	}
}

void UTN_GhostHatchWidget::ShowFor(APlayerController* PC, float SecondsToDark, float SecondsToHatch)
{
	if (!PC || !PC->IsLocalController() || PC->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	UTN_GhostHatchWidget* Widget = CreateWidget<UTN_GhostHatchWidget>(PC, UTN_GhostHatchWidget::StaticClass());
	if (!Widget)
	{
		return;
	}
	Widget->Begin(SecondsToDark, SecondsToHatch);
	TNScreen::AddToScreen(Widget, TNGhostHatchDetail::ZOrder);
}

UTN_GhostHatchWidget* UTN_GhostHatchWidget::ShowCurtain(APlayerController* PC, float CloseSeconds, float MaxHoldSeconds)
{
	if (!PC || !PC->IsLocalController() || PC->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	UTN_GhostHatchWidget* Widget = CreateWidget<UTN_GhostHatchWidget>(PC, UTN_GhostHatchWidget::StaticClass());
	if (!Widget)
	{
		return nullptr;
	}
	Widget->BeginCurtain(CloseSeconds, MaxHoldSeconds);
	TNScreen::AddToScreen(Widget, TNGhostHatchDetail::ZOrder);
	return Widget;
}

void UTN_GhostHatchWidget::NativeOnInitialized()
{
	if (WidgetTree && !Root)
	{
		Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
		WidgetTree->RootWidget = Root;
	}
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

float UTN_GhostHatchWidget::Elapsed() const
{
	return static_cast<float>(FPlatformTime::Seconds() - StartTime);
}

void UTN_GhostHatchWidget::Begin(float SecondsToDark, float SecondsToHatch)
{
	using namespace TNGhostHatchDetail;
	StartTime = FPlatformTime::Seconds();
	DarkAt = FMath::Max(0.25f, SecondsToDark);
	HatchAt = FMath::Max(DarkAt + 0.5f, SecondsToHatch);
	KnockTimes.Reset();
	for (const float Fraction : KnockFractions)
	{
		KnockTimes.Add(DarkAt + (HatchAt - DarkAt) * Fraction);
	}
	BuildShell();
}

void UTN_GhostHatchWidget::BeginCurtain(float CloseSeconds, float MaxHoldSeconds)
{
	// Sin fundido a negro ni golpes programados: las mitades entran desde fuera y se juntan en DarkAt; los «pum» los da
	// Knock y la abre Open (o se rompe sola en HatchAt, si nadie lo hace).
	bCurtain = true;
	StartTime = FPlatformTime::Seconds();
	DarkAt = FMath::Max(0.08f, CloseSeconds);
	HatchAt = DarkAt + FMath::Max(1.f, MaxHoldSeconds);
	KnockTimes.Reset();
	KnocksDone = 0;
	BuildShell();
}

void UTN_GhostHatchWidget::BuildShell()
{
	using namespace TNGhostHatchDetail;
	// Unión en zigzag, grietas, trozos y motas: una semilla por transición (0-1 de la pantalla).
	FRandomStream Random(static_cast<int32>(FPlatformTime::Cycles() & 0x7fffffff));
	Seam.Reset();
	const int32 SeamPoints = SeamTeeth * 2 + 1;
	for (int32 i = 0; i < SeamPoints; ++i)
	{
		const float X = -0.02f + 1.04f * static_cast<float>(i) / (SeamPoints - 1);
		const float Tooth = ((i % 2) ? 1.f : -1.f) * ToothHeight * Random.FRandRange(0.6f, 1.25f);
		Seam.Add(FVector2f(X, 0.5f + Tooth + 0.012f * FMath::Sin(X * 7.f)));
	}
	Cracks.Reset();
	for (int32 KnockIndex = 1; KnockIndex <= 3; ++KnockIndex)
	{
		for (const bool bTop : { true, false })
		{
			FTNGhostHatchCrack Crack;
			Crack.bTop = bTop;
			Crack.Knock = KnockIndex;
			const FVector2f& From = Seam[Random.RandRange(2, SeamPoints - 3)];
			FVector2f Point = From;
			Crack.Points.Add(Point);
			const int32 Segments = Random.RandRange(4, 6);
			const float Length = Random.FRandRange(0.1f, 0.2f) + 0.04f * KnockIndex;
			for (int32 s = 0; s < Segments; ++s)
			{
				Point += FVector2f(Random.FRandRange(-0.05f, 0.05f), (bTop ? -1.f : 1.f) * Length / Segments);
				Crack.Points.Add(Point);
			}
			Cracks.Add(Crack);
			// Una rama pequeña que sale de la mitad de la grieta.
			FTNGhostHatchCrack Branch;
			Branch.bTop = bTop;
			Branch.Knock = KnockIndex;
			FVector2f BranchPoint = Crack.Points[Segments / 2];
			Branch.Points.Add(BranchPoint);
			for (int32 s = 0; s < 2; ++s)
			{
				BranchPoint += FVector2f(Random.FRandRange(0.03f, 0.06f) * (Random.FRand() > 0.5f ? 1.f : -1.f), (bTop ? -1.f : 1.f) * 0.03f);
				Branch.Points.Add(BranchPoint);
			}
			Cracks.Add(Branch);
		}
	}
	Shards.Reset();
	for (int32 i = 0; i < 18; ++i)
	{
		FTNGhostHatchShard Shard;
		const FVector2f& From = Seam[Random.RandRange(0, SeamPoints - 1)];
		const bool bUp = (i % 2) == 0;
		Shard.Origin = From;
		Shard.Velocity = FVector2f(Random.FRandRange(-0.45f, 0.45f), (bUp ? -1.f : 1.f) * Random.FRandRange(0.5f, 1.1f));
		Shard.Spin = Random.FRandRange(-7.f, 7.f);
		Shard.Size = Random.FRandRange(22.f, 52.f);
		Shard.Shape = i % 3;
		Shards.Add(Shard);
	}
	Speckles.Reset();
	for (int32 i = 0; i < 70; ++i)
	{
		Speckles.Add(FVector2f(Random.FRand(), Random.FRand()));
	}

	// Pinceles: el blanco y los trozos de cáscara de la pantalla de carga del huevo.
	SetImageBrush(WhiteBrush, TNEggLoadingArt::White());
	for (int32 i = 0; i < 3; ++i)
	{
		SetImageBrush(ShardBrushes[i], TNEggLoadingArt::Shard(i % TNEggLoadingArt::NumShardShapes));
	}
	PumFont = TNHUDFonts::Make(TEXT("Black"), 64);
	PumFont.OutlineSettings.OutlineSize = 4;
	PumFont.OutlineSettings.OutlineColor = FLinearColor(0.02f, 0.02f, 0.05f, 1.f);

	// Los sonidos del huevo de la pantalla de carga, en la interfaz (2D).
	APlayerController* PC = GetOwningPlayer();
	if (PC && FApp::CanEverRenderAudio())
	{
		Synth = NewObject<UTN_EggSynthComponent>(PC, NAME_None, RF_Transient);
		if (USceneComponent* RootComp = PC->GetRootComponent())
		{
			Synth->SetupAttachment(RootComp);
		}
		Synth->RegisterComponent();
		Synth->KeepAwake();
	}
}

void UTN_GhostHatchWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	using namespace TNGhostHatchDetail;
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bFinished)
	{
		return;
	}
	const APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		Finish();
		return;
	}
	const float Now = Elapsed();
	if (bCurtain)
	{
		TickCurtain(Now);
		return;
	}
	// ¡Pum!: la cáscara se cierra de golpe.
	if (!bSlamDone && Now >= DarkAt)
	{
		bSlamDone = true;
		if (Synth)
		{
			Synth->PlayPop();
		}
	}
	// Pum, pum, pum: se resquebraja.
	while (OpenAt < 0.f && KnocksDone < KnockTimes.Num() && Now >= KnockTimes[KnocksDone])
	{
		if (Synth)
		{
			Synth->PlayKnock(0.6f + 0.1f * KnocksDone);
			Synth->PlayCrack(0.45f + 0.2f * KnocksDone);
		}
		++KnocksDone;
	}
	// Se abre cuando el huevo eclosiona y ya se tiene la tortuga (o, si tarda en llegar, al rato).
	if (OpenAt < 0.f && Now >= HatchAt)
	{
		const APawn* Pawn = PC->GetPawn();
		if ((Pawn && Pawn->IsLocallyControlled()) || Now >= HatchAt + WaitPawnTimeout)
		{
			OpenAt = Now;
			KnocksDone = KnockTimes.Num();
			if (Synth)
			{
				Synth->PlayCrack(1.f);
				Synth->PlayWhoosh(0.9f);
			}
		}
	}
	if ((OpenAt >= 0.f && Now >= OpenAt + FinishAfterOpen) || Now > HatchAt + WaitPawnTimeout + 3.f)
	{
		Finish();
		return;
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

void UTN_GhostHatchWidget::TickCurtain(float Now)
{
	using namespace TNGhostHatchDetail;
	// ¡Clac!: las dos mitades se juntan.
	if (!bSlamDone && Now >= DarkAt)
	{
		bSlamDone = true;
		if (Synth)
		{
			Synth->PlayKnock(1.f);
		}
	}
	// Nadie la abre: se rompe sola pasado el tiempo de espera (nunca se queda la pantalla tapada).
	if (OpenAt < 0.f && FadeOutAt < 0.f && Now >= HatchAt)
	{
		Open(true);
	}
	if (FadeOutAt >= 0.f)
	{
		const float Out = FMath::Clamp((Now - FadeOutAt) / CurtainFadeSeconds, 0.f, 1.f);
		SetRenderOpacity(1.f - Out);
		if (Out >= 1.f)
		{
			Finish();
			return;
		}
	}
	if (OpenAt >= 0.f && Now >= OpenAt + FinishAfterOpen)
	{
		Finish();
		return;
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

void UTN_GhostHatchWidget::Knock()
{
	if (!bCurtain || IsOpening())
	{
		return;
	}
	KnockTimes.Add(Elapsed());
	KnocksDone = KnockTimes.Num();
	if (Synth)
	{
		const float Step = static_cast<float>(FMath::Min(KnocksDone - 1, 3));
		Synth->PlayKnock(0.6f + 0.1f * Step);
		Synth->PlayCrack(0.45f + 0.2f * Step);
	}
}

void UTN_GhostHatchWidget::Open(bool bBurst)
{
	if (IsOpening())
	{
		return;
	}
	const float Now = Elapsed();
	if (!bBurst)
	{
		FadeOutAt = Now;
		return;
	}
	OpenAt = Now;
	KnocksDone = KnockTimes.Num();
	if (Synth)
	{
		Synth->PlayCrack(1.f);
		Synth->PlayWhoosh(0.9f);
	}
}

bool UTN_GhostHatchWidget::IsClosed() const
{
	return !IsOpening() && Elapsed() >= DarkAt;
}

void UTN_GhostHatchWidget::Finish()
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;
	SetVisibility(ESlateVisibility::Collapsed);
	// Se quita en el fotograma siguiente, no mientras Slate lo está recorriendo.
	TWeakObjectPtr<UTN_GhostHatchWidget> WeakThis(this);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([WeakThis]()
		{
			if (UTN_GhostHatchWidget* Self = WeakThis.Get())
			{
				Self->RemoveFromParent();
			}
		}));
	}
	else
	{
		RemoveFromParent();
	}
}

void UTN_GhostHatchWidget::NativeDestruct()
{
	if (Synth)
	{
		Synth->Stop();
		Synth->DestroyComponent();
		Synth = nullptr;
	}
	Super::NativeDestruct();
}

int32 UTN_GhostHatchWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace TNGhostHatchDetail;
	const int32 BaseLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const FVector2f ScreenSize = FVector2f(AllottedGeometry.GetLocalSize());
	const float Sw = ScreenSize.X;
	const float Sh = ScreenSize.Y;
	if (bFinished || Sw < 2.f || Sh < 2.f || Seam.Num() < 2)
	{
		return BaseLayer;
	}
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
	const float Now = Elapsed();
	const bool bOpening = OpenAt >= 0.f;
	const float SinceOpen = bOpening ? Now - OpenAt : 0.f;

	// ── 1. Todo negro mientras el fantasma se mete en el huevo (y detrás de la cáscara hasta que se abre) ──
	// En el modo carrera no: se ve la partida por la rendija hasta que las mitades se juntan.
	const float Black = (bOpening || bCurtain) ? 0.f : FMath::SmoothStep(FMath::Max(0.f, DarkAt - 0.4f), DarkAt, Now);
	if (Black > 0.f)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, BaseLayer + 1, AllottedGeometry.ToPaintGeometry(), &WhiteBrush, ESlateDrawEffect::None,
			FLinearColor(0.f, 0.f, 0.f, Black) * Tint);
	}
	if (!bCurtain && Now < DarkAt)
	{
		return BaseLayer + 1;
	}

	// ── 2. La cáscara oscura: dos mitades que se estampan (¡pum!), tiemblan con cada golpe y al final salen despedidas ──
	const float SinceSlam = Now - DarkAt;
	float Jolt = SinceSlam >= 0.f ? FMath::Max(0.f, 1.f - SinceSlam / 0.3f) : 0.f;
	for (int32 k = 0; k < KnocksDone && k < KnockTimes.Num(); ++k)
	{
		if (Now >= KnockTimes[k])
		{
			Jolt = FMath::Max(Jolt, FMath::Max(0.f, 1.f - (Now - KnockTimes[k]) / 0.25f) * (0.6f + 0.15f * FMath::Min(k, 3)));
		}
	}
	const float Shake = bOpening ? 0.f : Jolt * Jolt * 12.f * FMath::Sin(Now * 70.f);
	// Revivir: las mitades se estampan con un rebote. Carrera: entran desde fuera de la pantalla, cada vez más deprisa, y se
	// juntan en DarkAt (sin rebote: nunca se vuelve a ver la partida hasta que se abre).
	const float Slam = bCurtain ? 0.f : FMath::Square(FMath::Max(0.f, 1.f - SinceSlam / 0.14f)) * 0.08f * Sh;
	const float Slide = bCurtain ? (1.f - FMath::Square(FMath::Clamp(Now / DarkAt, 0.f, 1.f))) * CurtainTravel * Sh : 0.f;
	const float OpenEase = bOpening ? FMath::Square(FMath::Clamp(SinceOpen / OpenSeconds, 0.f, 1.f)) : 0.f;
	FHalfPose TopPose;
	TopPose.Pivot = FVector2f(0.15f * Sw, 0.5f * Sh);
	TopPose.Offset = FVector2f(Shake, -Slam - Slide - OpenEase * 1.15f * Sh);
	TopPose.Angle = -0.14f * OpenEase;
	FHalfPose BottomPose;
	BottomPose.Pivot = FVector2f(0.85f * Sw, 0.5f * Sh);
	BottomPose.Offset = FVector2f(-0.7f * Shake, Slam + Slide + OpenEase * 1.15f * Sh);
	BottomPose.Angle = 0.11f * OpenEase;
	// Luz que se cuela: tenue tras el ¡pum!, más con cada golpe y con destellos en cada uno.
	const float Light = FMath::Clamp(0.15f + 0.25f * FMath::Min(KnocksDone, 3) + 0.5f * Jolt, 0.f, 1.3f);

	if (OpenEase < 1.f)
	{
		FMeshBuilder Mesh(AllottedGeometry);
		for (const bool bTop : { true, false })
		{
			const FHalfPose& Pose = bTop ? TopPose : BottomPose;
			const float OuterY = bTop ? -0.15f * Sh : 1.15f * Sh;
			const float RimSide = bTop ? -5.f : 5.f;
			const FLinearColor Warm = FMath::Lerp(ShellInner, LightGlow * 0.55f, FMath::Clamp(0.25f * Light, 0.f, 0.4f));
			const int32 First = Mesh.Verts.Num();
			for (const FVector2f& SeamPoint : Seam)
			{
				const FVector2f AtSeam(SeamPoint.X * Sw, SeamPoint.Y * Sh);
				const FVector2f AtMid(AtSeam.X, FMath::Lerp(AtSeam.Y, OuterY, 0.3f));
				const FVector2f AtOuter(AtSeam.X, OuterY);
				Mesh.AddVertex(Pose.Apply(AtSeam), Warm * Tint);
				Mesh.AddVertex(Pose.Apply(AtMid), FMath::Lerp(ShellInner, ShellOuter, 0.55f) * Tint);
				Mesh.AddVertex(Pose.Apply(AtOuter), ShellOuter * Tint);
			}
			for (int32 i = 0; i + 1 < Seam.Num(); ++i)
			{
				const int32 A = First + i * 3;
				Mesh.AddQuad(A, A + 3, A + 4, A + 1);
				Mesh.AddQuad(A + 1, A + 4, A + 5, A + 2);
			}
			// Filo de la cáscara junto a la unión.
			const int32 RimFirst = Mesh.Verts.Num();
			for (const FVector2f& SeamPoint : Seam)
			{
				const FVector2f AtSeam(SeamPoint.X * Sw, SeamPoint.Y * Sh);
				Mesh.AddVertex(Pose.Apply(AtSeam), ShellRim * Tint);
				Mesh.AddVertex(Pose.Apply(AtSeam + FVector2f(0.f, RimSide)), ShellRim * Tint);
			}
			for (int32 i = 0; i + 1 < Seam.Num(); ++i)
			{
				const int32 A = RimFirst + i * 2;
				Mesh.AddQuad(A, A + 2, A + 3, A + 1);
			}
		}
		FSlateDrawElement::MakeCustomVerts(OutDrawElements, BaseLayer + 2, WhiteBrush.GetRenderingResource(), Mesh.Verts, Mesh.Indices, nullptr, 0, 0);

		// Motas de la cáscara (van con su mitad).
		for (const FVector2f& Speck : Speckles)
		{
			const bool bTop = Speck.Y < 0.5f;
			const FVector2f Where = (bTop ? TopPose : BottomPose).Apply(FVector2f(Speck.X * Sw, Speck.Y * Sh));
			FSlateDrawElement::MakeBox(OutDrawElements, BaseLayer + 3, AllottedGeometry.ToPaintGeometry(FVector2f(5.f, 5.f), FSlateLayoutTransform(Where)),
				&WhiteBrush, ESlateDrawEffect::None, ShellSpeck * Tint);
		}
	}

	// ── 3. Líneas de luz: la unión (mientras está cerrada) y las grietas de cada golpe ──
	if (!bOpening && SinceSlam >= 0.f)
	{
		TArray<FVector2f> SeamLine;
		for (const FVector2f& SeamPoint : Seam)
		{
			SeamLine.Add(TopPose.Apply(FVector2f(SeamPoint.X * Sw, SeamPoint.Y * Sh)));
		}
		TArray<FVector2f> SeamCore = SeamLine;
		FSlateDrawElement::MakeLines(OutDrawElements, BaseLayer + 4, AllottedGeometry.ToPaintGeometry(), MoveTemp(SeamLine), ESlateDrawEffect::None,
			FLinearColor(LightGlow.R, LightGlow.G, LightGlow.B, 0.22f * FMath::Min(1.f, Light)) * Tint, true, 6.f + 16.f * Light);
		FSlateDrawElement::MakeLines(OutDrawElements, BaseLayer + 4, AllottedGeometry.ToPaintGeometry(), MoveTemp(SeamCore), ESlateDrawEffect::None,
			FLinearColor(LightCore.R, LightCore.G, LightCore.B, FMath::Min(1.f, 0.35f + 0.6f * Light)) * Tint, true, 1.5f + 2.5f * Light);
	}
	if (OpenEase < 1.f)
	{
		for (const FTNGhostHatchCrack& Crack : Cracks)
		{
			if (Crack.Knock > KnocksDone || !KnockTimes.IsValidIndex(Crack.Knock - 1) || Crack.Points.Num() < 2)
			{
				continue;
			}
			const float Grow = bOpening ? 1.f : FMath::Clamp((Now - KnockTimes[Crack.Knock - 1]) / CrackGrowSeconds, 0.f, 1.f);
			const FHalfPose& Pose = Crack.bTop ? TopPose : BottomPose;
			const float Reach = Grow * (Crack.Points.Num() - 1);
			TArray<FVector2f> Line;
			for (int32 i = 0; i < Crack.Points.Num(); ++i)
			{
				if (i <= Reach)
				{
					Line.Add(Pose.Apply(FVector2f(Crack.Points[i].X * Sw, Crack.Points[i].Y * Sh)));
				}
				else
				{
					const float Part = Reach - (i - 1);
					if (Part > 0.f)
					{
						const FVector2f Partial = FMath::Lerp(Crack.Points[i - 1], Crack.Points[i], Part);
						Line.Add(Pose.Apply(FVector2f(Partial.X * Sw, Partial.Y * Sh)));
					}
					break;
				}
			}
			if (Line.Num() < 2)
			{
				continue;
			}
			TArray<FVector2f> Core = Line;
			FSlateDrawElement::MakeLines(OutDrawElements, BaseLayer + 4, AllottedGeometry.ToPaintGeometry(), MoveTemp(Line), ESlateDrawEffect::None,
				FLinearColor(LightGlow.R, LightGlow.G, LightGlow.B, 0.3f) * Tint, true, 11.f);
			FSlateDrawElement::MakeLines(OutDrawElements, BaseLayer + 4, AllottedGeometry.ToPaintGeometry(), MoveTemp(Core), ESlateDrawEffect::None,
				LightCore * Tint, true, 2.5f);
		}
	}

	// ── 4. «¡PUM!» al estamparse (en el modo carrera no: encima va el puesto o el título de la ronda) ──
	if (!bCurtain && SinceSlam < PumSeconds && !bOpening)
	{
		const FString PumText = NSLOCTEXT("TNLoading", "EggPum", "¡PUM!").ToString();
		const TSharedRef<FSlateFontMeasure> Measurer = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FVector2f TextSize = Measurer->Measure(PumText, PumFont, 1.f);
		const float Pop = SinceSlam < 0.12f ? FMath::Lerp(0.6f, 1.15f, SinceSlam / 0.12f) : FMath::Lerp(1.15f, 1.f, FMath::Min(1.f, (SinceSlam - 0.12f) / 0.2f));
		const float Fade = 1.f - FMath::Clamp((SinceSlam - (PumSeconds - 0.25f)) / 0.25f, 0.f, 1.f);
		const FVector2f Where(0.5f * Sw - 0.5f * TextSize.X * Pop, 0.5f * Sh - 0.5f * TextSize.Y * Pop - 0.12f * Sh);
		FSlateDrawElement::MakeText(OutDrawElements, BaseLayer + 5, AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(Pop, Where)),
			PumText, PumFont, ESlateDrawEffect::None, FLinearColor(LightCore.R, LightCore.G, LightCore.B, Fade) * Tint);
	}

	// ── 5. Al abrirse: fogonazo y trozos de cáscara que salen despedidos ──
	if (bOpening)
	{
		const float Flash = FMath::Max(0.f, 1.f - SinceOpen / 0.35f);
		if (Flash > 0.f)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, BaseLayer + 6, AllottedGeometry.ToPaintGeometry(), &WhiteBrush, ESlateDrawEffect::None,
				FLinearColor(LightCore.R, LightCore.G, LightCore.B, 0.6f * Flash) * Tint);
		}
		const float ShardFade = FMath::Clamp(1.f - (SinceOpen - 0.5f) / 0.4f, 0.f, 1.f);
		for (const FTNGhostHatchShard& Shard : Shards)
		{
			const FVector2f Where(
				(Shard.Origin.X + Shard.Velocity.X * SinceOpen) * Sw,
				(Shard.Origin.Y + Shard.Velocity.Y * SinceOpen + 0.9f * SinceOpen * SinceOpen) * Sh);
			const float ShardSize = Shard.Size * (1.f + 0.4f * SinceOpen);
			FSlateDrawElement::MakeRotatedBox(OutDrawElements, BaseLayer + 7,
				AllottedGeometry.ToPaintGeometry(FVector2f(ShardSize, ShardSize), FSlateLayoutTransform(Where - FVector2f(0.5f * ShardSize, 0.5f * ShardSize))),
				&ShardBrushes[Shard.Shape % 3], ESlateDrawEffect::None, Shard.Spin * SinceOpen, TOptional<FVector2f>(), FSlateDrawElement::RelativeToElement,
				FLinearColor(ShardTint.R, ShardTint.G, ShardTint.B, ShardFade) * Tint);
		}
	}
	return BaseLayer + 7;
}
