#include "TN_InkScreen.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Styling/SlateBrush.h"
#include "UObject/Package.h"
#include "Widgets/Images/SImage.h"
#include "../UI/HUD/TN_HUDArt.h"

namespace TNInkScreenDetail
{
	/** Tamaño de la textura de las manchas (16:9; se estira a la pantalla). */
	constexpr int32 SplatWidth = 320;
	constexpr int32 SplatHeight = 180;
	/** Segundos del final en que la tinta se desvanece. */
	constexpr double FadeSeconds = 0.8;
	/** Por debajo del HUD (los widgets del juego van en 0 o más): tapa el mundo, no los marcadores. */
	constexpr int32 ViewportZOrder = -10;

	/** La imagen de la tinta de un jugador local y cuándo se acaba (reloj de la plataforma). */
	struct FInkState
	{
		TWeakPtr<SWidget> Widget;
		TSharedPtr<double> EndTime;
	};

	TMap<TWeakObjectPtr<const ULocalPlayer>, FInkState>& States()
	{
		static TMap<TWeakObjectPtr<const ULocalPlayer>, FInkState> Map;
		return Map;
	}

	/** Una mancha de las manchas de tinta: centro y radio en píxeles de la textura, y la semilla de sus ondas. */
	struct FSplat
	{
		float X, Y, Radius, Seed;
	};

	/** Distancia a una mancha de radio Radius con el borde ondulado (Seed cambia las ondas). */
	float SplatSdf(float px, float py, float Cx, float Cy, float Radius, float Seed)
	{
		const float Angle = FMath::Atan2(py - Cy, px - Cx);
		const float Wave = 0.12f * FMath::Sin(5.f * Angle + Seed) + 0.08f * FMath::Sin(9.f * Angle + 2.3f * Seed) + 0.05f * FMath::Sin(14.f * Angle + Seed * 0.7f);
		return TNHUDArt::Circle(px, py, Cx, Cy, 0.f) - Radius * (1.f + Wave);
	}

	UTexture2D* PaintSplats()
	{
		using namespace TNHUDArt;
		FPainter Painter(SplatWidth, SplatHeight);
		// Manchas (x, y, radio, semilla) en píxeles de la textura: tapan casi todo y dejan huecos para asomarse.
		const FSplat Splats[] = {
			{ 96.f, 62.f, 44.f, 0.4f }, { 210.f, 52.f, 40.f, 1.7f }, { 158.f, 112.f, 50.f, 2.9f }, { 54.f, 136.f, 30.f, 4.1f },
			{ 268.f, 128.f, 36.f, 5.3f }, { 150.f, 30.f, 24.f, 6.2f }, { 22.f, 40.f, 20.f, 7.4f }, { 300.f, 36.f, 18.f, 8.8f } };
		const auto InkShape = [&Splats](float px, float py)
		{
			float Dist = 1e9f;
			for (const FSplat& Splat : Splats)
			{
				Dist = FMath::Min(Dist, SplatSdf(px, py, Splat.X, Splat.Y, Splat.Radius, Splat.Seed));
				// Un chorrito que cae de cada mancha, con la gota al final.
				const float DripX = Splat.X + Splat.Radius * 0.3f * FMath::Sin(Splat.Seed * 3.f);
				const float DripEnd = Splat.Y + Splat.Radius * (1.2f + 0.4f * FMath::Frac(Splat.Seed));
				Dist = FMath::Min(Dist, Segment(px, py, DripX, Splat.Y, DripX, DripEnd, Splat.Radius * 0.08f + 1.5f));
				Dist = FMath::Min(Dist, Circle(px, py, DripX, DripEnd, Splat.Radius * 0.13f + 2.f));
			}
			return Dist;
		};
		Painter.Fill(InkShape, Hex(0x141420, 0.94f));
		// Brillo húmedo arriba a la izquierda de cada mancha.
		for (const FSplat& Splat : Splats)
		{
			Painter.Fill([&Splat](float px, float py)
			{
				return Ellipse(px, py, Splat.X - Splat.Radius * 0.35f, Splat.Y - Splat.Radius * 0.4f, Splat.Radius * 0.22f, Splat.Radius * 0.1f);
			}, Hex(0x4A4A70, 0.55f));
		}
		return Painter.ToTexture(TEXT("TN_InkSplats"));
	}

	const FSlateBrush* InkBrush()
	{
		static FSlateBrush Brush;
		UTexture2D* Texture = TNInkScreen::SplatTexture();
		if (!Texture)
		{
			return nullptr;
		}
		if (Brush.GetResourceObject() != Texture)
		{
			Brush.SetResourceObject(Texture);
			Brush.SetImageSize(FVector2D(SplatWidth, SplatHeight));
			Brush.DrawAs = ESlateBrushDrawType::Image;
		}
		return &Brush;
	}

	ULocalPlayer* LocalPlayerOf(const APlayerController* PC)
	{
		return PC && PC->IsLocalController() ? PC->GetLocalPlayer() : nullptr;
	}
}

bool TNInkScreen::NeedsFallback(const UMaterialInterface* Material)
{
	return !Material || Material->GetPathName().StartsWith(TEXT("/Engine/"));
}

float TNInkScreen::OpacityAt(double SecondsLeft)
{
	return static_cast<float>(FMath::Clamp(SecondsLeft / TNInkScreenDetail::FadeSeconds, 0.0, 1.0));
}

UTexture2D* TNInkScreen::SplatTexture(bool bEvenHeadless)
{
	if (!bEvenHeadless && (IsRunningDedicatedServer() || !FApp::CanEverRender()))
	{
		return nullptr;
	}
	return TNHUDArt::Cached(TEXT("InkSplats"), []() -> UTexture2D* { return TNInkScreenDetail::PaintSplats(); });
}

void TNInkScreen::Show(const APlayerController* PC, float Duration)
{
	ULocalPlayer* LocalPlayer = TNInkScreenDetail::LocalPlayerOf(PC);
	UGameViewportClient* Viewport = LocalPlayer ? LocalPlayer->ViewportClient.Get() : nullptr;
	const FSlateBrush* Brush = Viewport ? TNInkScreenDetail::InkBrush() : nullptr;
	if (!Brush)
	{
		return;
	}
	TNInkScreenDetail::FInkState& State = TNInkScreenDetail::States().FindOrAdd(LocalPlayer);
	// La imagen se queda en la pantalla, oculta, para la próxima vez; al cambiar de mapa el motor la quita y se vuelve a crear.
	if (!State.Widget.IsValid() || !State.EndTime.IsValid())
	{
		TSharedRef<double> EndTime = MakeShared<double>(0.0);
		TSharedRef<SImage> Image = SNew(SImage)
			.Image(Brush)
			.Visibility_Lambda([EndTime]() { return FPlatformTime::Seconds() < *EndTime ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			.ColorAndOpacity_Lambda([EndTime]()
			{
				return FSlateColor(FLinearColor(1.f, 1.f, 1.f, TNInkScreen::OpacityAt(*EndTime - FPlatformTime::Seconds())));
			});
		Viewport->AddViewportWidgetForPlayer(LocalPlayer, Image, TNInkScreenDetail::ViewportZOrder);
		State.Widget = Image;
		State.EndTime = EndTime;
	}
	*State.EndTime = FMath::Max(*State.EndTime, FPlatformTime::Seconds() + FMath::Max(0.1, static_cast<double>(Duration)));
}

void TNInkScreen::Hide(const APlayerController* PC)
{
	const ULocalPlayer* LocalPlayer = TNInkScreenDetail::LocalPlayerOf(PC);
	if (TNInkScreenDetail::FInkState* State = LocalPlayer ? TNInkScreenDetail::States().Find(LocalPlayer) : nullptr)
	{
		if (State->EndTime.IsValid())
		{
			*State->EndTime = 0.0;
		}
	}
}
