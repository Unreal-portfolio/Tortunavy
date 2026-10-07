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
		case ETNCatalogLook::Totem:
			return PaintTotemIcon();
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
		default:
			// La bola y el tótem conservan su malla del proyecto.
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
	return Look == ETNCatalogLook::StaminaBoost || Look == ETNCatalogLook::StaminaFull;
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
