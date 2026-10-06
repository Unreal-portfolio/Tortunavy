#pragma once

#include "CoreMinimal.h"
#include "TN_HUDArt.h"

/** Estado de la cara de la tortuga en el distintivo del HUD (y en los carteles de derribo, eliminación y resultados). */
enum class ETNTurtleFace : uint8
{
	Happy,
	Shell,
	Down,
	Win,
};

/**
 * Caras de tortuga en estilo cartoon para el HUD Tortunavy: pegatinas vectoriales (formas con distancia con signo y
 * antialias, como las de TN_HUDArt) de 256x256, una por estado. Feliz; caparazón cerrado cuando se mete dentro;
 * mareada (ojos en cruz y estrellitas) al quedar eliminada; y con ojos de estrella al ganar. La estamina no se ve en
 * la interfaz: no hay caras de cansancio.
 * Es un boceto para el equipo de arte: la geometría está pensada para pasarse tal cual a SVG.
 */
namespace TNHUDFaces
{
	using namespace TNHUDArt;

	constexpr int32 Size = 256;
	constexpr float Cx = 128.f;
	constexpr float HeadY = 140.f;
	constexpr float HeadRx = 98.f;
	constexpr float HeadRy = 84.f;
	constexpr float EyeY = 120.f;
	constexpr float EyeRx = 25.f;
	constexpr float EyeRy = 29.f;
	constexpr float EyeDx = 36.f;

	namespace FaceColors
	{
		inline const FLinearColor HeadTop = Hex(0x8BDB5F);
		inline const FLinearColor HeadBottom = Hex(0x4FA83E);
		inline const FLinearColor Outline = Hex(0x1E4F26);
		inline const FLinearColor Spot = Hex(0x5FB548, 0.55f);
		inline const FLinearColor Snout = Hex(0xB9EA8C, 0.9f);
		inline const FLinearColor Blush = Hex(0xFF8FA3, 0.5f);
		inline const FLinearColor EyeLine = Hex(0x1E3326);
		inline const FLinearColor Pupil = Hex(0x16202A);
		inline const FLinearColor Mouth = Hex(0x6B1B26);
		inline const FLinearColor Tongue = Hex(0xFF6F8E);
		inline const FLinearColor StarFill = Hex(0xFFD23F);
		inline const FLinearColor StarLine = Hex(0xB8741A);
	}

	inline float Head(float x, float y) { return Ellipse(x, y, Cx, HeadY, HeadRx, HeadRy); }

	inline FLinearColor HeadColor(float y) { return Mix(FaceColors::HeadTop, FaceColors::HeadBottom, (y - (HeadY - HeadRy)) / (2.f * HeadRy)); }

	/** Cabeza: filo oscuro, relleno con degradado, pecas de la piel, hocico claro y orificios de la nariz. */
	inline void DrawHead(FPainter& P)
	{
		P.Fill([](float x, float y) { return Head(x, y) - 4.f; }, FaceColors::Outline);
		P.Layer(Head, [](float, float y) { return HeadColor(y); });
		P.Clip(40.f, 50.f, 216.f, 120.f);
		P.Fill([](float x, float y)
		{
			return FMath::Min(FMath::Min(Ellipse(x, y, 70.f, 84.f, 9.f, 6.f), Ellipse(x, y, 190.f, 90.f, 8.f, 5.f)), FMath::Min(Ellipse(x, y, 128.f, 70.f, 11.f, 6.f), Ellipse(x, y, 160.f, 66.f, 6.f, 4.f)));
		}, FaceColors::Spot);
		P.Clip(60.f, 140.f, 196.f, 216.f);
		P.Fill([](float x, float y) { return FMath::Max(Ellipse(x, y, Cx, 180.f, 62.f, 34.f), Head(x, y) + 3.f); }, FaceColors::Snout);
		P.Fill([](float x, float y) { return FMath::Min(Ellipse(x, y, 119.f, 152.f, 3.4f, 2.4f), Ellipse(x, y, 137.f, 152.f, 3.4f, 2.4f)); }, Hex(0x1E4F26, 0.7f));
		P.NoClip();
	}

	inline void DrawBlush(FPainter& P, float Alpha, float Scale = 1.f)
	{
		P.Clip(40.f, 145.f, 216.f, 185.f);
		P.Fill([Scale](float x, float y) { return FMath::Min(Ellipse(x, y, 68.f, 166.f, 17.f * Scale, 10.f * Scale), Ellipse(x, y, 188.f, 166.f, 17.f * Scale, 10.f * Scale)); },
			Hex(0xFF8FA3, Alpha));
		P.NoClip();
	}

	/** Ojos abiertos con pupila y brillos. */
	inline void DrawEyes(FPainter& P)
	{
		for (const float Side : { -1.f, 1.f })
		{
			const float Ex = Cx + Side * EyeDx;
			auto Eye = [Ex](float x, float y) { return Ellipse(x, y, Ex, EyeY, EyeRx, EyeRy); };
			P.Clip(Ex - EyeRx - 6.f, EyeY - EyeRy - 6.f, Ex + EyeRx + 6.f, EyeY + EyeRy + 6.f);
			P.Fill([&](float x, float y) { return Eye(x, y) - 3.5f; }, FaceColors::EyeLine);
			P.Fill(Eye, FLinearColor::White);
			P.Fill([&](float x, float y) { return FMath::Max(Circle(x, y, Ex + 4.f, EyeY + 6.f, 13.f), Eye(x, y)); }, FaceColors::Pupil);
			P.Fill([&](float x, float y) { return FMath::Min(Circle(x, y, Ex + 10.f, EyeY - 3.f, 5.f), Circle(x, y, Ex + 13.f, EyeY + 11.f, 2.4f)); }, FLinearColor::White);
		}
		P.NoClip();
	}

	inline void DrawStar(FPainter& P, float X, float Y, float R)
	{
		const TArray<FVector2f> Star = StarPoints(X, Y, R, 0.47f);
		P.Clip(X - R - 6.f, Y - R - 6.f, X + R + 6.f, Y + R + 6.f);
		P.Fill([&](float x, float y) { return Polygon(x, y, Star) - 2.5f; }, FaceColors::StarLine);
		P.Fill([&](float x, float y) { return Polygon(x, y, Star); }, FaceColors::StarFill);
		P.NoClip();
	}

	/** Boca abierta (semicírculo hacia abajo de radio R con el borde recto en Top) con la lengua al fondo. */
	inline void DrawOpenSmile(FPainter& P, float Top, float R)
	{
		auto Smile = [=](float x, float y) { return FMath::Max(Circle(x, y, Cx, Top, R), Top - y); };
		P.Clip(Cx - R - 6.f, Top - 6.f, Cx + R + 6.f, Top + R + 6.f);
		P.Fill([&](float x, float y) { return Smile(x, y) - 3.f; }, FaceColors::Outline);
		P.Fill(Smile, FaceColors::Mouth);
		P.Fill([&](float x, float y) { return FMath::Max(Ellipse(x, y, Cx, Top + R * 0.78f, R * 0.52f, R * 0.32f), Smile(x, y)); }, FaceColors::Tongue);
		P.NoClip();
	}

	/** Caparazón cerrado (la tortuga metida dentro): cúpula con escudos, reborde claro y brillo. */
	inline void DrawShell(FPainter& P)
	{
		auto Dome = [](float x, float y) { return FMath::Max(Ellipse(x, y, Cx, 142.f, 104.f, 92.f), y - 190.f); };
		auto Band = [](float x, float y) { return Box(x, y, Cx, 193.f, 100.f, 15.f, 13.f); };
		P.Sticker([&](float x, float y) { return FMath::Min(Dome(x, y), Band(x, y)); }, 7.f, 6.f, FVector2f(3.f, 6.f));
		P.Fill([&](float x, float y) { return FMath::Min(Dome(x, y), Band(x, y)) - 4.f; }, Hex(0x2F3F14));
		P.Layer(Dome, [](float, float y) { return Mix(Hex(0xA9D158), Hex(0x5D7A26), (y - 50.f) / 140.f); });
		// Escudos: hexágono central, radios hasta el borde y un anillo de placas.
		const FLinearColor Seam = Hex(0x3E5319);
		P.Fill([&](float x, float y) { return FMath::Max(FMath::Abs(Hexagon(x, y, Cx, 132.f, 32.f)) - 2.6f, Dome(x, y)); }, Seam);
		P.Fill([&](float x, float y) { return FMath::Max(FMath::Abs(Ellipse(x, y, Cx, 142.f, 72.f, 62.f)) - 2.4f, FMath::Max(Dome(x, y), -Hexagon(x, y, Cx, 132.f, 34.f))); }, Seam);
		for (int32 k = 0; k < 6; ++k)
		{
			const float A = -HALF_PI + k * PI / 3.f;
			const FVector2f From(Cx + 32.f * FMath::Cos(A), 132.f + 32.f * FMath::Sin(A));
			const FVector2f To(Cx + 120.f * FMath::Cos(A), 132.f + 120.f * FMath::Sin(A));
			P.Fill([&](float x, float y) { return FMath::Max(Segment(x, y, From.X, From.Y, To.X, To.Y, 2.4f), Dome(x, y)); }, Seam);
		}
		P.Fill([&](float x, float y) { return FMath::Max(Ellipse(x, y, 94.f, 92.f, 30.f, 14.f), Dome(x, y)); }, Hex(0xFFFFFF, 0.3f));
		// Reborde (la barriga asomando) con muescas.
		P.Layer(Band, [](float, float y) { return Mix(Hex(0xF0DA92), Hex(0xC9A95A), (y - 178.f) / 30.f); });
		for (const float Nx : { 64.f, 96.f, 128.f, 160.f, 192.f })
		{
			P.Fill([&](float x, float y) { return FMath::Max(Segment(x, y, Nx, 181.f, Nx, 205.f, 1.6f), Band(x, y)); }, Hex(0x8C7A3A));
		}
	}

	/** Dibuja la cara del estado pedido en un lienzo de Size x Size. */
	inline void DrawFace(FPainter& P, ETNTurtleFace Face)
	{
		using namespace FaceColors;
		if (Face == ETNTurtleFace::Shell)
		{
			DrawShell(P);
			return;
		}

		// Silueta de la pegatina: la cabeza y, mareada, las estrellas.
		const TArray<FVector2f> StarA = StarPoints(78.f, 40.f, 15.f, 0.47f);
		const TArray<FVector2f> StarB = StarPoints(128.f, 26.f, 17.f, 0.47f);
		const TArray<FVector2f> StarC = StarPoints(178.f, 40.f, 15.f, 0.47f);
		auto Silhouette = [&](float x, float y)
		{
			float D = Head(x, y);
			if (Face == ETNTurtleFace::Down) { D = FMath::Min(D, FMath::Min(FMath::Min(Polygon(x, y, StarA), Polygon(x, y, StarB)), Polygon(x, y, StarC))); }
			return D;
		};
		P.Sticker(Silhouette, 7.f, 6.f, FVector2f(3.f, 6.f));
		DrawHead(P);

		switch (Face)
		{
			case ETNTurtleFace::Happy:
			{
				DrawBlush(P, 0.45f);
				DrawEyes(P);
				DrawOpenSmile(P, 158.f, 29.f);
				break;
			}
			case ETNTurtleFace::Down:
			{
				DrawBlush(P, 0.25f);
				// Ojos en cruz.
				for (const float Side : { -1.f, 1.f })
				{
					const float Ex = Cx + Side * EyeDx;
					P.Clip(Ex - 24.f, EyeY - 24.f, Ex + 24.f, EyeY + 24.f);
					P.Fill([Ex](float x, float y)
					{
						return FMath::Min(Segment(x, y, Ex - 15.f, EyeY - 15.f, Ex + 15.f, EyeY + 15.f, 5.f), Segment(x, y, Ex + 15.f, EyeY - 15.f, Ex - 15.f, EyeY + 15.f, 5.f));
					}, EyeLine);
				}
				// Boca torcida con la lengua de lado.
				P.Clip(90.f, 160.f, 176.f, 214.f);
				P.Fill([](float x, float y) { return Ellipse(x, y, 146.f, 188.f, 10.f, 14.f) - 3.f; }, Outline);
				P.Fill([](float x, float y) { return Ellipse(x, y, 146.f, 188.f, 10.f, 14.f); }, Tongue);
				P.Fill([](float x, float y) { return Arc(x, y, Cx, 150.f, 26.f, 0.55f, 2.6f, 3.f); }, Outline);
				P.NoClip();
				DrawStar(P, 78.f, 40.f, 15.f);
				DrawStar(P, 128.f, 26.f, 17.f);
				DrawStar(P, 178.f, 40.f, 15.f);
				break;
			}
			case ETNTurtleFace::Win:
			{
				DrawBlush(P, 0.6f, 1.1f);
				// Ojos de estrella.
				for (const float Side : { -1.f, 1.f })
				{
					const float Ex = Cx + Side * EyeDx;
					P.Clip(Ex - EyeRx - 6.f, EyeY - EyeRy - 6.f, Ex + EyeRx + 6.f, EyeY + EyeRy + 6.f);
					P.Fill([Ex](float x, float y) { return Ellipse(x, y, Ex, EyeY, EyeRx, EyeRy) - 3.5f; }, EyeLine);
					P.Fill([Ex](float x, float y) { return Ellipse(x, y, Ex, EyeY, EyeRx, EyeRy); }, FLinearColor::White);
					P.NoClip();
					DrawStar(P, Ex + 2.f, EyeY + 3.f, 19.f);
				}
				DrawOpenSmile(P, 154.f, 37.f);
				// Dientecillos arriba.
				P.Clip(100.f, 150.f, 156.f, 166.f);
				P.Fill([](float x, float y) { return Box(x, y, Cx, 158.f, 18.f, 4.5f, 2.f); }, FLinearColor::White);
				P.NoClip();
				break;
			}
			default:
				break;
		}
	}

	/** Textura de la cara (en caché: se dibuja una vez por ejecución). */
	inline UTexture2D* TurtleFace(ETNTurtleFace Face)
	{
		static const TCHAR* Names[] = { TEXT("Face_Happy"), TEXT("Face_Shell"), TEXT("Face_Down"), TEXT("Face_Win") };
		static_assert(UE_ARRAY_COUNT(Names) == static_cast<int32>(ETNTurtleFace::Win) + 1, "Un nombre por cara");
		const int32 Index = FMath::Clamp(static_cast<int32>(Face), 0, static_cast<int32>(ETNTurtleFace::Win));
		return Cached(Names[Index], [Face, Index]
		{
			FPainter P(Size, Size);
			DrawFace(P, Face);
			return P.ToTexture(*FString::Printf(TEXT("TN_HUD_%s"), Names[Index]));
		});
	}
}
