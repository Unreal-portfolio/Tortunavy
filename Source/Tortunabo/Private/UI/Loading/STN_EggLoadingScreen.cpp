#include "STN_EggLoadingScreen.h"

#include "../HUD/TN_HUDArt.h"
#include "../HUD/TN_HUDFonts.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/Clipping.h"
#include "Math/RandomStream.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"

// ─────────────────────────────────────────────────────────────────────────────
// Medidas y arte (hilo de juego, una vez)
// ─────────────────────────────────────────────────────────────────────────────

namespace TNEggLoadingDetail
{
	/** Lienzo de la superficie de la cáscara (2,4:1) y zoom sobre el ajuste «cubrir»: las mitades se salen de la pantalla. */
	constexpr int32 SurfaceW = 2048;
	constexpr int32 SurfaceH = 854;
	constexpr float SurfaceZoom = 1.2f;

	// Medidas en altos de pantalla.
	/** Las mitades siguen por fuera de la pantalla (temblor y giros al salir despedidas). */
	constexpr float SideMargin = 0.25f;
	constexpr float EdgeMargin = 0.12f;
	/** Grosor de la cáscara: el filo que se ve cuando las mitades se separan. */
	constexpr float RimThickness = 0.013f;
	/** Sombra suave a los dos lados de la unión (le da hondura a la línea) y cuánto oscurece junto a ella. */
	constexpr float GrooveWidth = 0.03f;
	constexpr float GrooveShade = 0.84f;
	/** Tinta de la unión y de las grietas. */
	constexpr float InkWidth = 0.0055f;
	constexpr float CrackWidth = 0.0042f;
	/** Rótulos y tortugas (sobre la pantalla con el huevo cerrado). */
	constexpr float TitleTop = 0.07f;
	constexpr float StatusTop = 0.595f;
	constexpr float TipTop = 0.672f;
	constexpr float FeetLine = 0.885f;
	constexpr float TurtleHeight = 0.105f;

	constexpr int32 NumMainCracks = 10;
	constexpr int32 NumShards = 24;
	constexpr int32 NumPuffs = 18;

	/** De lineal a sRGB con tabla (la superficie tiene casi dos millones de píxeles; FPainter::ToTexture va píxel a píxel). */
	UTexture2D* ToTextureFast(const TNHUDArt::FPainter& Canvas, const TCHAR* Name)
	{
		static uint8 SrgbTable[4097];
		static bool bTableReady = false;
		if (!bTableReady)
		{
			for (int32 i = 0; i <= 4096; ++i)
			{
				const float Linear = static_cast<float>(i) / 4096.f;
				const float Encoded = Linear <= 0.0031308f ? Linear * 12.92f : 1.055f * FMath::Pow(Linear, 1.f / 2.4f) - 0.055f;
				SrgbTable[i] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Encoded * 255.f), 0, 255));
			}
			bTableReady = true;
		}
		UTexture2D* Texture = UTexture2D::CreateTransient(Canvas.W, Canvas.H, PF_B8G8R8A8, FName(Name));
		if (!Texture)
		{
			return nullptr;
		}
		Texture->SRGB = true;
		Texture->Filter = TF_Bilinear;
		Texture->LODGroup = TEXTUREGROUP_UI;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		FColor* Data = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		const int32 PixelCount = Canvas.W * Canvas.H;
		for (int32 i = 0; i < PixelCount; ++i)
		{
			const FLinearColor& Source = Canvas.Px[i];
			Data[i] = FColor(
				SrgbTable[FMath::Clamp(FMath::RoundToInt(Source.R * 4096.f), 0, 4096)],
				SrgbTable[FMath::Clamp(FMath::RoundToInt(Source.G * 4096.f), 0, 4096)],
				SrgbTable[FMath::Clamp(FMath::RoundToInt(Source.B * 4096.f), 0, 4096)],
				static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Source.A * 255.f), 0, 255)));
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	UTexture2D* MakeShellSurface()
	{
		TNHUDArt::FPainter Canvas(SurfaceW, SurfaceH);

		// Fondo: crema con un moteado muy suave (la cáscara no es lisa del todo). Ondas separables precalculadas por
		// fila y columna: el lienzo entero se rellena en unos milisegundos.
		const FLinearColor ShellCream = TNHUDArt::Hex(0xFFF6E4);
		const FLinearColor ShellWarm = TNHUDArt::Hex(0xF3E1C0);
		TArray<float> WaveA, WaveC, WaveE;
		TArray<float> WaveB, WaveD, WaveF;
		WaveA.SetNumUninitialized(SurfaceW);
		WaveC.SetNumUninitialized(SurfaceW);
		WaveE.SetNumUninitialized(SurfaceW);
		for (int32 x = 0; x < SurfaceW; ++x)
		{
			const float U = static_cast<float>(x) / SurfaceW;
			WaveA[x] = FMath::Sin(U * 21.f + 1.3f);
			WaveC[x] = FMath::Sin(U * 57.f + 2.1f);
			WaveE[x] = FMath::Sin(U * 9.f);
		}
		WaveB.SetNumUninitialized(SurfaceH);
		WaveD.SetNumUninitialized(SurfaceH);
		WaveF.SetNumUninitialized(SurfaceH);
		for (int32 y = 0; y < SurfaceH; ++y)
		{
			const float V = static_cast<float>(y) / SurfaceH;
			WaveB[y] = FMath::Sin(V * 15.f + 0.4f);
			WaveD[y] = FMath::Sin(V * 41.f + 1.7f);
			WaveF[y] = FMath::Sin(V * 7.f + 0.9f);
		}
		for (int32 y = 0; y < SurfaceH; ++y)
		{
			for (int32 x = 0; x < SurfaceW; ++x)
			{
				const float Mottle = 0.5f + 0.22f * WaveA[x] * WaveB[y] + 0.14f * WaveC[x] * WaveD[y] + 0.14f * WaveE[x] * WaveF[y];
				Canvas.Px[y * SurfaceW + x] = TNHUDArt::Mix(ShellCream, ShellWarm, 0.55f * Mottle);
			}
		}

		// Motas de colores (los de las cuatro tortugas), de forma algo irregular y sin tapar el título ni el estado.
		struct FSpot
		{
			float X = 0.f;
			float Y = 0.f;
			float R = 0.f;
			int32 Hue = 0;
			float Wobble = 0.f;
			float Phase = 0.f;
		};
		TArray<FSpot> Spots;
		FRandomStream Stream(20260927);
		for (int32 Attempt = 0; Attempt < 6000 && Spots.Num() < 70; ++Attempt)
		{
			FSpot Candidate;
			Candidate.R = Stream.FRandRange(0.02f, 0.066f) * SurfaceH;
			Candidate.X = Stream.FRandRange(0.f, 1.f) * SurfaceW;
			Candidate.Y = Stream.FRandRange(0.f, 1.f) * SurfaceH;
			Candidate.Hue = Stream.RandRange(0, 3);
			Candidate.Wobble = Stream.FRandRange(0.05f, 0.13f);
			Candidate.Phase = Stream.FRandRange(0.f, 2.f * UE_PI);
			// Zonas del título y del estado con el consejo, en coordenadas del lienzo (el lienzo mide 2,4 de ancho por 1).
			const float U = Candidate.X / SurfaceW;
			const float V = Candidate.Y / SurfaceH;
			const float Reach = Candidate.R / SurfaceH;
			if (FMath::Abs(U - 0.5f) < 0.12f + Reach * 0.42f && V > 0.1f - Reach && V < 0.3f + Reach)
			{
				continue;
			}
			if (FMath::Abs(U - 0.5f) < 0.21f + Reach * 0.42f && V > 0.54f - Reach && V < 0.75f + Reach)
			{
				continue;
			}
			bool bFree = true;
			for (const FSpot& Other : Spots)
			{
				if (FMath::Square(Other.X - Candidate.X) + FMath::Square(Other.Y - Candidate.Y) < FMath::Square(Other.R + Candidate.R + 0.03f * SurfaceH))
				{
					bFree = false;
					break;
				}
			}
			if (bFree)
			{
				Spots.Add(Candidate);
			}
		}
		static const uint32 SpotHex[4] = { 0x86DCCB, 0xFFB6A3, 0xFFD36E, 0xCDB8F2 };
		for (const FSpot& Spot : Spots)
		{
			auto SpotSdf = [&Spot](float x, float y)
			{
				const float Dx = x - Spot.X;
				const float Dy = y - Spot.Y;
				const float Ang = FMath::Atan2(Dy, Dx);
				const float Radius = Spot.R * (1.f + Spot.Wobble * (0.6f * FMath::Sin(3.f * Ang + Spot.Phase) + 0.4f * FMath::Sin(5.f * Ang + 2.f * Spot.Phase)));
				return FMath::Sqrt(Dx * Dx + Dy * Dy) - Radius;
			};
			const float Bound = Spot.R * 1.3f + 3.f;
			Canvas.Clip(Spot.X - Bound, Spot.Y - Bound, Spot.X + Bound, Spot.Y + Bound);
			const FLinearColor SpotLight = TNHUDArt::Hex(SpotHex[FMath::Clamp(Spot.Hue, 0, 3)]);
			const FLinearColor SpotDeep(SpotLight.R * 0.8f, SpotLight.G * 0.8f, SpotLight.B * 0.8f, 1.f);
			const float SpotRadius = Spot.R;
			// Acuarela: el borde, un poco más oscuro que el centro.
			Canvas.Layer(SpotSdf, [&SpotSdf, &SpotLight, &SpotDeep, SpotRadius](float x, float y)
			{
				const float Inside = FMath::Clamp(-SpotSdf(x, y) / (0.35f * SpotRadius), 0.f, 1.f);
				FLinearColor Mixed = TNHUDArt::Mix(SpotDeep, SpotLight, Inside);
				Mixed.A = 0.9f;
				return Mixed;
			});
			Canvas.Fill([&Spot](float x, float y) { return TNHUDArt::Circle(x, y, Spot.X - Spot.R * 0.34f, Spot.Y - Spot.R * 0.36f, Spot.R * 0.2f); },
				FLinearColor(1.f, 1.f, 1.f, 0.35f));
		}

		// Pintitas de cáscara.
		for (int32 i = 0; i < 900; ++i)
		{
			const float Sx = Stream.FRandRange(0.f, 1.f) * SurfaceW;
			const float Sy = Stream.FRandRange(0.f, 1.f) * SurfaceH;
			const float Sr = Stream.FRandRange(0.9f, 2.3f);
			const float SpeckAlpha = Stream.FRandRange(0.16f, 0.38f);
			Canvas.Clip(Sx - Sr - 2.f, Sy - Sr - 2.f, Sx + Sr + 2.f, Sy + Sr + 2.f);
			Canvas.Fill([Sx, Sy, Sr](float x, float y) { return TNHUDArt::Circle(x, y, Sx, Sy, Sr); }, TNHUDArt::Hex(0xB39062, SpeckAlpha));
		}
		Canvas.NoClip();
		return ToTextureFast(Canvas, TEXT("TN_EggShell_Surface"));
	}

	UTexture2D* MakeWhite()
	{
		TNHUDArt::FPainter Canvas(8, 8);
		Canvas.Layer([](float, float) { return -1.f; }, [](float, float) { return FLinearColor::White; });
		return Canvas.ToTexture(TEXT("TN_EggShell_White"));
	}

	UTexture2D* MakeGlint()
	{
		TNHUDArt::FPainter Canvas(256, 128);
		// Media luna: una elipse menos otra desplazada; más intensa a la izquierda y se apaga hacia la punta.
		auto Crescent = [](float x, float y)
		{
			const float Whole = TNHUDArt::Ellipse(x, y, 128.f, 70.f, 116.f, 50.f);
			const float Bite = TNHUDArt::Ellipse(x, y, 142.f, 92.f, 124.f, 54.f);
			return FMath::Max(Whole, -Bite);
		};
		Canvas.Layer(Crescent, [](float x, float)
		{
			return FLinearColor(1.f, 1.f, 1.f, FMath::Clamp(1.05f - x / 230.f, 0.f, 1.f));
		});
		return Canvas.ToTexture(TEXT("TN_EggShell_Glint"));
	}

	UTexture2D* MakeDot()
	{
		TNHUDArt::FPainter Canvas(64, 64);
		Canvas.Layer([](float x, float y) { return TNHUDArt::Circle(x, y, 32.f, 32.f, 30.f); }, [](float x, float y)
		{
			const float Dist = FMath::Clamp(FMath::Sqrt(FMath::Square(x - 32.f) + FMath::Square(y - 32.f)) / 30.f, 0.f, 1.f);
			return FLinearColor(1.f, 1.f, 1.f, FMath::Square(1.f - Dist));
		});
		return Canvas.ToTexture(TEXT("TN_EggShell_Dot"));
	}

	UTexture2D* MakeShard(int32 Shape, const TCHAR* Name)
	{
		TArray<FVector2f> Points;
		if (Shape == 0)
		{
			Points = { FVector2f(10.f, 70.f), FVector2f(38.f, 12.f), FVector2f(80.f, 30.f), FVector2f(86.f, 62.f), FVector2f(50.f, 84.f) };
		}
		else if (Shape == 1)
		{
			Points = { FVector2f(14.f, 40.f), FVector2f(56.f, 10.f), FVector2f(84.f, 52.f), FVector2f(40.f, 86.f) };
		}
		else
		{
			Points = { FVector2f(8.f, 58.f), FVector2f(30.f, 22.f), FVector2f(64.f, 14.f), FVector2f(88.f, 44.f), FVector2f(70.f, 80.f), FVector2f(30.f, 84.f) };
		}
		TNHUDArt::FPainter Canvas(96, 96);
		auto ShardSdf = [&Points](float x, float y) { return TNHUDArt::Polygon(x, y, Points); };
		Canvas.Fill([&ShardSdf](float x, float y) { return ShardSdf(x, y) - 4.f; }, TNHUDArt::Ink);
		Canvas.Layer(ShardSdf, [](float x, float y) { return TNHUDArt::Mix(TNHUDArt::Hex(0xFFF8EA), TNHUDArt::Hex(0xEBD6B2), (x + y) / 180.f); });
		// El grosor de la cáscara: un filo claro en el lado de arriba.
		Canvas.Fill([&ShardSdf](float x, float y) { return FMath::Max(TNHUDArt::Rim(ShardSdf, x, y, 2.f, 2.f), y - 48.f); }, FLinearColor(1.f, 1.f, 1.f, 0.75f));
		// Un trozo de mota en dos de las tres formas.
		if (Shape != 0)
		{
			const FLinearColor SpotColor = TNHUDArt::Hex(Shape == 1 ? 0x86DCCB : 0xFFB6A3, 0.9f);
			Canvas.Fill([&ShardSdf](float x, float y) { return FMath::Max(TNHUDArt::Circle(x, y, 62.f, 60.f, 16.f), ShardSdf(x, y) + 3.f); }, SpotColor);
		}
		return Canvas.ToTexture(Name);
	}

	UTexture2D* MakeTurtle(int32 ColorIndex, int32 Frame, const TCHAR* Name)
	{
		static const uint32 ShellColors[TNEggLoadingArt::NumTurtles] = { 0x2EC4B6, 0xFF6A52, 0xFFCB3D, 0x9B5DE5 };
		const FLinearColor ShellColor = TNHUDArt::Hex(ShellColors[FMath::Clamp(ColorIndex, 0, TNEggLoadingArt::NumTurtles - 1)]);
		const FLinearColor ShellDark(ShellColor.R * 0.7f, ShellColor.G * 0.7f, ShellColor.B * 0.7f, 1.f);
		const FLinearColor Skin = TNHUDArt::Hex(0x9CD66C);
		const FLinearColor SkinDark = TNHUDArt::Hex(0x6DAF45);

		TNHUDArt::FPainter P(160, 110);
		const float Swing = Frame == 0 ? 9.f : -8.f;
		auto LegSdf = [](float x, float y, float Hip, float Reach) { return TNHUDArt::Segment(x, y, Hip, 68.f, Hip + Reach, 94.f, 8.f); };
		auto FarLegs = [&](float x, float y) { return FMath::Min(LegSdf(x, y, 96.f, -Swing), LegSdf(x, y, 52.f, Swing)); };
		auto NearLegs = [&](float x, float y) { return FMath::Min(LegSdf(x, y, 108.f, Swing), LegSdf(x, y, 40.f, -Swing)); };
		auto ShellSdf = [](float x, float y) { return FMath::Max(TNHUDArt::Ellipse(x, y, 72.f, 66.f, 50.f, 42.f), y - 70.f); };
		auto RimSdf = [](float x, float y) { return TNHUDArt::Box(x, y, 72.f, 71.f, 53.f, 6.f, 5.f); };
		auto HeadSdf = [](float x, float y)
		{
			return FMath::Min(TNHUDArt::Circle(x, y, 130.f, 56.f, 16.f), TNHUDArt::Segment(x, y, 106.f, 66.f, 122.f, 60.f, 9.f));
		};
		const TArray<FVector2f> TailPoints = { FVector2f(26.f, 66.f), FVector2f(8.f, 74.f), FVector2f(28.f, 76.f) };
		auto TailSdf = [&](float x, float y) { return TNHUDArt::Polygon(x, y, TailPoints); };
		auto AllSdf = [&](float x, float y)
		{
			return FMath::Min(FMath::Min(FMath::Min(FarLegs(x, y), NearLegs(x, y)), FMath::Min(ShellSdf(x, y), RimSdf(x, y))),
				FMath::Min(HeadSdf(x, y), TailSdf(x, y)));
		};

		// Sombra, contorno y piezas de atrás hacia delante.
		P.Fill([](float x, float y) { return TNHUDArt::Ellipse(x, y, 76.f, 101.f, 58.f, 6.f); }, TNHUDArt::Hex(0x0A1C38, 0.35f));
		P.Fill([&](float x, float y) { return AllSdf(x, y) - 3.5f; }, TNHUDArt::Ink);
		P.Fill(FarLegs, SkinDark);
		P.Fill(TailSdf, Skin);
		P.Fill(NearLegs, Skin);
		P.Fill(HeadSdf, Skin);
		P.Fill(ShellSdf, ShellColor);
		// Placas: anillo central y dos radios hacia el borde; borde de la concha y brillo.
		P.Fill([&](float x, float y) { return FMath::Max(FMath::Abs(TNHUDArt::Circle(x, y, 72.f, 60.f, 22.f)) - 2.2f, ShellSdf(x, y) + 3.f); }, ShellDark);
		P.Fill([&](float x, float y)
		{
			return FMath::Max(FMath::Min(TNHUDArt::Segment(x, y, 52.f, 70.f, 58.f, 52.f, 2.2f), TNHUDArt::Segment(x, y, 92.f, 70.f, 86.f, 52.f, 2.2f)),
				ShellSdf(x, y) + 3.f);
		}, ShellDark);
		P.Fill(RimSdf, ShellDark);
		P.Fill([&](float x, float y) { return FMath::Max(TNHUDArt::Ellipse(x, y, 56.f, 42.f, 13.f, 7.f), ShellSdf(x, y) + 4.f); }, FLinearColor(1.f, 1.f, 1.f, 0.45f));
		// Ojo con brillo, moflete y sonrisa.
		P.Fill([](float x, float y) { return TNHUDArt::Circle(x, y, 134.f, 52.f, 5.5f); }, FLinearColor::White);
		P.Fill([](float x, float y) { return TNHUDArt::Circle(x, y, 136.f, 52.5f, 3.2f); }, TNHUDArt::Ink);
		P.Fill([](float x, float y) { return TNHUDArt::Circle(x, y, 137.f, 51.f, 1.2f); }, FLinearColor::White);
		P.Fill([](float x, float y) { return TNHUDArt::Circle(x, y, 127.f, 62.f, 3.4f); }, TNHUDArt::Hex(0xFF9A80, 0.7f));
		P.Fill([](float x, float y) { return TNHUDArt::Arc(x, y, 134.f, 59.f, 5.f, 0.35f, 1.9f, 1.2f); }, TNHUDArt::Ink);
		return P.ToTexture(Name);
	}

	UTexture2D* MakeBurst()
	{
		TNHUDArt::FPainter P(256, 256);
		const TArray<FVector2f> Outer = TNHUDArt::StarPoints(128.f, 128.f, 120.f, 0.72f, 12);
		const TArray<FVector2f> Inner = TNHUDArt::StarPoints(128.f, 128.f, 90.f, 0.7f, 12);
		P.Fill([&](float x, float y) { return TNHUDArt::Polygon(x, y, Outer) - 5.f; }, TNHUDArt::Ink);
		P.Fill([&](float x, float y) { return TNHUDArt::Polygon(x, y, Outer); }, TNHUDArt::Gold);
		P.Fill([&](float x, float y) { return TNHUDArt::Polygon(x, y, Inner); }, TNHUDArt::CoralLight);
		return P.ToTexture(TEXT("TN_EggShell_Burst"));
	}

	void SetBrush(FSlateBrush& Brush, UTexture2D* Texture)
	{
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.Tiling = ESlateBrushTileType::NoTile;
		if (Texture)
		{
			Brush.SetResourceObject(Texture);
			Brush.ImageSize = FVector2D(static_cast<double>(Texture->GetSizeX()), static_cast<double>(Texture->GetSizeY()));
		}
	}

	// ── Mallas para MakeCustomVerts ──────────────────────────────────────────

	/** Triángulos en el espacio de una geometría (con su giro y desplazamiento), listos para MakeCustomVerts. */
	struct FMeshBuilder
	{
		TArray<FSlateVertex> Verts;
		TArray<SlateIndex> Indices;
		FSlateRenderTransform Transform;

		void Begin(const FGeometry& Geometry)
		{
			Verts.Reset();
			Indices.Reset();
			Transform = Geometry.GetAccumulatedRenderTransform();
		}

		int32 AddVertex(const FVector2f& Local, const FVector2f& UV, const FLinearColor& Color)
		{
			// Slate guarda el color de vértice en sRGB (como hace con el tinte de las cajas).
			FSlateVertex Vertex = FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform, Local, UV, Color.ToFColor(true));
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

		void Submit(FSlateWindowElementList& ElementList, int32 InLayer, const FSlateBrush& Brush)
		{
			if (Indices.Num() > 0)
			{
				FSlateDrawElement::MakeCustomVerts(ElementList, InLayer, Brush.GetRenderingResource(), Verts, Indices, nullptr, 0, 0);
			}
			Verts.Reset();
			Indices.Reset();
		}
	};

	/** Volumen de la cáscara: la luz viene de arriba a la izquierda; hacia los bordes se oscurece y se calienta. */
	FLinearColor ShellShade(float X, float Y, float Sw, float Sh)
	{
		const float Nx = (X / Sw - 0.4f) / 0.8f;
		const float Ny = (Y / Sh - 0.34f) / 0.92f;
		const float Dist = FMath::Sqrt(Nx * Nx + Ny * Ny);
		return TNHUDArt::Mix(FLinearColor::White, TNHUDArt::Hex(0xE8D3B2), FMath::SmoothStep(0.25f, 1.25f, Dist));
	}

	/**
	 * Cuerpo de una mitad: rejilla que va del borde de fuera (fuera de la pantalla) hasta la línea de unión siguiendo sus
	 * dientes, con el volumen por vértice, la sombra junto a la unión y la superficie ajustada «cubriendo».
	 */
	void AddBody(FMeshBuilder& Mesh, const TArray<FVector2f>& Seam, bool bTop, float Sw, float Sh, const FVector2f& SurfaceOrigin,
		const FVector2f& SurfaceSize, float Alpha)
	{
		constexpr int32 FarRows = 5;
		constexpr int32 Rows = FarRows + 2;
		const int32 Cols = Seam.Num();
		const float Toward = bTop ? -1.f : 1.f;
		const float OuterY = bTop ? -EdgeMargin * Sh : Sh * (1.f + EdgeMargin);
		const int32 First = Mesh.Verts.Num();
		for (int32 Col = 0; Col < Cols; ++Col)
		{
			const float X = Seam[Col].X;
			const float GrooveY = Seam[Col].Y + Toward * GrooveWidth * Sh;
			for (int32 Row = 0; Row < Rows; ++Row)
			{
				const bool bSeamRow = Row == Rows - 1;
				const float Y = bSeamRow ? Seam[Col].Y : FMath::Lerp(OuterY, GrooveY, static_cast<float>(Row) / FarRows);
				const FLinearColor Shade = ShellShade(X, Y, Sw, Sh);
				const float Groove = bSeamRow ? GrooveShade : 1.f;
				const FVector2f UV((X - SurfaceOrigin.X) / SurfaceSize.X, (Y - SurfaceOrigin.Y) / SurfaceSize.Y);
				Mesh.AddVertex(FVector2f(X, Y), UV, FLinearColor(Shade.R * Groove, Shade.G * Groove, Shade.B * Groove, Alpha));
			}
		}
		for (int32 Col = 0; Col + 1 < Cols; ++Col)
		{
			for (int32 Row = 0; Row + 1 < Rows; ++Row)
			{
				const int32 Corner = First + Col * Rows + Row;
				Mesh.AddQuad(Corner, Corner + Rows, Corner + Rows + 1, Corner + 1);
			}
		}
	}

	/** Filo de la cáscara (su grosor): tira por fuera de la unión, hacia la otra mitad, clara junto al cuerpo. */
	void AddRim(FMeshBuilder& Mesh, const TArray<FVector2f>& Seam, bool bTop, float Thickness, float Alpha)
	{
		const FVector2f CenterUV(0.5f, 0.5f);
		const FVector2f Down(0.f, bTop ? Thickness : -Thickness);
		const FLinearColor InnerColor = TNHUDArt::Hex(0xFFFDF6, Alpha);
		const FLinearColor OuterColor = TNHUDArt::Hex(0xE9D5B1, Alpha);
		const int32 First = Mesh.Verts.Num();
		for (const FVector2f& SeamPoint : Seam)
		{
			Mesh.AddVertex(SeamPoint, CenterUV, InnerColor);
			Mesh.AddVertex(SeamPoint + Down, CenterUV, OuterColor);
		}
		for (int32 Col = 0; Col + 1 < Seam.Num(); ++Col)
		{
			const int32 Corner = First + Col * 2;
			Mesh.AddQuad(Corner, Corner + 2, Corner + 3, Corner + 1);
		}
	}

	/** Luz de dentro del huevo a lo largo de la unión (se ve solo por la rendija entre las mitades). */
	void AddGlow(FMeshBuilder& Mesh, const TArray<FVector2f>& Seam, float HalfHeight, float Alpha)
	{
		const FVector2f CenterUV(0.5f, 0.5f);
		const FVector2f Across(0.f, HalfHeight);
		const FLinearColor CoreColor = TNHUDArt::Hex(0xFFF8DA, Alpha);
		const FLinearColor WarmColor = TNHUDArt::Hex(0xFFC766, Alpha);
		const int32 First = Mesh.Verts.Num();
		for (const FVector2f& SeamPoint : Seam)
		{
			Mesh.AddVertex(SeamPoint - Across, CenterUV, WarmColor);
			Mesh.AddVertex(SeamPoint, CenterUV, CoreColor);
			Mesh.AddVertex(SeamPoint + Across, CenterUV, WarmColor);
		}
		for (int32 Col = 0; Col + 1 < Seam.Num(); ++Col)
		{
			const int32 Corner = First + Col * 3;
			Mesh.AddQuad(Corner, Corner + 3, Corner + 4, Corner + 1);
			Mesh.AddQuad(Corner + 1, Corner + 4, Corner + 5, Corner + 2);
		}
	}

	/**
	 * Trazo de tinta a lo largo de una polilínea: esquinas a inglete (con tope en las muy agudas, como las puntas del
	 * zigzag) y un borde de un píxel que se desvanece, así sale suave aunque los vértices no tengan antialias.
	 */
	void AddStroke(FMeshBuilder& Mesh, const TArray<FVector2f>& Points, float HalfWidth, float Feather, const FLinearColor& Color)
	{
		const int32 Count = Points.Num();
		if (Count < 2 || HalfWidth <= 0.f)
		{
			return;
		}
		const FVector2f CenterUV(0.5f, 0.5f);
		const FLinearColor Clear(Color.R, Color.G, Color.B, 0.f);
		int32 Previous = -1;
		for (int32 i = 0; i < Count; ++i)
		{
			const FVector2f DirIn = (i > 0 ? Points[i] - Points[i - 1] : Points[1] - Points[0]).GetSafeNormal();
			const FVector2f DirOut = (i + 1 < Count ? Points[i + 1] - Points[i] : Points[i] - Points[i - 1]).GetSafeNormal();
			const FVector2f NormalIn(-DirIn.Y, DirIn.X);
			const FVector2f NormalOut(-DirOut.Y, DirOut.X);
			FVector2f Miter = NormalIn + NormalOut;
			const float MiterLength = Miter.Size();
			Miter = MiterLength > 1e-3f ? Miter / MiterLength : (NormalIn.IsNearlyZero() ? NormalOut : NormalIn);
			const float Stretch = 1.f / FMath::Max(0.35f, FMath::Abs(FVector2f::DotProduct(Miter, NormalIn.IsNearlyZero() ? NormalOut : NormalIn)));
			const FVector2f Core = Miter * (HalfWidth * Stretch);
			const FVector2f Soft = Miter * ((HalfWidth + Feather) * Stretch);
			const int32 Base = Mesh.AddVertex(Points[i] - Soft, CenterUV, Clear);
			Mesh.AddVertex(Points[i] - Core, CenterUV, Color);
			Mesh.AddVertex(Points[i] + Core, CenterUV, Color);
			Mesh.AddVertex(Points[i] + Soft, CenterUV, Clear);
			if (Previous >= 0)
			{
				for (int32 Strip = 0; Strip < 3; ++Strip)
				{
					Mesh.AddQuad(Previous + Strip, Base + Strip, Base + Strip + 1, Previous + Strip + 1);
				}
			}
			Previous = Base;
		}
	}

	/** Tramo ya abierto de una grieta (Grow de 0 a 1 de su largo). */
	void CrackPrefix(const TArray<FVector2f>& Points, const TArray<float>& Along, float Grow, TArray<FVector2f>& OutPoints)
	{
		OutPoints.Reset();
		if (Points.Num() < 2 || Along.Num() != Points.Num() || Grow <= 0.f)
		{
			return;
		}
		const float Reach = Along.Last() * FMath::Min(Grow, 1.f);
		OutPoints.Add(Points[0]);
		for (int32 i = 1; i < Points.Num(); ++i)
		{
			if (Along[i] <= Reach)
			{
				OutPoints.Add(Points[i]);
				continue;
			}
			const float Span = Along[i] - Along[i - 1];
			if (Reach - Along[i - 1] > 0.05f && Span > 1e-3f)
			{
				OutPoints.Add(FMath::Lerp(Points[i - 1], Points[i], (Reach - Along[i - 1]) / Span));
			}
			break;
		}
	}
}

namespace TNEggLoadingArt
{
	UTexture2D* ShellSurface() { return TNHUDArt::Cached(TEXT("EggShell_Surface"), [] { return TNEggLoadingDetail::MakeShellSurface(); }); }
	UTexture2D* White() { return TNHUDArt::Cached(TEXT("EggShell_White"), [] { return TNEggLoadingDetail::MakeWhite(); }); }
	UTexture2D* Glint() { return TNHUDArt::Cached(TEXT("EggShell_Glint"), [] { return TNEggLoadingDetail::MakeGlint(); }); }
	UTexture2D* Dot() { return TNHUDArt::Cached(TEXT("EggShell_Dot"), [] { return TNEggLoadingDetail::MakeDot(); }); }
	UTexture2D* Burst() { return TNHUDArt::Cached(TEXT("EggShell_Burst"), [] { return TNEggLoadingDetail::MakeBurst(); }); }

	UTexture2D* Turtle(int32 ColorIndex, int32 Frame)
	{
		const FString Key = FString::Printf(TEXT("EggShell_Turtle_%d_%d"), ColorIndex, Frame);
		return TNHUDArt::Cached(FName(*Key), [ColorIndex, Frame, &Key] { return TNEggLoadingDetail::MakeTurtle(ColorIndex, Frame, *Key); });
	}

	UTexture2D* Shard(int32 Shape)
	{
		const FString Key = FString::Printf(TEXT("EggShell_Shard_%d"), Shape);
		return TNHUDArt::Cached(FName(*Key), [Shape, &Key] { return TNEggLoadingDetail::MakeShard(Shape, *Key); });
	}

	void Warm()
	{
		ShellSurface();
		White();
		Glint();
		Dot();
		Burst();
		for (int32 i = 0; i < NumShardShapes; ++i)
		{
			Shard(i);
		}
		for (int32 i = 0; i < NumTurtles; ++i)
		{
			Turtle(i, 0);
			Turtle(i, 1);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// «¡ADELANTE!»
// ─────────────────────────────────────────────────────────────────────────────

namespace TNEggLoadingDetail
{
	// Medidas del rótulo en fracciones de la pantalla o del alto de línea de la palabra. Las de dentro de la línea son de
	// Roboto (la fuente por defecto de Slate): las mayúsculas van del 19 % al 79 % del alto de línea.
	/** La palabra ocupa este ancho de la pantalla, con un alto de línea máximo para las pantallas muy anchas. */
	constexpr float GoWordWidth = 0.86f;
	constexpr float GoWordMaxHeight = 0.36f;
	/** La frase: su tamaño a 1080 de alto, encogida si no cabe en este ancho. */
	constexpr float GoLineWidth = 0.9f;
	/** Centro vertical del conjunto (de lo alto de las mayúsculas de la palabra a la línea base de la frase). */
	constexpr float GoCenterY = 0.47f;
	constexpr float GoCapTop = 0.19f;
	constexpr float GoBaseline = 0.79f;
	/** La frase empieza a este alto de línea de la palabra: por debajo del palo del «¡». */
	constexpr float GoLineTop = 0.96f;
	/** Copias del relieve naranja, de la cara hasta el fondo. */
	constexpr int32 GoExtrudeSteps = 5;
	/** En el huevo, el rótulo sigue a la vista hasta GoLingerSeconds después de que se vayan las mitades. */
	constexpr float GoEggHoldSeconds = FTNEggTimeline::BreakEnd + FTNEggTimeline::GoLingerSeconds - FTNEggTimeline::PopAt;

	/** Entrada con rebote: de 0 a 1 en Duration pasándose por arriba (Overshoot 1,7 ≈ 10 % de más; 2,6 ≈ 20 %). */
	float EaseOutBack(float T, float Duration, float Overshoot)
	{
		if (T <= 0.f)
		{
			return 0.f;
		}
		if (T >= Duration)
		{
			return 1.f;
		}
		const float P = T / Duration - 1.f;
		return 1.f + (Overshoot + 1.f) * P * P * P + Overshoot * P * P;
	}

	/** Frase de ánimo al azar (como mucho 70 caracteres), nunca la misma dos veces seguidas. Hilo de juego. */
	FText PickGoLine()
	{
		// Estático local, no de archivo: los NSLOCTEXT se crean con el sistema de localización ya en marcha.
		static const FText GoLines[] = {
			NSLOCTEXT("TNLoading", "GoLineStorm", "¡Corre hacia el mar, que la tormenta no te pille!"),
			NSLOCTEXT("TNLoading", "GoLineFins", "¡Aletas a tope: el mar te está esperando!"),
			NSLOCTEXT("TNLoading", "GoLineNest", "¡Sal del nido como un rayo y no mires atrás!"),
			NSLOCTEXT("TNLoading", "GoLineRollJumpSwim", "¡Rueda, salta y nada: la playa es toda tuya!"),
			NSLOCTEXT("TNLoading", "GoLineFasterThanTide", "¡Más rápida que la marea, más valiente que la tormenta!"),
			NSLOCTEXT("TNLoading", "GoLineShellAway", "¡Que la tormenta solo vea tu caparazón alejarse!"),
			NSLOCTEXT("TNLoading", "GoLineWaveWaits", "¡A la carrera, tortuga, que la ola no espera!"),
			NSLOCTEXT("TNLoading", "GoLineAlgae", "¡La última en llegar al agua invita a algas!"),
		};
		static int32 LastPicked = -1;
		const int32 NumLines = UE_ARRAY_COUNT(GoLines);
		int32 Picked = FMath::RandHelper(NumLines);
		if (Picked == LastPicked && NumLines > 1)
		{
			Picked = (Picked + 1 + FMath::RandHelper(NumLines - 1)) % NumLines;
		}
		LastPicked = Picked;
		return GoLines[Picked];
	}
}

void FTNGoBannerPainter::Init()
{
	WordText = NSLOCTEXT("TNLoading", "GoWord", "¡ADELANTE!").ToString();
	// A 1080 de alto la palabra sale casi a este tamaño; la escala de maquetación la ajusta al ancho de la pantalla. Todas
	// las capas usan este contorno y solo cambian su color, que no cuenta para la caché de letras: se rasterizan una vez.
	WordFont = TNHUDFonts::Make("Bold", 220);
	WordFont.OutlineSettings.OutlineSize = 12;
	WordFont.OutlineSettings.OutlineColor = TNHUDArt::Ink;
	LineFont = TNHUDFonts::Make("Bold", 50);
	LineFont.OutlineSettings.OutlineSize = 4;
	LineFont.OutlineSettings.OutlineColor = TNHUDArt::Ink;
	PickLine();
}

void FTNGoBannerPainter::PickLine()
{
	LineText = TNEggLoadingDetail::PickGoLine().ToString();
}

bool FTNGoBannerPainter::ComputePlacement(const FGeometry& BannerGeo, FPlacement& OutPlace) const
{
	namespace EggDetail = TNEggLoadingDetail;
	const FVector2f LocalSize = BannerGeo.GetLocalSize();
	const float Sw = LocalSize.X;
	const float Sh = LocalSize.Y;
	if (Sw < 16.f || Sh < 16.f || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
	{
		return false;
	}
	const TSharedRef<FSlateFontMeasure> Measurer = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	// Cajas a escala 1 con el contorno a los dos lados (Slate desplaza el relleno el grosor del contorno a la derecha).
	OutPlace.WordBox = Measurer->Measure(WordText, WordFont, 1.f) + FVector2f(2.f * static_cast<float>(WordFont.OutlineSettings.OutlineSize), 0.f);
	OutPlace.LineBox = Measurer->Measure(LineText, LineFont, 1.f) + FVector2f(2.f * static_cast<float>(LineFont.OutlineSettings.OutlineSize), 0.f);
	if (OutPlace.WordBox.X < 1.f || OutPlace.WordBox.Y < 1.f || OutPlace.LineBox.Y < 1.f)
	{
		return false;
	}
	// Escalas de maquetación: solo dependen del tamaño de la pantalla, así que las letras se rasterizan una sola vez.
	OutPlace.WordScale = FMath::Min(EggDetail::GoWordWidth * Sw / OutPlace.WordBox.X, EggDetail::GoWordMaxHeight * Sh / OutPlace.WordBox.Y);
	OutPlace.LineScale = Sh / 1080.f;
	if (OutPlace.LineBox.X * OutPlace.LineScale > EggDetail::GoLineWidth * Sw)
	{
		OutPlace.LineScale = EggDetail::GoLineWidth * Sw / OutPlace.LineBox.X;
	}
	const float WordH = OutPlace.WordBox.Y * OutPlace.WordScale;
	const float LineH = OutPlace.LineBox.Y * OutPlace.LineScale;
	const float BlockH = (EggDetail::GoLineTop - EggDetail::GoCapTop) * WordH + EggDetail::GoBaseline * LineH;
	const float WordTop = EggDetail::GoCenterY * Sh - 0.5f * BlockH - EggDetail::GoCapTop * WordH;
	OutPlace.WordPos = FVector2f(0.5f * (Sw - OutPlace.WordBox.X * OutPlace.WordScale), WordTop);
	OutPlace.LinePos = FVector2f(0.5f * (Sw - OutPlace.LineBox.X * OutPlace.LineScale), WordTop + EggDetail::GoLineTop * WordH);
	return true;
}

int32 FTNGoBannerPainter::Paint(const FGeometry& BannerGeo, FSlateWindowElementList& OutDrawElements, int32 LayerId, float SinceShow,
	float HoldSeconds, float Alpha) const
{
	namespace EggDetail = TNEggLoadingDetail;
	FPlacement Place;
	if (SinceShow < 0.f || !ComputePlacement(BannerGeo, Place))
	{
		return LayerId;
	}
	const float FadeT = FMath::Clamp((SinceShow - HoldSeconds) / FadeSeconds, 0.f, 1.f);
	const float Opacity = (1.f - FadeT) * Alpha;
	if (Opacity <= 0.f)
	{
		return LayerId;
	}
	const float Sh = BannerGeo.GetLocalSize().Y;
	auto Tint = [Opacity](uint32 Rgb, float Strength = 1.f) { return TNHUDArt::Hex(Rgb, Strength * Opacity); };
	int32 Layer = LayerId;

	// La palabra: rebote de entrada (≈ 20 % de más), respira un poco y crece al desvanecerse. El rebote va en la
	// transformación de render alrededor del centro de su caja: el tamaño de las letras en el atlas no cambia.
	const float Breath = SinceShow > PopSeconds ? 1.f + 0.012f * FMath::Sin((SinceShow - PopSeconds) * 7.f) : 1.f;
	const float WordRender = EggDetail::EaseOutBack(SinceShow, PopSeconds, 2.6f) * Breath * (1.f + 0.1f * FadeT);
	if (WordRender > 0.02f)
	{
		// Desplazamientos en altos de línea de la palabra ya en pantalla (con el rebote): todo crece junto.
		const float WordH = Place.WordBox.Y * Place.WordScale * WordRender;
		auto WordGeometry = [&BannerGeo, &Place, WordH, WordRender](const FVector2f& OffsetInLines)
		{
			return BannerGeo.MakeChild(Place.WordBox, FSlateLayoutTransform(Place.WordScale, Place.WordPos + OffsetInLines * WordH),
				FSlateRenderTransform(WordRender), FVector2f(0.5f, 0.5f));
		};
		// Tres colores del mismo contorno (misma entrada en la caché de letras): tinta, sombra y transparente (solo el
		// relleno). El contorno no sigue al tinte del texto: se desvanece a mano.
		FSlateFontInfo InkFont = WordFont;
		InkFont.OutlineSettings.OutlineColor = FLinearColor(TNHUDArt::Ink.R, TNHUDArt::Ink.G, TNHUDArt::Ink.B, Opacity);
		FSlateFontInfo SoftShadowFont = WordFont;
		SoftShadowFont.OutlineSettings.OutlineColor = Tint(0x0A1C38, 0.3f);
		FSlateFontInfo FillFont = WordFont;
		FillFont.OutlineSettings.OutlineColor = FLinearColor::Transparent;

		// 1. Sombra suave, abajo a la derecha.
		FSlateDrawElement::MakeText(OutDrawElements, Layer, WordGeometry(FVector2f(0.02f, 0.1f)).ToPaintGeometry(), WordText, SoftShadowFont,
			ESlateDrawEffect::None, Tint(0x0A1C38, 0.3f));
		Layer += 2;
		// 2. Relieve naranja: copias escalonadas hacia abajo. Todos los contornos van en una capa y todos los rellenos en la
		//    de encima, así el contorno grueso de tinta rodea el conjunto (la cara con su relieve).
		for (int32 Step = 0; Step < EggDetail::GoExtrudeSteps; ++Step)
		{
			const float Depth = static_cast<float>(Step) / static_cast<float>(EggDetail::GoExtrudeSteps - 1);
			FSlateDrawElement::MakeText(OutDrawElements, Layer, WordGeometry(FVector2f(0.012f, 0.062f) * Depth).ToPaintGeometry(), WordText, InkFont,
				ESlateDrawEffect::None, Tint(0xF26A1B));
		}
		Layer += 2;
		// 3. Línea de sombra justo debajo de la cara (la separa del relieve) y la cara, naranja dorado.
		FSlateDrawElement::MakeText(OutDrawElements, Layer, WordGeometry(FVector2f(0.003f, 0.016f)).ToPaintGeometry(), WordText, FillFont,
			ESlateDrawEffect::None, Tint(0xB2480E));
		Layer += 2;
		const FGeometry FaceGeo = WordGeometry(FVector2f::ZeroVector);
		const FPaintGeometry FacePaint = FaceGeo.ToPaintGeometry();
		FSlateDrawElement::MakeText(OutDrawElements, Layer, FacePaint, WordText, FillFont, ESlateDrawEffect::None, Tint(0xFFA41B));
		Layer += 2;
		// 4. Degradado: franjas cada vez más claras hacia arriba, recortadas desde lo alto de la caja hasta su fin (de 0 en
		//    lo alto de las mayúsculas a 1 en la línea base). El recorte va en la geometría de la cara, rebote incluido.
		static constexpr float BandEnd[] = { 0.72f, 0.48f, 0.24f };
		static constexpr uint32 BandRgb[] = { 0xFFC02A, 0xFFD84A, 0xFFF1A0 };
		const int32 NumBands = UE_ARRAY_COUNT(BandEnd);
		for (int32 Band = 0; Band < NumBands; ++Band)
		{
			const float BandBottom = (EggDetail::GoCapTop + BandEnd[Band] * (EggDetail::GoBaseline - EggDetail::GoCapTop)) * Place.WordBox.Y;
			const FGeometry BandGeo = FaceGeo.MakeChild(FVector2f(1.2f * Place.WordBox.X, BandBottom + Place.WordBox.Y),
				FSlateLayoutTransform(FVector2f(-0.1f * Place.WordBox.X, -Place.WordBox.Y)));
			OutDrawElements.PushClip(FSlateClippingZone(BandGeo));
			FSlateDrawElement::MakeText(OutDrawElements, Layer, FacePaint, WordText, FillFont, ESlateDrawEffect::None, Tint(BandRgb[Band]));
			OutDrawElements.PopClip();
			Layer += 2;
		}
	}

	// La frase: sale un poco después, desde más pequeña y un poco más abajo, en crema con contorno de tinta y sombra.
	const float LineT = SinceShow - 0.12f;
	if (LineT > 0.f)
	{
		const float LineIn = EggDetail::EaseOutBack(LineT, 0.28f, 1.7f);
		const float LineRender = FMath::Lerp(0.6f, 1.f, LineIn) * (1.f + 0.06f * FadeT);
		const float LineOpacity = Opacity * FMath::Clamp(LineT / 0.1f, 0.f, 1.f);
		const float LineH = Place.LineBox.Y * Place.LineScale * LineRender;
		const FVector2f Rise(0.f, (1.f - FMath::Clamp(LineIn, 0.f, 1.f)) * 0.03f * Sh);
		auto LinePaint = [&BannerGeo, &Place, &Rise, LineRender](const FVector2f& LineShift)
		{
			return BannerGeo.ToPaintGeometry(Place.LineBox, FSlateLayoutTransform(Place.LineScale, Place.LinePos + Rise + LineShift),
				FSlateRenderTransform(LineRender), FVector2f(0.5f, 0.5f));
		};
		FSlateFontInfo LineShadowFont = LineFont;
		LineShadowFont.OutlineSettings.OutlineColor = TNHUDArt::Hex(0x0A1C38, 0.45f * LineOpacity);
		FSlateFontInfo LineInkFont = LineFont;
		LineInkFont.OutlineSettings.OutlineColor = FLinearColor(TNHUDArt::Ink.R, TNHUDArt::Ink.G, TNHUDArt::Ink.B, LineOpacity);
		FSlateDrawElement::MakeText(OutDrawElements, Layer, LinePaint(FVector2f(0.f, 0.07f * LineH)), LineText, LineShadowFont,
			ESlateDrawEffect::None, TNHUDArt::Hex(0x0A1C38, 0.45f * LineOpacity));
		Layer += 2;
		FSlateDrawElement::MakeText(OutDrawElements, Layer, LinePaint(FVector2f::ZeroVector), LineText, LineInkFont, ESlateDrawEffect::None,
			FLinearColor(TNHUDArt::Cream.R, TNHUDArt::Cream.G, TNHUDArt::Cream.B, LineOpacity));
		Layer += 2;
	}
	return Layer;
}

void FTNGoBannerPainter::Warm(const FGeometry& BannerGeo, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	FPlacement Place;
	if (!ComputePlacement(BannerGeo, Place))
	{
		return;
	}
	// Con la misma escala de maquetación que al pintarlo (la clave de la caché de letras) y alfa 1/255: MakeText descarta
	// lo que tiene alfa 0 y así no se ve nada.
	constexpr float Faint = 1.f / 255.f;
	FSlateFontInfo FaintWordFont = WordFont;
	FaintWordFont.OutlineSettings.OutlineColor.A = Faint;
	FSlateFontInfo FaintLineFont = LineFont;
	FaintLineFont.OutlineSettings.OutlineColor.A = Faint;
	const FLinearColor FaintTint(1.f, 1.f, 1.f, Faint);
	FSlateDrawElement::MakeText(OutDrawElements, LayerId, BannerGeo.ToPaintGeometry(Place.WordBox, FSlateLayoutTransform(Place.WordScale, Place.WordPos)),
		WordText, FaintWordFont, ESlateDrawEffect::None, FaintTint);
	FSlateDrawElement::MakeText(OutDrawElements, LayerId, BannerGeo.ToPaintGeometry(Place.LineBox, FSlateLayoutTransform(Place.LineScale, Place.LinePos)),
		LineText, FaintLineFont, ESlateDrawEffect::None, FaintTint);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pantalla
// ─────────────────────────────────────────────────────────────────────────────

void STN_EggLoadingScreen::Construct(const FArguments& InArgs)
{
	TNEggLoadingArt::Warm();
	const double Now = FPlatformTime::Seconds();
	Seed = InArgs._Seed != 0u ? InArgs._Seed : ((static_cast<uint32>(FPlatformTime::Cycles()) * 2654435761u) | 1u);
	Timeline.Origin = InArgs._TimeOrigin >= 0.0 ? InArgs._TimeOrigin : Now;
	Timeline.bClosing = true;
	Timeline.MoveFrom = InArgs._StartClosed ? 1.f : 0.f;
	Timeline.MoveStart = InArgs._StartClosed ? Now - 10.0 : Now;
	Status = InArgs._Status.IsEmpty() ? NSLOCTEXT("TNLoading", "Default", "Incubando la partida") : InArgs._Status;

	// Momentos de las grietas principales, repartidos antes del «¡pum!» y barajados: no se abren de izquierda a derecha.
	FRandomStream TimeStream(static_cast<int32>(Seed * 7u + 3u));
	CrackStartTimes.Reset();
	for (int32 i = 0; i < TNEggLoadingDetail::NumMainCracks; ++i)
	{
		CrackStartTimes.Add(0.06f + 0.7f * static_cast<float>(i) / TNEggLoadingDetail::NumMainCracks + TimeStream.FRandRange(0.f, 0.04f));
	}
	for (int32 i = CrackStartTimes.Num() - 1; i > 0; --i)
	{
		CrackStartTimes.Swap(i, TimeStream.RandRange(0, i));
	}

	TNEggLoadingDetail::SetBrush(SurfaceBrush, TNEggLoadingArt::ShellSurface());
	TNEggLoadingDetail::SetBrush(WhiteBrush, TNEggLoadingArt::White());
	TNEggLoadingDetail::SetBrush(GlintBrush, TNEggLoadingArt::Glint());
	TNEggLoadingDetail::SetBrush(DotBrush, TNEggLoadingArt::Dot());
	TNEggLoadingDetail::SetBrush(BurstBrush, TNEggLoadingArt::Burst());
	for (int32 i = 0; i < TNEggLoadingArt::NumShardShapes; ++i)
	{
		TNEggLoadingDetail::SetBrush(ShardBrushes[i], TNEggLoadingArt::Shard(i));
	}
	for (int32 i = 0; i < TNEggLoadingArt::NumTurtles; ++i)
	{
		TNEggLoadingDetail::SetBrush(TurtleBrushes[i][0], TNEggLoadingArt::Turtle(i, 0));
		TNEggLoadingDetail::SetBrush(TurtleBrushes[i][1], TNEggLoadingArt::Turtle(i, 1));
	}

	// Rótulos a 1080 de alto (se escalan con la pantalla al pintarlos).
	TitleFont = TNHUDFonts::Make("Bold", 84);
	TitleFont.OutlineSettings.OutlineSize = 6;
	TitleFont.OutlineSettings.OutlineColor = TNHUDArt::Ink;
	TitleShadowFont = TitleFont;
	TitleShadowFont.OutlineSettings.OutlineColor = FLinearColor(TNHUDArt::Ink.R, TNHUDArt::Ink.G, TNHUDArt::Ink.B, 0.3f);
	StatusFont = TNHUDFonts::Make("Bold", 36);
	StatusFont.OutlineSettings.OutlineSize = 3;
	StatusFont.OutlineSettings.OutlineColor = TNHUDArt::Cream;
	TipFont = TNHUDFonts::Make("Regular", 22);
	PumFont = TNHUDFonts::Make("Bold", 72);
	PumFont.OutlineSettings.OutlineSize = 5;
	PumFont.OutlineSettings.OutlineColor = TNHUDArt::Ink;
	GoBanner.Init();

	// Mantiene Slate despierto mientras está a la vista (también en el hilo de carga de MoviePlayer).
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([](double, float) { return EActiveTimerReturnType::Continue; }));
}

void STN_EggLoadingScreen::SetStatus(const FText& InStatus, bool bInAnimateDots)
{
	if (!InStatus.IsEmpty())
	{
		Status = InStatus;
	}
	bAnimateDots = bInAnimateDots;
}

void STN_EggLoadingScreen::Close()
{
	if (IsBreaking() || Timeline.bClosing)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	Timeline.MoveFrom = Timeline.Closedness(Now);
	Timeline.MoveStart = Now;
	Timeline.bClosing = true;
	++Timeline.MoveSerial;
}

void STN_EggLoadingScreen::SnapClosed()
{
	const double Now = FPlatformTime::Seconds();
	if (IsBreaking() || Timeline.IsSettledAt(Now))
	{
		return;
	}
	Timeline.bClosing = true;
	Timeline.MoveFrom = 1.f;
	Timeline.MoveStart = Now - 10.0;
	++Timeline.MoveSerial;
}

void STN_EggLoadingScreen::Open()
{
	if (IsBreaking() || !Timeline.bClosing)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	Timeline.MoveFrom = Timeline.Closedness(Now);
	Timeline.MoveStart = Now;
	Timeline.bClosing = false;
	++Timeline.MoveSerial;
}

void STN_EggLoadingScreen::StartBreak(bool bInGoFinale)
{
	if (!IsBreaking())
	{
		Timeline.bClosing = true;
		Timeline.BreakTime = FPlatformTime::Seconds();
		bGoFinale = bInGoFinale;
		if (bGoFinale)
		{
			GoBanner.PickLine();
		}
	}
}

float STN_EggLoadingScreen::SeamYAt(float X) const
{
	const TArray<FVector2f>& SeamPoints = Layout.Seam;
	if (SeamPoints.Num() == 0)
	{
		return Layout.Sh * 0.5f;
	}
	if (X <= SeamPoints[0].X)
	{
		return SeamPoints[0].Y;
	}
	for (int32 i = 1; i < SeamPoints.Num(); ++i)
	{
		if (X <= SeamPoints[i].X)
		{
			const float Span = FMath::Max(1e-3f, SeamPoints[i].X - SeamPoints[i - 1].X);
			return FMath::Lerp(SeamPoints[i - 1].Y, SeamPoints[i].Y, (X - SeamPoints[i - 1].X) / Span);
		}
	}
	return SeamPoints.Last().Y;
}

void STN_EggLoadingScreen::RefreshLayout(float Sw, float Sh) const
{
	if (Layout.Sw == Sw && Layout.Sh == Sh && Layout.Seam.Num() > 1)
	{
		return;
	}
	Layout = FEggLayout();
	Layout.Sw = Sw;
	Layout.Sh = Sh;
	FRandomStream Stream(static_cast<int32>(Seed));

	// Línea de unión: zigzag irregular (dientes de ancho y alto distintos) sobre una onda suave, de lado a lado y más
	// allá de la pantalla. Los dientes se miden en altos de pantalla: son iguales en 16:9, 21:9 o 4:3.
	const float SeamLeft = -TNEggLoadingDetail::SideMargin * Sh;
	const float SeamRight = Sw + TNEggLoadingDetail::SideMargin * Sh;
	const float WavePhase = Stream.FRandRange(0.f, 2.f * UE_PI);
	bool bPeak = Stream.FRand() < 0.5f;
	float SeamX = SeamLeft;
	for (int32 Guard = 0; Guard < 2000; ++Guard)
	{
		const float Wave = 0.012f * Sh * FMath::Sin(SeamX / Sw * 2.f * UE_PI * 1.3f + WavePhase);
		const float Tooth = Sh * Stream.FRandRange(0.015f, 0.036f);
		Layout.Seam.Add(FVector2f(SeamX, 0.5f * Sh + Wave + (bPeak ? -Tooth : Tooth)));
		bPeak = !bPeak;
		if (SeamX >= SeamRight)
		{
			break;
		}
		SeamX = FMath::Min(SeamRight, SeamX + Sh * Stream.FRandRange(0.026f, 0.05f));
	}
	Layout.SeamMinY = Sh;
	Layout.SeamMaxY = 0.f;
	const FVector2f RimOffset(0.f, TNEggLoadingDetail::RimThickness * Sh);
	for (const FVector2f& SeamPoint : Layout.Seam)
	{
		Layout.SeamMinY = FMath::Min(Layout.SeamMinY, SeamPoint.Y);
		Layout.SeamMaxY = FMath::Max(Layout.SeamMaxY, SeamPoint.Y);
		Layout.RimTop.Add(SeamPoint + RimOffset);
		Layout.RimBottom.Add(SeamPoint - RimOffset);
	}

	// Grietas: salen de la unión hacia dentro de una mitad o de la otra, quebrándose, y alguna echa una rama.
	const bool bFirstUp = Stream.FRand() < 0.5f;
	for (int32 CrackIndex = 0; CrackIndex < CrackStartTimes.Num(); ++CrackIndex)
	{
		const float StartX = (static_cast<float>(CrackIndex) + 0.5f + Stream.FRandRange(-0.32f, 0.32f)) / CrackStartTimes.Num() * Sw;
		const bool bTop = ((CrackIndex % 2) == 0) == bFirstUp;
		const float Toward = bTop ? -1.f : 1.f;
		FCrackPath Main;
		Main.bTop = bTop;
		Main.Start = CrackStartTimes[CrackIndex];
		Main.Duration = Stream.FRandRange(0.22f, 0.32f);
		Main.Points.Add(FVector2f(StartX, SeamYAt(StartX)));
		TArray<float> Headings;
		float Heading = Toward * UE_HALF_PI + Stream.FRandRange(-0.45f, 0.45f);
		const int32 Segments = Stream.RandRange(4, 6);
		for (int32 Segment = 0; Segment < Segments; ++Segment)
		{
			Heading += Stream.FRandRange(-0.6f, 0.6f);
			// Que no se tumbe: como mucho unos 65° desde la vertical.
			Heading = Toward * UE_HALF_PI + FMath::Clamp(Heading - Toward * UE_HALF_PI, -1.15f, 1.15f);
			const float Length = Sh * Stream.FRandRange(0.028f, 0.052f) * (Segment == 0 ? 0.7f : 1.f);
			FVector2f NextPoint = Main.Points.Last() + FVector2f(FMath::Cos(Heading), FMath::Sin(Heading)) * Length;
			const float SeamHere = SeamYAt(NextPoint.X);
			NextPoint.Y = bTop ? FMath::Min(NextPoint.Y, SeamHere - 0.012f * Sh) : FMath::Max(NextPoint.Y, SeamHere + 0.012f * Sh);
			Main.Points.Add(NextPoint);
			Headings.Add(Heading);
		}
		Main.Along.Add(0.f);
		for (int32 i = 1; i < Main.Points.Num(); ++i)
		{
			Main.Along.Add(Main.Along.Last() + FVector2f::Distance(Main.Points[i], Main.Points[i - 1]));
		}
		Layout.Cracks.Add(Main);

		if (Stream.FRand() < 0.75f && Main.Points.Num() > 3)
		{
			const int32 From = Stream.RandRange(1, Main.Points.Num() - 3);
			FCrackPath Branch;
			Branch.bTop = bTop;
			Branch.Start = Main.Start + Main.Duration * Main.Along[From] / FMath::Max(1e-3f, Main.Along.Last());
			Branch.Duration = Stream.FRandRange(0.14f, 0.22f);
			Branch.Points.Add(Main.Points[From]);
			float BranchHeading = Headings[From] + (Stream.FRand() < 0.5f ? -1.f : 1.f) * Stream.FRandRange(0.6f, 0.95f);
			const int32 BranchSegments = Stream.RandRange(2, 3);
			for (int32 Segment = 0; Segment < BranchSegments; ++Segment)
			{
				BranchHeading += Stream.FRandRange(-0.4f, 0.4f);
				const float Length = Sh * Stream.FRandRange(0.022f, 0.04f);
				FVector2f NextPoint = Branch.Points.Last() + FVector2f(FMath::Cos(BranchHeading), FMath::Sin(BranchHeading)) * Length;
				const float SeamHere = SeamYAt(NextPoint.X);
				NextPoint.Y = bTop ? FMath::Min(NextPoint.Y, SeamHere - 0.012f * Sh) : FMath::Max(NextPoint.Y, SeamHere + 0.012f * Sh);
				Branch.Points.Add(NextPoint);
			}
			Branch.Along.Add(0.f);
			for (int32 i = 1; i < Branch.Points.Num(); ++i)
			{
				Branch.Along.Add(Branch.Along.Last() + FVector2f::Distance(Branch.Points[i], Branch.Points[i - 1]));
			}
			Layout.Cracks.Add(Branch);
		}
	}

	// Trozos de cáscara del «¡pum!»: salen de la unión, unos hacia arriba y otros hacia abajo.
	for (int32 i = 0; i < TNEggLoadingDetail::NumShards; ++i)
	{
		FShardSpec Shard;
		const float ShardX = Stream.FRandRange(0.02f, 0.98f) * Sw;
		Shard.Origin = FVector2f(ShardX, SeamYAt(ShardX) + Sh * Stream.FRandRange(-0.015f, 0.015f));
		const float ShardHeading = ((i % 2) == 0 ? -0.5f : 0.5f) * UE_PI + Stream.FRandRange(-0.95f, 0.95f);
		Shard.Velocity = FVector2f(FMath::Cos(ShardHeading), FMath::Sin(ShardHeading)) * (Sh * Stream.FRandRange(0.55f, 1.3f));
		Shard.Spin = Stream.FRandRange(-8.f, 8.f);
		Shard.Angle = Stream.FRandRange(0.f, 2.f * UE_PI);
		Shard.Size = Sh * Stream.FRandRange(0.024f, 0.055f);
		Shard.Shape = i % TNEggLoadingArt::NumShardShapes;
		Layout.Shards.Add(Shard);
	}

	// Polvo del golpe al cerrarse, a lo largo de la unión.
	for (int32 i = 0; i < TNEggLoadingDetail::NumPuffs; ++i)
	{
		FPuffSpec Puff;
		const float PuffX = (static_cast<float>(i) + Stream.FRandRange(0.1f, 0.9f)) / TNEggLoadingDetail::NumPuffs * Sw;
		Puff.Origin = FVector2f(PuffX, SeamYAt(PuffX));
		Puff.Drift = Stream.FRandRange(-1.f, 1.f);
		Puff.Size = Sh * Stream.FRandRange(0.03f, 0.065f);
		Puff.Delay = Stream.FRandRange(0.f, 0.06f);
		Layout.Puffs.Add(Puff);
	}

	// Superficie: ajuste «cubrir» (sin deformar las motas) con un poco de zoom para los márgenes de fuera.
	const float SurfaceAspect = static_cast<float>(TNEggLoadingDetail::SurfaceW) / TNEggLoadingDetail::SurfaceH;
	float CanvasW = 0.f;
	float CanvasH = 0.f;
	if (Sw / Sh <= SurfaceAspect)
	{
		CanvasH = Sh * TNEggLoadingDetail::SurfaceZoom;
		CanvasW = CanvasH * SurfaceAspect;
	}
	else
	{
		CanvasW = Sw * TNEggLoadingDetail::SurfaceZoom;
		CanvasH = CanvasW / SurfaceAspect;
	}
	Layout.SurfaceSize = FVector2f(CanvasW, CanvasH);
	Layout.SurfaceOrigin = FVector2f(0.5f * (Sw - CanvasW), 0.5f * (Sh - CanvasH));
}

void STN_EggLoadingScreen::ComputePoses(double Now, float Sw, float Sh, FPoses& OutPoses) const
{
	OutPoses = FPoses();
	const float BreakT = Timeline.BreakElapsed(Now);
	const float Closed = BreakT >= 0.f ? 1.f : Timeline.Closedness(Now);
	// Abiertas del todo, cada mitad queda entera fuera de la pantalla (con su filo).
	const float Clearance = (TNEggLoadingDetail::RimThickness + 0.1f) * Sh;
	const float TopY = -(1.f - Closed) * (Layout.SeamMaxY + Clearance);
	const float BottomY = (1.f - Closed) * (Sh - Layout.SeamMinY + Clearance);

	FVector2f Shake = FVector2f::ZeroVector;
	float Wobble = 0.f;
	if (BreakT < 0.f && Timeline.bClosing)
	{
		const float SinceImpact = static_cast<float>(Now - Timeline.ImpactTime());
		// Golpe del cierre: una sacudida que se apaga (sin rendija: una vez juntas, las mitades no se vuelven a separar).
		if (Timeline.MoveFrom < 0.95f && SinceImpact >= 0.f && SinceImpact < FTNEggTimeline::SettleSeconds)
		{
			const float Decay = FMath::Exp(-SinceImpact / 0.06f);
			Shake.Y += 0.009f * Sh * FMath::Sin(SinceImpact * 75.f) * Decay;
			Shake.X += 0.0025f * Sh * FMath::Sin(SinceImpact * 53.f + 1.f) * Decay;
		}
		// De vez en cuando algo se mueve dentro: un temblorcito.
		if (SinceImpact > 1.5f)
		{
			const float Cycle = FMath::Fmod(SinceImpact - 1.5f, 4.3f);
			if (Cycle < 0.26f)
			{
				const float Fade = 1.f - Cycle / 0.26f;
				Shake.X += 0.0035f * Sh * FMath::Sin(Cycle * 68.f) * Fade;
				Wobble += 0.0022f * FMath::Sin(Cycle * 47.f) * Fade;
			}
		}
	}
	if (BreakT >= 0.f && BreakT < FTNEggTimeline::PopAt)
	{
		// Tiembla cada vez más y, al final, se abre una rendija por la que sale luz.
		const float Grow = BreakT / FTNEggTimeline::PopAt;
		const float Amp = Sh * (0.0015f + 0.011f * Grow * Grow);
		Shake.X += Amp * (FMath::Sin(BreakT * 83.f) + 0.5f * FMath::Sin(BreakT * 131.f + 0.7f));
		Shake.Y += Amp * 0.45f * FMath::Cos(BreakT * 67.f);
		Wobble += 0.006f * Grow * FMath::Sin(BreakT * 41.f);
		const float Split = FMath::Clamp((BreakT - (FTNEggTimeline::PopAt - 0.14f)) / 0.14f, 0.f, 1.f);
		OutPoses.Gap = 0.016f * Sh * Split * Split;
	}

	OutPoses.Shake.Offset = Shake;
	OutPoses.Shake.Angle = Wobble;
	OutPoses.Top.Offset = Shake + FVector2f(0.f, TopY - 0.5f * OutPoses.Gap);
	OutPoses.Top.Angle = Wobble;
	OutPoses.Bottom.Offset = Shake + FVector2f(0.f, BottomY + 0.5f * OutPoses.Gap);
	OutPoses.Bottom.Angle = Wobble;

	if (BreakT >= FTNEggTimeline::PopAt)
	{
		// «¡Pum!»: la mitad de arriba sale despedida hacia arriba y la de abajo cae, girando cada una hacia un lado.
		const float Flight = BreakT - FTNEggTimeline::PopAt;
		const float Turn = FMath::SmoothStep(0.f, 0.5f, Flight);
		OutPoses.Top.Pivot = FVector2f(0.5f, 0.28f);
		OutPoses.Top.Angle = -0.3f * Turn;
		OutPoses.Top.Offset = FVector2f(0.05f * Sw * Flight, -(0.9f * Flight + 2.6f * Flight * Flight) * Sh - 0.008f * Sh);
		OutPoses.Bottom.Pivot = FVector2f(0.5f, 0.72f);
		OutPoses.Bottom.Angle = 0.22f * Turn;
		OutPoses.Bottom.Offset = FVector2f(-0.035f * Sw * Flight, (0.7f * Flight + 3.1f * Flight * Flight) * Sh + 0.008f * Sh);
	}
}

FString STN_EggLoadingScreen::GetStatusString(double Now) const
{
	if (IsBreaking())
	{
		return NSLOCTEXT("TNLoading", "EggBreaking", "¡Allá vamos!").ToString();
	}
	FString Line = Status.ToString();
	if (bAnimateDots)
	{
		const int32 Dots = 1 + static_cast<int32>(FMath::Max(0.0, Now - Timeline.Origin) * 2.5) % 3;
		Line += FString::ChrN(Dots, TEXT('.'));
	}
	return Line;
}

FString STN_EggLoadingScreen::GetTipString(double Now) const
{
	static const FText Tips[] = {
		NSLOCTEXT("TNLoading", "TipShellRoll", "Métete en el caparazón: rodarás cuesta abajo y tus compañeros te podrán lanzar."),
		NSLOCTEXT("TNLoading", "TipBellyFlop", "El panzazo cruza huecos que andando no se cruzan."),
		NSLOCTEXT("TNLoading", "TipKnockout", "Si te noquean, espera a que se vayan los pajaritos."),
		NSLOCTEXT("TNLoading", "TipCarryThrow", "Lleva a un compañero en su caparazón y lánzalo hacia la meta."),
		NSLOCTEXT("TNLoading", "TipStorm", "La tormenta avanza por el camino: no te quedes atrás."),
		NSLOCTEXT("TNLoading", "TipSwim", "En el agua se nada; las corrientes también empujan."),
		NSLOCTEXT("TNLoading", "TipCastle", "¿Aburrido en el castillo? Prueba el patio de pruebas y los toboganes del muro."),
		NSLOCTEXT("TNLoading", "TipReadyUp", "Para estar listo, métete en un huevo o en la sala de la puerta doble: con todos dentro, empieza la partida."),
		NSLOCTEXT("TNLoading", "TipReadyExit", "Como os pongáis listos, así saldréis al mapa: por la puerta doble o rompiendo los huevos."),
	};
	const int32 NumTips = UE_ARRAY_COUNT(Tips);
	const int32 First = static_cast<int32>(FMath::Frac(Timeline.Origin * 0.37) * NumTips);
	const int32 Index = (First + static_cast<int32>(FMath::Max(0.0, Now - Timeline.Origin) / 4.5)) % NumTips;
	return FText::Format(NSLOCTEXT("TNLoading", "TipPrefix", "Consejo: {0}"), Tips[FMath::Clamp(Index, 0, NumTips - 1)]).ToString();
}

int32 STN_EggLoadingScreen::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	namespace EggDetail = TNEggLoadingDetail;
	const double Now = FPlatformTime::Seconds();
	const FVector2f LocalSize = AllottedGeometry.GetLocalSize();
	const float Sw = LocalSize.X;
	const float Sh = LocalSize.Y;
	if (Sw < 16.f || Sh < 16.f)
	{
		return LayerId;
	}
	RefreshLayout(Sw, Sh);
	const FEggLayout& Egg = Layout;
	const float Alpha = InWidgetStyle.GetColorAndOpacityTint().A;
	// Un píxel de borde suave para las líneas de tinta, sea cual sea la escala de la interfaz.
	const float Feather = 1.1f / FMath::Max(0.25f, AllottedGeometry.GetAccumulatedLayoutTransform().GetScale());
	const float Idle = static_cast<float>(FMath::Max(0.0, Now - Timeline.Origin));
	const float BreakT = Timeline.BreakElapsed(Now);
	const float PopT = BreakT - FTNEggTimeline::PopAt;

	// Con «¡ADELANTE!», en cuanto las mitades han salido de la pantalla solo queda el rótulo.
	if (bGoFinale && BreakT >= FTNEggTimeline::BreakEnd)
	{
		return GoBanner.Paint(AllottedGeometry, OutDrawElements, LayerId, PopT, EggDetail::GoEggHoldSeconds, Alpha);
	}
	// Mientras tiembla, sus letras (enormes) se rasterizan casi transparentes y debajo de la cáscara: el tirón de generarlas
	// no cae en el momento del rótulo.
	if (bGoFinale && BreakT >= 0.f && BreakT < FTNEggTimeline::PopAt)
	{
		GoBanner.Warm(AllottedGeometry, OutDrawElements, LayerId);
	}

	FPoses Poses;
	ComputePoses(Now, Sw, Sh, Poses);
	auto PoseGeometry = [&AllottedGeometry](const FHalfPose& Pose)
	{
		return AllottedGeometry.MakeChild(FSlateRenderTransform(FQuat2f(Pose.Angle), Pose.Offset), Pose.Pivot);
	};
	const FGeometry TopGeo = PoseGeometry(Poses.Top);
	const FGeometry BottomGeo = PoseGeometry(Poses.Bottom);
	const FGeometry ShakeGeo = PoseGeometry(Poses.Shake);

	const FLinearColor InkColor(TNHUDArt::Ink.R, TNHUDArt::Ink.G, TNHUDArt::Ink.B, Alpha);
	const float InkHalf = 0.5f * EggDetail::InkWidth * Sh;
	int32 Layer = LayerId;
	EggDetail::FMeshBuilder Mesh;

	// 1. El filo de cada mitad (el grosor de la cáscara) con su línea de tinta: tapado mientras está cerrado, se ve al
	//    entrar las mitades y al salir despedidas.
	for (int32 Half = 0; Half < 2; ++Half)
	{
		const bool bTopHalf = Half == 0;
		const FGeometry& HalfGeo = bTopHalf ? TopGeo : BottomGeo;
		Mesh.Begin(HalfGeo);
		EggDetail::AddRim(Mesh, Egg.Seam, bTopHalf, EggDetail::RimThickness * Sh, Alpha);
		Mesh.Submit(OutDrawElements, Layer, WhiteBrush);
		Mesh.Begin(HalfGeo);
		EggDetail::AddStroke(Mesh, bTopHalf ? Egg.RimTop : Egg.RimBottom, InkHalf * 0.75f, Feather, InkColor);
		Mesh.Submit(OutDrawElements, Layer + 1, WhiteBrush);
	}
	Layer += 2;

	// 2. Luz de dentro por la rendija que se abre justo antes del «¡pum!»: encima de los filos y debajo de las mitades,
	//    así solo se ve entre ellas.
	if (Poses.Gap > 0.3f)
	{
		Mesh.Begin(ShakeGeo);
		EggDetail::AddGlow(Mesh, Egg.Seam, 0.5f * Poses.Gap + 0.01f * Sh, Alpha);
		Mesh.Submit(OutDrawElements, Layer, WhiteBrush);
	}
	++Layer;

	// 3. Las dos mitades de cáscara.
	for (int32 Half = 0; Half < 2; ++Half)
	{
		const bool bTopHalf = Half == 0;
		Mesh.Begin(bTopHalf ? TopGeo : BottomGeo);
		EggDetail::AddBody(Mesh, Egg.Seam, bTopHalf, Sw, Sh, Egg.SurfaceOrigin, Egg.SurfaceSize, Alpha);
		Mesh.Submit(OutDrawElements, Layer, SurfaceBrush);
	}
	++Layer;

	// 4. Tinta de la unión (una línea por mitad: cerradas coinciden) y grietas que crecen desde ella.
	{
		const FLinearColor CrackLight(1.f, 0.99f, 0.95f, 0.8f * Alpha);
		const float CrackHalf = 0.5f * EggDetail::CrackWidth * Sh;
		const FVector2f LightOffset(0.0017f * Sh, 0.0017f * Sh);
		TArray<FVector2f> Opened;
		TArray<FVector2f> OpenedLight;
		for (int32 Half = 0; Half < 2; ++Half)
		{
			const bool bTopHalf = Half == 0;
			Mesh.Begin(bTopHalf ? TopGeo : BottomGeo);
			EggDetail::AddStroke(Mesh, Egg.Seam, InkHalf, Feather, InkColor);
			if (BreakT >= 0.f)
			{
				for (const FCrackPath& Crack : Egg.Cracks)
				{
					if (Crack.bTop != bTopHalf)
					{
						continue;
					}
					EggDetail::CrackPrefix(Crack.Points, Crack.Along, (BreakT - Crack.Start) / Crack.Duration, Opened);
					if (Opened.Num() < 2)
					{
						continue;
					}
					// Canto claro al lado de la grieta (la cáscara partida) y la grieta en tinta encima.
					OpenedLight.Reset();
					for (const FVector2f& CrackPoint : Opened)
					{
						OpenedLight.Add(CrackPoint + LightOffset);
					}
					EggDetail::AddStroke(Mesh, OpenedLight, CrackHalf * 0.7f, Feather, CrackLight);
					EggDetail::AddStroke(Mesh, Opened, CrackHalf, Feather, InkColor);
				}
			}
			Mesh.Submit(OutDrawElements, Layer, WhiteBrush);
		}
	}
	++Layer;

	// 5. Lo pintado encima de la cáscara: brillo y nombre del juego arriba; tortugas, estado y consejo abajo.
	{
		const float GlintW = 0.36f * Sh;
		const float GlintH = 0.16f * Sh;
		const FVector2f GlintPos(0.2f * Sw - 0.5f * GlintW, 0.19f * Sh - 0.5f * GlintH);
		FSlateDrawElement::MakeRotatedBox(OutDrawElements, Layer, TopGeo.ToPaintGeometry(FVector2f(GlintW, GlintH), FSlateLayoutTransform(GlintPos)),
			&GlintBrush, ESlateDrawEffect::None, -0.32f, TOptional<FVector2f>(), FSlateDrawElement::RelativeToElement, FLinearColor(1.f, 1.f, 1.f, 0.55f * Alpha));
		const float SparkSize = 0.03f * Sh;
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, TopGeo.ToPaintGeometry(FVector2f(SparkSize, SparkSize),
			FSlateLayoutTransform(FVector2f(0.2f * Sw + 0.19f * Sh, 0.13f * Sh))), &DotBrush, ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, 0.7f * Alpha));

		const float TurtleH = EggDetail::TurtleHeight * Sh;
		const float TurtleW = TurtleH * 160.f / 110.f;
		const float FeetY = EggDetail::FeetLine * Sh;
		for (int32 i = 0; i < TNEggLoadingArt::NumTurtles; ++i)
		{
			const float Phase = FMath::Frac(Idle * 0.045f + i * 0.25f);
			const float TurtleX = -TurtleW + Phase * (Sw + TurtleW);
			const int32 Frame = static_cast<int32>(Idle * 5.f + i * 0.5f) % 2;
			const float Bob = FMath::Abs(FMath::Sin(Idle * 5.f * UE_PI + i)) * TurtleH * 0.035f;
			FSlateDrawElement::MakeBox(OutDrawElements, Layer, BottomGeo.ToPaintGeometry(FVector2f(TurtleW, TurtleH),
				FSlateLayoutTransform(FVector2f(TurtleX, FeetY - TurtleH * (100.f / 110.f) - Bob))), &TurtleBrushes[i][Frame], ESlateDrawEffect::None,
				FLinearColor(1.f, 1.f, 1.f, Alpha));
		}
	}
	++Layer;

	const TSharedRef<FSlateFontMeasure> Measurer = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const float TextScale = Sh / 1080.f;
	// Centrado según WidestLine (el estado con sus tres puntos: no baila mientras los puntos se animan).
	auto DrawLabel = [&](const FGeometry& LabelGeo, const FString& Line, const FString& WidestLine, const FSlateFontInfo& LabelFont, float TopY,
		const FLinearColor& LabelColor, const FSlateFontInfo* ShadowFont, const FLinearColor& ShadowColor)
	{
		if (Line.IsEmpty())
		{
			return;
		}
		const FVector2f LabelSize = Measurer->Measure(WidestLine, LabelFont, 1.f);
		float LabelScale = TextScale;
		// Un consejo largo en 4:3 no se sale: se encoge hasta caber.
		if (LabelSize.X * LabelScale > 0.92f * Sw && LabelSize.X > 1.f)
		{
			LabelScale = 0.92f * Sw / LabelSize.X;
		}
		const FVector2f LabelPos(0.5f * Sw - 0.5f * LabelSize.X * LabelScale, TopY);
		if (ShadowFont)
		{
			FSlateDrawElement::MakeText(OutDrawElements, Layer, LabelGeo.ToPaintGeometry(LabelSize,
				FSlateLayoutTransform(LabelScale, LabelPos + FVector2f(0.f, 0.006f * Sh))), Line, *ShadowFont, ESlateDrawEffect::None, ShadowColor);
		}
		FSlateDrawElement::MakeText(OutDrawElements, Layer + 1, LabelGeo.ToPaintGeometry(LabelSize, FSlateLayoutTransform(LabelScale, LabelPos)), Line,
			LabelFont, ESlateDrawEffect::None, LabelColor);
	};
	const FLinearColor ShadowTint(TNHUDArt::Ink.R, TNHUDArt::Ink.G, TNHUDArt::Ink.B, 0.3f * Alpha);
	const FString TitleLine(TEXT("Tortunavy"));
	DrawLabel(TopGeo, TitleLine, TitleLine, TitleFont, EggDetail::TitleTop * Sh, FLinearColor(TNHUDArt::Gold.R, TNHUDArt::Gold.G, TNHUDArt::Gold.B, Alpha),
		&TitleShadowFont, ShadowTint);
	const FString StatusLine = GetStatusString(Now);
	const FString StatusWidest = (!IsBreaking() && bAnimateDots) ? Status.ToString() + TEXT("...") : StatusLine;
	DrawLabel(BottomGeo, StatusLine, StatusWidest, StatusFont, EggDetail::StatusTop * Sh,
		FLinearColor(TNHUDArt::Navy.R, TNHUDArt::Navy.G, TNHUDArt::Navy.B, Alpha), nullptr, ShadowTint);
	const FString TipLine = GetTipString(Now);
	DrawLabel(BottomGeo, TipLine, TipLine, TipFont, EggDetail::TipTop * Sh, FLinearColor(TNHUDArt::Ink.R, TNHUDArt::Ink.G, TNHUDArt::Ink.B, 0.78f * Alpha),
		nullptr, ShadowTint);
	Layer += 2;

	// 6. Polvo del golpe al cerrarse (solo si venía de abierto).
	if (!IsBreaking() && ClosesWithImpact())
	{
		const float SinceImpact = static_cast<float>(Now - Timeline.ImpactTime());
		if (SinceImpact >= 0.f && SinceImpact < 0.65f)
		{
			for (const FPuffSpec& Puff : Egg.Puffs)
			{
				const float PuffT = SinceImpact - Puff.Delay;
				if (PuffT < 0.f)
				{
					continue;
				}
				const float Life = FMath::Clamp(PuffT / 0.55f, 0.f, 1.f);
				const float PuffSize = Puff.Size * (0.5f + 0.9f * Life);
				const FVector2f PuffCenter = Puff.Origin + FVector2f(Puff.Drift * 0.03f * Sh * Life, -0.012f * Sh * Life);
				FSlateDrawElement::MakeBox(OutDrawElements, Layer, ShakeGeo.ToPaintGeometry(FVector2f(PuffSize, PuffSize),
					FSlateLayoutTransform(PuffCenter - FVector2f(0.5f * PuffSize, 0.5f * PuffSize))), &DotBrush, ESlateDrawEffect::None,
					FLinearColor(1.f, 0.97f, 0.9f, 0.75f * FMath::Pow(1.f - Life, 1.5f) * Alpha));
			}
		}
	}
	++Layer;

	// 7. «¡Pum!»: fogonazo, trozos de cáscara y la estrella con el rótulo; al empezar la ronda del mapa procedural,
	//    «¡ADELANTE!» con su frase en lugar de la estrella.
	if (PopT >= 0.f)
	{
		if (PopT < 0.3f)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(FVector2f(Sw, Sh), FSlateLayoutTransform()), &WhiteBrush,
				ESlateDrawEffect::None, FLinearColor(1.f, 0.98f, 0.9f, 0.6f * (1.f - PopT / 0.3f) * Alpha));
		}
		++Layer;
		const float Gravity = 2.3f * Sh;
		const float ShardAlpha = 1.f - FMath::Clamp((PopT - 0.45f) / 0.35f, 0.f, 1.f);
		if (ShardAlpha > 0.f)
		{
			for (const FShardSpec& Shard : Egg.Shards)
			{
				const FVector2f ShardPos = Shard.Origin + Shard.Velocity * PopT + FVector2f(0.f, 0.5f * Gravity * PopT * PopT);
				FSlateDrawElement::MakeRotatedBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(FVector2f(Shard.Size, Shard.Size),
					FSlateLayoutTransform(ShardPos - FVector2f(0.5f * Shard.Size, 0.5f * Shard.Size))), &ShardBrushes[FMath::Clamp(Shard.Shape, 0, TNEggLoadingArt::NumShardShapes - 1)],
					ESlateDrawEffect::None, Shard.Angle + Shard.Spin * PopT, TOptional<FVector2f>(), FSlateDrawElement::RelativeToElement,
					FLinearColor(1.f, 1.f, 1.f, ShardAlpha * Alpha));
			}
		}
		++Layer;
		if (bGoFinale)
		{
			Layer = GoBanner.Paint(AllottedGeometry, OutDrawElements, Layer, PopT, EggDetail::GoEggHoldSeconds, Alpha);
		}
		else
		{
			const float PopScale = PopT < 0.12f ? PopT / 0.12f * 1.15f : 1.15f - FMath::Min(0.15f, (PopT - 0.12f) * 0.8f);
			const float BurstAlpha = PopT < 0.4f ? 1.f : 1.f - FMath::Clamp((PopT - 0.4f) / 0.3f, 0.f, 1.f);
			if (BurstAlpha > 0.f)
			{
				const float BurstSize = 0.34f * Sh * PopScale;
				FSlateDrawElement::MakeBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(FVector2f(BurstSize, BurstSize),
					FSlateLayoutTransform(FVector2f(0.5f * Sw - 0.5f * BurstSize, 0.5f * Sh - 0.5f * BurstSize))), &BurstBrush, ESlateDrawEffect::None,
					FLinearColor(1.f, 1.f, 1.f, BurstAlpha * Alpha));
				const FString PumLine = NSLOCTEXT("TNLoading", "EggPum", "¡PUM!").ToString();
				const FVector2f PumSize = Measurer->Measure(PumLine, PumFont, 1.f);
				const float PumScale = FMath::Max(0.05f, PopScale * TextScale);
				const FVector2f PumPos(0.5f * Sw - 0.5f * PumSize.X * PumScale, 0.5f * Sh - 0.5f * PumSize.Y * PumScale);
				// El contorno no sigue al tinte del texto: se desvanece con él a mano.
				FSlateFontInfo PumFadeFont = PumFont;
				PumFadeFont.OutlineSettings.OutlineColor.A *= BurstAlpha * Alpha;
				FSlateDrawElement::MakeText(OutDrawElements, Layer + 1, AllottedGeometry.ToPaintGeometry(PumSize, FSlateLayoutTransform(PumScale, PumPos)), PumLine,
					PumFadeFont, ESlateDrawEffect::None, FLinearColor(TNHUDArt::Navy.R, TNHUDArt::Navy.G, TNHUDArt::Navy.B, BurstAlpha * Alpha));
			}
			Layer += 2;
		}
	}
	return Layer;
}

// ─────────────────────────────────────────────────────────────────────────────
// «¡ADELANTE!» sin huevo
// ─────────────────────────────────────────────────────────────────────────────

void STN_GoBanner::Construct(const FArguments& InArgs)
{
	Painter.Init();
	ShowTime = FPlatformTime::Seconds() + static_cast<double>(FMath::Max(0.f, InArgs._Delay));
	// Mantiene Slate despierto mientras está a la vista.
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([](double, float) { return EActiveTimerReturnType::Continue; }));
}

int32 STN_GoBanner::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const float SinceShow = static_cast<float>(FPlatformTime::Seconds() - ShowTime);
	if (SinceShow < 0.f)
	{
		// Mientras espera a salir, sus letras se rasterizan sin que se vean: el tirón no cae en la entrada.
		Painter.Warm(AllottedGeometry, OutDrawElements, LayerId);
		return LayerId + 2;
	}
	return Painter.Paint(AllottedGeometry, OutDrawElements, LayerId, SinceShow, FTNGoBannerPainter::OverlayHoldSeconds,
		InWidgetStyle.GetColorAndOpacityTint().A);
}
