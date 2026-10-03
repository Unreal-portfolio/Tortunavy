#include "TN_TctItemArt.h"
#include "Engine/Texture2D.h"
#include "Misc/App.h"
#include "../UI/HUD/TN_HUDArt.h"

namespace TNTctItemArtDetail
{
	using namespace TNHUDArt;

	constexpr int32 IconSize = 128;

	/** Grados a radianes (TNHUDArt::Arc va en radianes). */
	float Rad(float Degrees) { return FMath::DegreesToRadians(Degrees); }

	/** Silueta con filo oscuro y degradado vertical. */
	template <typename FSdf>
	void Body(FPainter& Painter, const FSdf& Sdf, uint32 Edge, uint32 Top, uint32 Bottom, float TopY, float BottomY)
	{
		Painter.Fill([&Sdf](float px, float py) { return Sdf(px, py) - 2.f; }, Hex(Edge));
		const FLinearColor TopColor = Hex(Top);
		const FLinearColor BottomColor = Hex(Bottom);
		Painter.Layer(Sdf, [&TopColor, &BottomColor, TopY, BottomY](float, float py)
		{
			return Mix(TopColor, BottomColor, (py - TopY) / FMath::Max(1.f, BottomY - TopY));
		});
	}

	/** Pistola genérica (cuerpo, cañón y culata) con el color del objeto. */
	void Pistol(FPainter& Painter, uint32 Edge, uint32 Top, uint32 Bottom, float BarrelLength, float BarrelRadius)
	{
		const auto Grip = [](float px, float py) { return Box(px, py, 46.f, 86.f, 11.f, 24.f, 6.f); };
		const auto Frame = [](float px, float py) { return Box(px, py, 58.f, 58.f, 30.f, 13.f, 7.f); };
		const auto Barrel = [BarrelLength, BarrelRadius](float px, float py)
		{
			return Segment(px, py, 70.f, 54.f, 70.f + BarrelLength, 54.f, BarrelRadius);
		};
		const auto All = [&](float px, float py) { return FMath::Min(FMath::Min(Grip(px, py), Frame(px, py)), Barrel(px, py)); };
		Painter.Sticker(All, 6.f);
		Body(Painter, Grip, 0x2A1A10, 0x8A5A34, 0x5A3820, 62.f, 110.f);
		Body(Painter, Frame, Edge, Top, Bottom, 45.f, 71.f);
		Body(Painter, Barrel, Edge, Top, Bottom, 40.f, 68.f);
	}

	/** Dibuja el objeto en Painter. false si no tiene icono. */
	bool PaintKind(FPainter& Painter, ETNTctItem Kind)
	{
		switch (Kind)
		{
		case ETNTctItem::KnockoutPistol:
			Pistol(Painter, 0x3A1030, 0xFF7AD9, 0xB0308F, 40.f, 7.f);
			Painter.Fill([](float px, float py) { return Circle(px, py, 116.f, 54.f, 6.f); }, Hex(0xFFE36B));
			break;
		case ETNTctItem::InkPistol:
			Pistol(Painter, 0x101830, 0x5A6AA0, 0x2A3460, 46.f, 7.f);
			Painter.Fill([](float px, float py) { return Drop(px, py, 112.f, 82.f, 9.f); }, Hex(0x141420));
			break;
		case ETNTctItem::AirBlunderbuss:
		{
			const TArray<FVector2f> Bell = { { 70.f, 50.f }, { 116.f, 30.f }, { 116.f, 78.f }, { 70.f, 62.f } };
			Pistol(Painter, 0x1A2A3A, 0x9AD8F0, 0x3A88B0, 20.f, 8.f);
			const auto Mouth = [&Bell](float px, float py) { return Polygon(px, py, Bell); };
			Body(Painter, Mouth, 0x1A2A3A, 0xC8F0FF, 0x5AA8D0, 30.f, 78.f);
			break;
		}
		case ETNTctItem::Grapple:
		{
			const auto Shaft = [](float px, float py) { return Segment(px, py, 30.f, 98.f, 78.f, 40.f, 7.f); };
			const auto HookL = [](float px, float py) { return Arc(px, py, 70.f, 40.f, 20.f, Rad(180.f), Rad(300.f), 6.f); };
			const auto HookR = [](float px, float py) { return Arc(px, py, 86.f, 48.f, 20.f, Rad(240.f), Rad(420.f), 6.f); };
			const auto All = [&](float px, float py) { return FMath::Min(Shaft(px, py), FMath::Min(HookL(px, py), HookR(px, py))); };
			Painter.Sticker(All, 6.f);
			Body(Painter, All, 0x202428, 0xD8E0E8, 0x6A7480, 20.f, 104.f);
			break;
		}
		case ETNTctItem::Shovel:
		{
			const auto Handle = [](float px, float py) { return Segment(px, py, 26.f, 104.f, 70.f, 54.f, 6.f); };
			const auto Blade = [](float px, float py) { return Ellipse(px, py, 84.f, 38.f, 20.f, 28.f); };
			const auto All = [&](float px, float py) { return FMath::Min(Handle(px, py), Blade(px, py)); };
			Painter.Sticker(All, 6.f);
			Body(Painter, Handle, 0x3A2010, 0xD89A50, 0x8A5A24, 50.f, 108.f);
			Body(Painter, Blade, 0x5A1010, 0xFF6A52, 0xD9432F, 10.f, 66.f);
			break;
		}
		case ETNTctItem::BeachBall:
		{
			const auto Ball = [](float px, float py) { return Circle(px, py, 64.f, 64.f, 42.f); };
			Painter.Sticker(Ball, 6.f);
			Body(Painter, Ball, 0x203040, 0xFFFFFF, 0xE8E8F0, 22.f, 106.f);
			Painter.Fill([&](float px, float py) { return FMath::Max(Ball(px, py) + 1.f, Ellipse(px, py, 46.f, 64.f, 12.f, 40.f)); }, Hex(0xFF5A5A));
			Painter.Fill([&](float px, float py) { return FMath::Max(Ball(px, py) + 1.f, Ellipse(px, py, 82.f, 64.f, 12.f, 40.f)); }, Hex(0x3A8AFF));
			Painter.Fill([](float px, float py) { return Circle(px, py, 64.f, 30.f, 8.f); }, Hex(0xFFD23F));
			break;
		}
		case ETNTctItem::Anchor:
		{
			const auto Stem = [](float px, float py) { return Box(px, py, 64.f, 62.f, 6.f, 36.f, 3.f); };
			const auto Ring = [](float px, float py) { return FMath::Abs(Circle(px, py, 64.f, 22.f, 10.f)) - 4.f; };
			const auto Bar = [](float px, float py) { return Box(px, py, 64.f, 44.f, 24.f, 5.f, 3.f); };
			const auto Hook = [](float px, float py) { return Arc(px, py, 64.f, 78.f, 30.f, Rad(20.f), Rad(160.f), 6.f); };
			const auto All = [&](float px, float py)
			{
				return FMath::Min(FMath::Min(Stem(px, py), Ring(px, py)), FMath::Min(Bar(px, py), Hook(px, py)));
			};
			Painter.Sticker(All, 6.f);
			Body(Painter, All, 0x101418, 0x7A8490, 0x2A3038, 12.f, 112.f);
			break;
		}
		case ETNTctItem::JellyDart:
		{
			const auto Shaft = [](float px, float py) { return Segment(px, py, 22.f, 100.f, 92.f, 30.f, 5.f); };
			const auto Bell = [](float px, float py) { return Ellipse(px, py, 92.f, 32.f, 18.f, 14.f); };
			const auto All = [&](float px, float py) { return FMath::Min(Shaft(px, py), Bell(px, py)); };
			Painter.Sticker(All, 6.f);
			Body(Painter, Shaft, 0x30204A, 0xB89AE8, 0x6A4AA0, 30.f, 104.f);
			Body(Painter, Bell, 0x4A1A5A, 0xF0B0FF, 0xB060D0, 18.f, 46.f);
			break;
		}
		default:
			return false;
		}
		return true;
	}

	/** El icono del objeto con un punto por carga abajo a la derecha (hasta cuatro; ninguno con una sola). */
	UTexture2D* PaintWithCharges(ETNTctItem Kind, int32 Charges)
	{
		FPainter Painter(IconSize, IconSize);
		if (!PaintKind(Painter, Kind))
		{
			return nullptr;
		}
		const int32 Shown = Charges > 1 ? FMath::Min(Charges, 4) : 0;
		for (int32 Pip = 0; Pip < Shown; ++Pip)
		{
			const float Cx = 114.f - Pip * 15.f;
			Painter.Fill([Cx](float px, float py) { return Circle(px, py, Cx, 116.f, 7.f); }, Hex(0x0A1C38));
			Painter.Fill([Cx](float px, float py) { return Circle(px, py, Cx, 116.f, 4.5f); }, Hex(0xFFCB3D));
		}
		return Painter.ToTexture(*FString::Printf(TEXT("TN_Tct_Item_%d_%d"), static_cast<int32>(Kind), Charges));
	}
}

UTexture2D* TNTctItemArt::GetIcon(ETNTctItem Kind, int32 Charges)
{
	if (IsRunningDedicatedServer() || !FApp::CanEverRender() || Kind == ETNTctItem::None || Kind >= ETNTctItem::Count)
	{
		return nullptr;
	}
	const FName Key(*FString::Printf(TEXT("TctItem_%d_%d"), static_cast<int32>(Kind), FMath::Clamp(Charges, 0, 4)));
	return TNHUDArt::Cached(Key, [Kind, Charges]() -> UTexture2D* { return TNTctItemArtDetail::PaintWithCharges(Kind, Charges); });
}
