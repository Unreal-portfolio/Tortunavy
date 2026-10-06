#include "TN_CatalogItemArt.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Misc/App.h"
#include "UObject/Package.h"
#include "../Lobby/Playground/TN_PlaygroundMeshKit.h"
#include "../UI/HUD/TN_HUDArt.h"

// ─────────────────────────────────────────────────────────────────────────────
// Arte de los objetos de siempre dibujado en código (#787): iconos estilo pegatina (pintor de distancias con signo del HUD,
// como los de la carrera y de Todos contra Todos) y mallas de juguete para los que traían una forma básica del motor.
// Los colores de los iconos de la piedra, la concha, el calamar y el peluche salen de los materiales de sus mallas.
// ─────────────────────────────────────────────────────────────────────────────

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto del
// bloque.
namespace TNCatalogItemArtDetail
{
	using FBuffers = TNPlaygroundKit::FBuffers;
	namespace Kit = TNPlaygroundKit;

	/** Lado en píxeles de los iconos cuadrados. */
	constexpr int32 CatalogIconSize = 128;

	bool IsHeadless()
	{
		return IsRunningDedicatedServer() || !FApp::CanEverRender();
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Ayudas de los iconos
	// ─────────────────────────────────────────────────────────────────────────

	/** Silueta con filo oscuro y degradado vertical: el relleno básico de las pegatinas. */
	template <typename FSdf>
	void Body(TNHUDArt::FPainter& Painter, const FSdf& Sdf, uint32 Edge, uint32 Top, uint32 Bottom, float TopY, float BottomY)
	{
		Painter.Fill([&Sdf](float px, float py) { return Sdf(px, py) - 2.f; }, TNHUDArt::Hex(Edge));
		const FLinearColor TopColor = TNHUDArt::Hex(Top);
		const FLinearColor BottomColor = TNHUDArt::Hex(Bottom);
		Painter.Layer(Sdf, [&TopColor, &BottomColor, TopY, BottomY](float, float py)
		{
			return TNHUDArt::Mix(TopColor, BottomColor, (py - TopY) / FMath::Max(1.f, BottomY - TopY));
		});
	}

	/** Caja de esquinas redondeadas centrada en (Cx, Cy), girada Angle radianes (positivo = en el sentido del reloj). */
	float RotatedBox(float px, float py, float Cx, float Cy, float Hx, float Hy, float Angle, float Radius)
	{
		const float C = FMath::Cos(Angle);
		const float S = FMath::Sin(Angle);
		const float Dx = px - Cx;
		const float Dy = py - Cy;
		return TNHUDArt::Box(Dx * C + Dy * S, -Dx * S + Dy * C, 0.f, 0.f, Hx, Hy, Radius);
	}

	/** Rayo de 7 vértices centrado en (Cx, Cy) con la escala Unit (mide 9,2 x 5,2 unidades). */
	TArray<FVector2f> BoltPolygon(float Cx, float Cy, float Unit)
	{
		return {
			FVector2f(Cx + 0.6f * Unit, Cy - 4.6f * Unit), FVector2f(Cx - 2.6f * Unit, Cy + 0.4f * Unit), FVector2f(Cx - 0.2f * Unit, Cy + 0.4f * Unit),
			FVector2f(Cx - 1.2f * Unit, Cy + 4.6f * Unit), FVector2f(Cx + 2.6f * Unit, Cy - 0.8f * Unit), FVector2f(Cx + 0.2f * Unit, Cy - 0.8f * Unit),
			FVector2f(Cx + 1.6f * Unit, Cy - 4.6f * Unit) };
	}

	/** Mota redonda de un icono: centro y radio en píxeles. */
	struct FIconDot
	{
		float X, Y, Radius;
	};

	/** Estrella de cuatro puntas con filo (destellos de lo que revive o crece). */
	void PaintSparkle(TNHUDArt::FPainter& Painter, float Cx, float Cy, float Radius, uint32 Edge, uint32 Fill)
	{
		const TArray<FVector2f> Star = TNHUDArt::StarPoints(Cx, Cy, Radius, 0.3f, 4);
		Painter.Fill([&Star](float px, float py) { return TNHUDArt::Polygon(px, py, Star) - 1.6f; }, TNHUDArt::Hex(Edge));
		Painter.Fill([&Star](float px, float py) { return TNHUDArt::Polygon(px, py, Star); }, TNHUDArt::Hex(Fill));
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Iconos
	// ─────────────────────────────────────────────────────────────────────────

	/** Paleta de una barrita: envoltorio, puntas engarzadas y rayo. */
	struct FBarPalette
	{
		uint32 Edge, WrapTop, WrapBottom, EndTop, EndBottom, BoltEdge, BoltTop, BoltBottom;
	};

	/** Barrita de energía en diagonal: envoltorio con las puntas engarzadas y un rayo en medio. */
	UTexture2D* PaintBarIcon(const FBarPalette& Pal, const TCHAR* TextureName)
	{
		using namespace TNHUDArt;
		FPainter Painter(CatalogIconSize, CatalogIconSize);
		constexpr float Angle = -0.52f;
		const float Ax = FMath::Cos(Angle);
		const float Ay = FMath::Sin(Angle);
		const float Cx = 64.f;
		const float Cy = 66.f;
		const auto Wrap = [=](float px, float py) { return RotatedBox(px, py, Cx, Cy, 36.f, 16.f, Angle, 5.f); };
		const auto EndA = [=](float px, float py) { return RotatedBox(px, py, Cx - Ax * 41.f, Cy - Ay * 41.f, 7.f, 18.f, Angle, 2.f); };
		const auto EndB = [=](float px, float py) { return RotatedBox(px, py, Cx + Ax * 41.f, Cy + Ay * 41.f, 7.f, 18.f, Angle, 2.f); };
		const auto All = [&](float px, float py) { return FMath::Min(Wrap(px, py), FMath::Min(EndA(px, py), EndB(px, py))); };
		Painter.Sticker(All, 6.f);
		Body(Painter, EndA, Pal.Edge, Pal.EndTop, Pal.EndBottom, 30.f, 110.f);
		Body(Painter, EndB, Pal.Edge, Pal.EndTop, Pal.EndBottom, 16.f, 80.f);
		// Pliegues del engarce: rayas oscuras de través en cada punta.
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			const float Ex = Cx + Ax * Side * 41.f;
			const float Ey = Cy + Ay * Side * 41.f;
			for (int32 Fold = -1; Fold <= 1; ++Fold)
			{
				const float Fx = Ex + Ax * Fold * 4.f;
				const float Fy = Ey + Ay * Fold * 4.f;
				Painter.Fill([=](float px, float py)
				{
					return FMath::Max(RotatedBox(px, py, Fx, Fy, 0.8f, 16.f, Angle, 0.4f), RotatedBox(px, py, Ex, Ey, 7.f, 18.f, Angle, 2.f) + 1.5f);
				}, Hex(Pal.Edge, 0.45f));
			}
		}
		Body(Painter, Wrap, Pal.Edge, Pal.WrapTop, Pal.WrapBottom, 36.f, 96.f);
		// Brillo del canto de arriba del envoltorio.
		const float Gx = Cx + Ay * 9.f;
		const float Gy = Cy - Ax * 9.f;
		Painter.Fill([=](float px, float py) { return RotatedBox(px, py, Gx, Gy, 28.f, 1.6f, Angle, 1.6f); }, Hex(0xFFFFFF, 0.45f));
		// El rayo, con filo oscuro.
		const TArray<FVector2f> Bolt = BoltPolygon(Cx, Cy, 3.1f);
		Painter.Fill([&Bolt](float px, float py) { return Polygon(px, py, Bolt) - 1.8f; }, Hex(Pal.BoltEdge));
		Painter.Layer([&Bolt](float px, float py) { return Polygon(px, py, Bolt); },
			[&Pal](float, float py) { return Mix(Hex(Pal.BoltTop), Hex(Pal.BoltBottom), (py - 52.f) / 28.f); });
		return Painter.ToTexture(TextureName);
	}

	/** Piedra gris redondeada (la malla Piedra1 de la fila de la bola), con motas, una grieta y brillo. */
	UTexture2D* PaintBallIcon()
	{
		using namespace TNHUDArt;
		FPainter Painter(CatalogIconSize, CatalogIconSize);
		const auto Stone = [](float px, float py)
		{
			const float A = FMath::Atan2(py - 68.f, px - 64.f);
			const float Wobble = 1.f + 0.05f * FMath::Sin(3.f * A + 0.6f) + 0.035f * FMath::Sin(5.f * A + 1.9f);
			return Ellipse(px, py, 64.f, 68.f, 46.f * Wobble, 36.f * Wobble);
		};
		Painter.Sticker(Stone, 6.f);
		Body(Painter, Stone, 0x23262B, 0xB9BDC4, 0x5A5F66, 32.f, 104.f);
		// Cara de arriba algo más clara, motas y una grieta.
		Painter.Fill([&Stone](float px, float py) { return FMath::Max(Ellipse(px, py, 62.f, 54.f, 32.f, 14.f), Stone(px, py) + 3.f); }, Hex(0xFFFFFF, 0.16f));
		const FIconDot Dots[] = { { 44.f, 74.f, 2.6f }, { 80.f, 82.f, 3.2f }, { 90.f, 62.f, 2.2f }, { 58.f, 90.f, 2.0f }, { 70.f, 70.f, 1.8f }, { 36.f, 60.f, 1.6f } };
		for (const FIconDot& Dot : Dots)
		{
			Painter.Fill([&Dot](float px, float py) { return Circle(px, py, Dot.X, Dot.Y, Dot.Radius); }, Hex(0x2E3238, 0.5f));
		}
		Painter.Fill([](float px, float py)
		{
			return FMath::Min(Segment(px, py, 72.f, 76.f, 80.f, 86.f, 1.1f), Segment(px, py, 80.f, 86.f, 77.f, 96.f, 1.1f));
		}, Hex(0x23262B, 0.6f));
		Painter.Fill([](float px, float py) { return Ellipse(px, py, 44.f, 48.f, 11.f, 5.f); }, Hex(0xFFFFFF, 0.6f));
		return Painter.ToTexture(TEXT("TN_Catalog_Ball"));
	}

	/** Cabeza de tortuga grandota con ojos saltones y las esquinas de «crece». */
	UTexture2D* PaintBigHeadIcon()
	{
		using namespace TNHUDArt;
		FPainter Painter(CatalogIconSize, CatalogIconSize);
		const auto Head = [](float px, float py) { return Circle(px, py, 64.f, 68.f, 38.f); };
		// Esquinas en L hacia fuera: «se hace más grande».
		const auto Corners = [](float px, float py)
		{
			float Dist = 1e9f;
			const float Xs[2] = { 14.f, 114.f };
			const float Ys[2] = { 16.f, 116.f };
			for (int32 i = 0; i < 2; ++i)
			{
				for (int32 j = 0; j < 2; ++j)
				{
					const float Dx = i == 0 ? 14.f : -14.f;
					const float Dy = j == 0 ? 14.f : -14.f;
					Dist = FMath::Min(Dist, Segment(px, py, Xs[i], Ys[j], Xs[i] + Dx, Ys[j], 3.4f));
					Dist = FMath::Min(Dist, Segment(px, py, Xs[i], Ys[j], Xs[i], Ys[j] + Dy, 3.4f));
				}
			}
			return Dist;
		};
		Painter.Sticker([&](float px, float py) { return FMath::Min(Head(px, py), Corners(px, py)); }, 5.f);
		Painter.Fill([&Corners](float px, float py) { return Corners(px, py) - 1.6f; }, Hex(0x5A3F0A));
		Painter.Fill(Corners, Hex(0xFFCB3D));
		Body(Painter, Head, 0x1F4A1A, 0xA6E07A, 0x4DB35A, 30.f, 106.f);
		// Manchas de la cabeza, morro más claro y mofletes.
		Painter.Fill([&Head](float px, float py) { return FMath::Max(Circle(px, py, 64.f, 38.f, 8.f), Head(px, py) + 2.f); }, Hex(0x3D8F47, 0.6f));
		Painter.Fill([&Head](float px, float py) { return FMath::Max(Circle(px, py, 42.f, 46.f, 5.f), Head(px, py) + 2.f); }, Hex(0x3D8F47, 0.6f));
		Painter.Fill([&Head](float px, float py) { return FMath::Max(Circle(px, py, 86.f, 46.f, 5.f), Head(px, py) + 2.f); }, Hex(0x3D8F47, 0.6f));
		Painter.Fill([](float px, float py) { return Ellipse(px, py, 64.f, 88.f, 24.f, 13.f); }, Hex(0xC8F0A0, 0.85f));
		Painter.Fill([](float px, float py) { return FMath::Min(Circle(px, py, 38.f, 82.f, 6.f), Circle(px, py, 90.f, 82.f, 6.f)); }, Hex(0xFF7A8A, 0.45f));
		// Ojos saltones.
		for (const float Ex : { 50.f, 78.f })
		{
			Painter.Fill([Ex](float px, float py) { return Circle(px, py, Ex, 62.f, 12.f); }, Hex(0x1F4A1A));
			Painter.Fill([Ex](float px, float py) { return Circle(px, py, Ex, 62.f, 10.f); }, FLinearColor::White);
			Painter.Fill([Ex](float px, float py) { return Circle(px, py, Ex + 2.f, 64.f, 5.f); }, Hex(0x13233B));
			Painter.Fill([Ex](float px, float py) { return Circle(px, py, Ex + 0.5f, 61.5f, 1.8f); }, FLinearColor::White);
		}
		// Sonrisa.
		Painter.Fill([](float px, float py) { return Arc(px, py, 64.f, 84.f, 10.f, FMath::DegreesToRadians(25.f), FMath::DegreesToRadians(155.f), 1.8f); },
			Hex(0x1F4A1A));
		Painter.Fill([](float px, float py) { return Ellipse(px, py, 48.f, 40.f, 9.f, 5.f); }, Hex(0xFFFFFF, 0.5f));
		return Painter.ToTexture(TEXT("TN_Catalog_BigHead"));
	}

	/** Concha cerrada malva (la malla ConchaCerrada de la fila), vista de frente, con el filo dentado de la trampa. */
	UTexture2D* PaintConchIcon()
	{
		using namespace TNHUDArt;
		FPainter Painter(CatalogIconSize, CatalogIconSize);
		const auto Upper = [](float px, float py) { return FMath::Max(Ellipse(px, py, 64.f, 72.f, 48.f, 46.f), py - 72.f); };
		const auto Lower = [](float px, float py) { return FMath::Max(Ellipse(px, py, 64.f, 72.f, 48.f, 24.f), 72.f - py); };
		const auto All = [&](float px, float py) { return FMath::Min(Upper(px, py), Lower(px, py)); };
		Painter.Sticker(All, 6.f);
		Body(Painter, Lower, 0x3A2A30, 0xA8989E, 0x5E5258, 72.f, 96.f);
		Body(Painter, Upper, 0x3A2A30, 0xE2C8D2, 0x8C7480, 26.f, 72.f);
		// Costillas desde la charnela de abajo, solo en la valva de arriba.
		for (int32 k = -4; k <= 4; ++k)
		{
			const float A = k * 0.3f;
			const float Ex = 64.f + FMath::Sin(A) * 52.f;
			const float Ey = 76.f - FMath::Cos(A) * 52.f;
			Painter.Fill([&Upper, Ex, Ey](float px, float py) { return FMath::Max(Segment(px, py, 64.f, 76.f, Ex, Ey, 1.7f), Upper(px, py) + 2.f); },
				Hex(0x6E5560, 0.5f));
		}
		// Filo dentado entre las dos valvas: la trampa que se cierra.
		Painter.Fill([](float px, float py)
		{
			float Dist = 1e9f;
			for (int32 Tooth = 0; Tooth < 10; ++Tooth)
			{
				const float X0 = 20.f + Tooth * 8.8f;
				const float Y0 = Tooth % 2 == 0 ? 68.f : 76.f;
				const float Y1 = Tooth % 2 == 0 ? 76.f : 68.f;
				Dist = FMath::Min(Dist, Segment(px, py, X0, Y0, X0 + 8.8f, Y1, 1.6f));
			}
			return Dist;
		}, Hex(0x3A2A30));
		Painter.Fill([](float px, float py) { return Ellipse(px, py, 46.f, 42.f, 11.f, 6.f); }, Hex(0xFFFFFF, 0.55f));
		return Painter.ToTexture(TEXT("TN_Catalog_Conch"));
	}

	/** Calamar morado (la malla Calamar de la fila de la tinta) con aletas, tentáculos y una gota de tinta. */
	UTexture2D* PaintInkIcon()
	{
		using namespace TNHUDArt;
		FPainter Painter(CatalogIconSize, CatalogIconSize);
		const TArray<FVector2f> Tip = { { 50.f, 30.f }, { 64.f, 6.f }, { 78.f, 30.f } };
		const TArray<FVector2f> FinL = { { 47.f, 18.f }, { 28.f, 36.f }, { 46.f, 44.f } };
		const TArray<FVector2f> FinR = { { 81.f, 18.f }, { 100.f, 36.f }, { 82.f, 44.f } };
		const auto Mantle = [&Tip](float px, float py) { return FMath::Min(Ellipse(px, py, 64.f, 46.f, 20.f, 27.f), Polygon(px, py, Tip)); };
		const auto Fins = [&FinL, &FinR](float px, float py) { return FMath::Min(Polygon(px, py, FinL), Polygon(px, py, FinR)); };
		const auto Head = [](float px, float py) { return Ellipse(px, py, 64.f, 74.f, 19.f, 12.f); };
		const auto Arms = [](float px, float py)
		{
			float Dist = 1e9f;
			const float Offsets[5] = { -16.f, -8.f, 0.f, 8.f, 16.f };
			for (const float Offset : Offsets)
			{
				const float X0 = 64.f + Offset * 0.8f;
				const float X1 = 64.f + Offset * 1.5f;
				Dist = FMath::Min(Dist, Segment(px, py, X0, 80.f, X1, 108.f, 3.6f));
				Dist = FMath::Min(Dist, Circle(px, py, X1, 109.f, 4.4f));
			}
			return Dist;
		};
		const auto Blob = [](float px, float py) { return FMath::Min(Circle(px, py, 104.f, 98.f, 10.f), Circle(px, py, 116.f, 84.f, 4.f)); };
		const auto Squid = [&](float px, float py)
		{
			return FMath::Min(FMath::Min(Mantle(px, py), Fins(px, py)), FMath::Min(Head(px, py), Arms(px, py)));
		};
		Painter.Sticker([&](float px, float py) { return FMath::Min(Squid(px, py), Blob(px, py)); }, 5.f);
		Body(Painter, Arms, 0x1E1440, 0x8A78D0, 0x4A3A8A, 80.f, 114.f);
		Body(Painter, Fins, 0x1E1440, 0xA898E8, 0x6A58B0, 18.f, 44.f);
		Body(Painter, Mantle, 0x1E1440, 0xA898E8, 0x57459C, 6.f, 74.f);
		Body(Painter, Head, 0x1E1440, 0x8A78D0, 0x57459C, 62.f, 86.f);
		// Motas del manto y brillo.
		const FIconDot Spots[] = { { 58.f, 40.f, 3.f }, { 70.f, 50.f, 2.6f }, { 62.f, 58.f, 2.2f }, { 72.f, 34.f, 2.f } };
		for (const FIconDot& Spot : Spots)
		{
			Painter.Fill([&Spot](float px, float py) { return Circle(px, py, Spot.X, Spot.Y, Spot.Radius); }, Hex(0xD8CCFF, 0.55f));
		}
		Painter.Fill([](float px, float py) { return Ellipse(px, py, 56.f, 30.f, 5.f, 9.f); }, Hex(0xFFFFFF, 0.45f));
		// Ojos.
		for (const float Ex : { 55.f, 73.f })
		{
			Painter.Fill([Ex](float px, float py) { return Circle(px, py, Ex, 73.f, 5.5f); }, FLinearColor::White);
			Painter.Fill([Ex](float px, float py) { return Circle(px, py, Ex + 1.f, 74.f, 3.f); }, Hex(0x13132A));
		}
		// La tinta.
		Painter.Fill(Blob, Hex(0x141420));
		Painter.Fill([](float px, float py) { return Ellipse(px, py, 101.f, 94.f, 3.f, 2.f); }, Hex(0x5A5A80, 0.8f));
		return Painter.ToTexture(TEXT("TN_Catalog_Ink"));
	}

	/** Tortuga de peluche verde (la malla Peluche1 del tótem) con costuras, ojos de botón y destellos de revivir. */
	UTexture2D* PaintTotemIcon()
	{
		using namespace TNHUDArt;
		FPainter Painter(CatalogIconSize, CatalogIconSize);
		const auto Shell = [](float px, float py) { return Ellipse(px, py, 64.f, 80.f, 36.f, 29.f); };
		const auto HeadSdf = [](float px, float py) { return Circle(px, py, 64.f, 40.f, 17.f); };
		const auto Legs = [](float px, float py)
		{
			float Dist = Ellipse(px, py, 32.f, 98.f, 11.f, 8.f);
			Dist = FMath::Min(Dist, Ellipse(px, py, 96.f, 98.f, 11.f, 8.f));
			Dist = FMath::Min(Dist, Ellipse(px, py, 34.f, 64.f, 10.f, 7.f));
			return FMath::Min(Dist, Ellipse(px, py, 94.f, 64.f, 10.f, 7.f));
		};
		const auto Turtle = [&](float px, float py) { return FMath::Min(FMath::Min(Shell(px, py), HeadSdf(px, py)), Legs(px, py)); };
		const TArray<FVector2f> StarA = StarPoints(106.f, 24.f, 13.f, 0.3f, 4);
		const TArray<FVector2f> StarB = StarPoints(22.f, 30.f, 9.f, 0.3f, 4);
		const auto Stars = [&StarA, &StarB](float px, float py) { return FMath::Min(Polygon(px, py, StarA), Polygon(px, py, StarB)); };
		Painter.Sticker([&](float px, float py) { return FMath::Min(Turtle(px, py), Stars(px, py)); }, 5.f);
		Body(Painter, Legs, 0x1E2E14, 0x9CCB7A, 0x5E8C44, 56.f, 106.f);
		Body(Painter, HeadSdf, 0x1E2E14, 0xA8D488, 0x5E8C44, 23.f, 57.f);
		Body(Painter, Shell, 0x1E2E14, 0x7FAE5E, 0x3F5C2E, 51.f, 109.f);
		// Placas del caparazón (hexágonos) y la costura del borde.
		Painter.Fill([](float px, float py) { return FMath::Abs(Hexagon(px, py, 64.f, 80.f, 12.f)) - 1.6f; }, Hex(0xC8E6A8, 0.75f));
		for (const float Hx : { 40.f, 88.f })
		{
			Painter.Fill([&Shell, Hx](float px, float py) { return FMath::Max(FMath::Abs(Hexagon(px, py, Hx, 80.f, 11.f)) - 1.4f, Shell(px, py) + 3.f); },
				Hex(0xC8E6A8, 0.6f));
		}
		for (int32 Stitch = 0; Stitch < 18; ++Stitch)
		{
			const float A = Stitch * (2.f * PI / 18.f);
			const float Sx = 64.f + FMath::Cos(A) * 31.f;
			const float Sy = 80.f + FMath::Sin(A) * 24.f;
			const float Tx = -FMath::Sin(A) * 2.4f;
			const float Ty = FMath::Cos(A) * 2.f;
			Painter.Fill([=](float px, float py) { return Segment(px, py, Sx - Tx, Sy - Ty, Sx + Tx, Sy + Ty, 0.9f); }, Hex(0xFFFBF0, 0.75f));
		}
		// Ojos de botón y sonrisa cosida.
		for (const float Ex : { 57.f, 71.f })
		{
			Painter.Fill([Ex](float px, float py) { return Circle(px, py, Ex, 38.f, 3.8f); }, Hex(0x13233B));
			Painter.Fill([Ex](float px, float py) { return Circle(px, py, Ex - 1.f, 37.f, 1.2f); }, FLinearColor::White);
		}
		Painter.Fill([](float px, float py) { return Arc(px, py, 64.f, 44.f, 6.f, FMath::DegreesToRadians(30.f), FMath::DegreesToRadians(150.f), 1.2f); },
			Hex(0x1E2E14));
		// Destellos dorados: revive.
		PaintSparkle(Painter, 106.f, 24.f, 13.f, 0xB8760A, 0xFFE27A);
		PaintSparkle(Painter, 22.f, 30.f, 9.f, 0xB8760A, 0xFFE27A);
		return Painter.ToTexture(TEXT("TN_Catalog_Totem"));
	}

	UTexture2D* PaintIcon(ETNCatalogLook Look)
	{
		switch (Look)
		{
		case ETNCatalogLook::StaminaBoost:
			return PaintBarIcon({ 0x5A2A08, 0xFFB347, 0xE8661A, 0xFFE27A, 0xF2B01E, 0x3B2A08, 0xFFFFFF, 0xFFE14A }, TEXT("TN_Catalog_StaminaBoost"));
		case ETNCatalogLook::StaminaFull:
			return PaintBarIcon({ 0x0C4A44, 0x6AE8D8, 0x1A9F92, 0xE0F8F5, 0xA8DCD6, 0x0C2A44, 0xFFFFFF, 0xC8F4FF }, TEXT("TN_Catalog_StaminaFull"));
		case ETNCatalogLook::Ball:
			return PaintBallIcon();
		case ETNCatalogLook::BigHead:
			return PaintBigHeadIcon();
		case ETNCatalogLook::Conch:
			return PaintConchIcon();
		case ETNCatalogLook::Ink:
			return PaintInkIcon();
		case ETNCatalogLook::Totem:
			return PaintTotemIcon();
		case ETNCatalogLook::Score:
			// La misma concha de puntos del contador del HUD (y de su malla, abajo).
			return TNHUDArt::ShellIcon();
		default:
			return nullptr;
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Mallas (cm, pivote en el centro, mirando a +X)
	// ─────────────────────────────────────────────────────────────────────────

	/** Barrita de 26 x 9 x 4,4 cm: envoltorio, puntas engarzadas y un rayo en relieve encima. */
	void BuildBar(FBuffers& B, uint32 Wrap, uint32 Ends, uint32 Bolt)
	{
		constexpr double HalfZ = 2.2;
		Kit::AddAxisBox(B, FVector::ZeroVector, FVector(11.0, 4.5, HalfZ), Kit::Rgb(Wrap, 0.15f));
		for (const double Side : { -1.0, 1.0 })
		{
			Kit::AddAxisBox(B, FVector(Side * 12.3, 0.0, 0.0), FVector(1.3, 4.9, 1.0), Kit::Rgb(Ends, 0.25f));
		}
		// Rayo: dos paralelogramos convexos que se solapan en el codo.
		const TArray<FVector2D> UpperPart = { { -6.5, 2.6 }, { -2.0, 2.6 }, { 0.8, -0.3 }, { -3.2, -0.3 } };
		const TArray<FVector2D> LowerPart = { { -0.8, 0.3 }, { 3.2, 0.3 }, { 6.5, -2.6 }, { 2.0, -2.6 } };
		const FVector Top(0.0, 0.0, HalfZ + 0.4);
		Kit::AddSlab(B, Top, FVector::ForwardVector, FVector::RightVector, FVector::UpVector, UpperPart, 0.8, Kit::Rgb(Bolt, 0.3f));
		Kit::AddSlab(B, Top, FVector::ForwardVector, FVector::RightVector, FVector::UpVector, LowerPart, 0.8, Kit::Rgb(Bolt, 0.3f));
	}

	/** Cabeza de tortuga de unos 30 x 32 x 28 cm: morro claro, ojos saltones y manchas encima. */
	void BuildBigHead(FBuffers& B)
	{
		const FLinearColor Skin = Kit::Rgb(0x7BC96F, 0.1f);
		Kit::AddEllipsoid(B, FVector::ZeroVector, FVector::ForwardVector, FVector::RightVector, FVector::UpVector, FVector(15.0, 16.0, 14.0), 20, 10, Skin);
		Kit::AddEllipsoid(B, FVector(9.0, 0.0, -4.0), FVector::ForwardVector, FVector::RightVector, FVector::UpVector, FVector(8.0, 11.0, 7.0), 16, 8,
			Kit::Rgb(0xA6E07A, 0.1f));
		for (const double Side : { -1.0, 1.0 })
		{
			Kit::AddBall(B, FVector(9.5, Side * 7.0, 5.0), 5.0, 12, Kit::Rgb(0xFFFFFF, 0.35f));
			Kit::AddBall(B, FVector(13.6, Side * 7.0, 5.5), 2.4, 10, Kit::Rgb(0x13233B, 0.5f));
			Kit::AddEllipsoid(B, FVector(-3.0, Side * 6.0, 12.0), FVector::ForwardVector, FVector::RightVector, FVector::UpVector, FVector(4.0, 4.0, 2.0), 10, 5,
				Kit::Rgb(0x3D8F47, 0.1f));
		}
		Kit::AddEllipsoid(B, FVector(2.0, 0.0, 13.0), FVector::ForwardVector, FVector::RightVector, FVector::UpVector, FVector(4.5, 4.5, 2.0), 10, 5,
			Kit::Rgb(0x3D8F47, 0.1f));
	}

	/** Concha de puntos melocotón de unos 22 cm de alto: nueve costillas en abanico desde la charnela y las orejetas. */
	void BuildScoreShell(FBuffers& B)
	{
		const FVector Hinge(0.0, 0.0, -9.0);
		for (int32 Rib = -4; Rib <= 4; ++Rib)
		{
			const double A = Rib * 0.3;
			const FVector Dir(0.0, FMath::Sin(A), FMath::Cos(A));
			const FVector Across = FVector::CrossProduct(FVector::ForwardVector, Dir).GetSafeNormal();
			const double Length = 11.0 - FMath::Abs(Rib) * 0.6;
			Kit::AddEllipsoid(B, Hinge + Dir * Length, Dir, Across, FVector::ForwardVector, FVector(Length, 2.6, 2.4 - FMath::Abs(Rib) * 0.15), 12, 6,
				Kit::Rgb(Rib % 2 == 0 ? 0xFFB89A : 0xFF8A6A, 0.2f));
		}
		Kit::AddAxisBox(B, Hinge + FVector(0.0, 0.0, 0.5), FVector(1.6, 5.0, 2.0), Kit::Rgb(0xFF7A5E, 0.2f));
	}

	bool BuildLook(ETNCatalogLook Look, FBuffers& B)
	{
		switch (Look)
		{
		case ETNCatalogLook::StaminaBoost:
			BuildBar(B, 0xFF8A1F, 0xFFD23F, 0xFFF07A);
			return true;
		case ETNCatalogLook::StaminaFull:
			BuildBar(B, 0x2EC4B6, 0xE0F8F5, 0xFFFFFF);
			return true;
		case ETNCatalogLook::BigHead:
			BuildBigHead(B);
			return true;
		case ETNCatalogLook::Score:
			BuildScoreShell(B);
			return true;
		default:
			// La bola, la concha, la tinta y el tótem conservan su malla del proyecto.
			return false;
		}
	}

	/** La malla de Look, construida una sola vez y fuera del recolector (las filas del inventario la referencian). */
	UStaticMesh* GetMesh(ETNCatalogLook Look)
	{
		static TMap<int32, UStaticMesh*> Cache;
		const int32 Key = static_cast<int32>(Look);
		if (UStaticMesh** Found = Cache.Find(Key))
		{
			return *Found;
		}
		UStaticMesh* Mesh = nullptr;
		FBuffers Buffers;
		if (BuildLook(Look, Buffers) && !Buffers.IsEmpty())
		{
			Mesh = Kit::BuildMesh(GetTransientPackage(), Buffers, Kit::VertexColorMaterial());
			if (Mesh)
			{
				Mesh->AddToRoot();
			}
		}
		Cache.Add(Key, Mesh);
		return Mesh;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// API
// ─────────────────────────────────────────────────────────────────────────────

bool TNCatalogItemArt::HasCodeMesh(ETNCatalogLook Look)
{
	return Look == ETNCatalogLook::StaminaBoost || Look == ETNCatalogLook::StaminaFull || Look == ETNCatalogLook::BigHead
		|| Look == ETNCatalogLook::Score;
}

bool TNCatalogItemArt::GetHeldLook(ETNCatalogLook Look, FHeldLook& OutLook, bool bEvenHeadless)
{
	OutLook = FHeldLook();
	if ((!bEvenHeadless && TNCatalogItemArtDetail::IsHeadless()) || !HasCodeMesh(Look))
	{
		return false;
	}
	OutLook.Mesh = TNCatalogItemArtDetail::GetMesh(Look);
	return OutLook.Mesh != nullptr;
}

UTexture2D* TNCatalogItemArt::GetIcon(ETNCatalogLook Look, bool bEvenHeadless)
{
	if ((!bEvenHeadless && TNCatalogItemArtDetail::IsHeadless()) || Look == ETNCatalogLook::None || Look >= ETNCatalogLook::Count)
	{
		return nullptr;
	}
	const FString KeyText = FString::Printf(TEXT("CatalogItem_%s"), TNCatalogItemVisuals::CodeName(Look));
	return TNHUDArt::Cached(FName(*KeyText), [Look]() -> UTexture2D* { return TNCatalogItemArtDetail::PaintIcon(Look); });
}
