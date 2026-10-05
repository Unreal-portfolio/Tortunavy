// Arte de los objetos del coop dibujado en código: mallas de juguete (kit del parque de pruebas del lobby, brillo en el alfa
// del color de vértice) e iconos estilo pegatina (pintor de distancias con signo del HUD). Ver TN_CoopItemArt.h.

#include "TN_CoopItemArt.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Misc/App.h"
#include "UObject/Package.h"
#include "../Lobby/Playground/TN_PlaygroundMeshKit.h"
#include "../UI/HUD/TN_HUDArt.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto del bloque.
namespace TNCoopItemArtDetail
{
	using FBuffers = TNPlaygroundKit::FBuffers;
	namespace Kit = TNPlaygroundKit;

	constexpr int32 IconSize = 128;

	bool IsHeadless()
	{
		return IsRunningDedicatedServer() || !FApp::CanEverRender();
	}

	/** Malla de los buffers, fuera del recolector (las filas del inventario y los actores la referencian). */
	UStaticMesh* Finish(const FBuffers& Buffers)
	{
		if (Buffers.IsEmpty())
		{
			return nullptr;
		}
		UStaticMesh* Mesh = Kit::BuildMesh(GetTransientPackage(), Buffers, Kit::VertexColorMaterial());
		if (Mesh)
		{
			Mesh->AddToRoot();
		}
		return Mesh;
	}

	// ── Charco de pesca ─────────────────────────────────────────────────────────────────────────────────────────────

	void BuildPool(FBuffers& B)
	{
		const double Water = TNCoopItemTuning::PoolRadius;
		// Arena mojada alrededor, el agua (algo brillante) y un anillo más claro en la orilla.
		Kit::AddAnnulus(B, FVector(0.0, 0.0, 0.6), FVector::UpVector, Water - 4.0, Water + 38.0, 28, Kit::Rgb(0x9C7A4E, 0.f));
		Kit::AddDisc(B, FVector(0.0, 0.0, 1.6), FVector::UpVector, Water, 28, Kit::Rgb(0x2C8FC0, 0.55f));
		Kit::AddDisc(B, FVector(-18.0, 14.0, 1.9), FVector::UpVector, Water * 0.55, 20, Kit::Rgb(0x3FB0DA, 0.6f));
		Kit::AddAnnulus(B, FVector(0.0, 0.0, 1.8), FVector::UpVector, Water - 10.0, Water - 2.0, 28, Kit::Rgb(0xBFE8F2, 0.4f));
		// Piedras de la orilla (no todas iguales).
		constexpr int32 Stones = 9;
		for (int32 Index = 0; Index < Stones; ++Index)
		{
			const double Angle = Kit::KitTwoPi * (Index + 0.3 * (Index % 3)) / Stones;
			const double Size = 9.0 + 5.0 * Kit::Hash01(Index, 7, 0x51u);
			const FVector Center(FMath::Cos(Angle) * (Water + 12.0), FMath::Sin(Angle) * (Water + 12.0), Size * 0.35);
			const uint32 Tone = (Index % 2) ? 0x8A8378 : 0x6E675E;
			Kit::AddEllipsoid(B, Center, FVector::ForwardVector, FVector::RightVector, FVector::UpVector, FVector(Size, Size * 0.8, Size * 0.55), 8, 4,
				Kit::Rgb(Tone, 0.05f));
		}
		// Caña clavada en la arena con el sedal hasta un corcho rojo y blanco que flota.
		const FVector RodBase(Water + 30.0, 46.0, 0.0);
		const FVector RodTip(Water - 30.0, 18.0, 120.0);
		Kit::AddRod(B, RodBase, RodTip, 2.2, 6, Kit::Rgb(0x7A4A22, 0.1f), FVector::UpVector);
		Kit::AddBall(B, RodBase + (RodTip - RodBase) * 0.22, 4.0, 8, Kit::Rgb(0x3A3A40, 0.4f));
		const FVector Float(30.0, 6.0, 4.0);
		Kit::AddRod(B, RodTip, Float + FVector(0.0, 0.0, 4.0), 0.35, 4, Kit::Rgb(0xEDEDED, 0.f), FVector::ForwardVector);
		Kit::AddBall(B, Float + FVector(0.0, 0.0, 2.5), 4.5, 10, Kit::Rgb(0xE63946, 0.2f));
		Kit::AddBall(B, Float + FVector(0.0, 0.0, -0.5), 4.2, 10, Kit::Rgb(0xF5F5F5, 0.2f));
	}

	// ── Objetos ─────────────────────────────────────────────────────────────────────────────────────────────────────

	/** Construye la malla de Kind. false si no tiene. */
	bool BuildKind(ETNCoopItem Kind, FBuffers& B)
	{
		switch (Kind)
		{
		case ETNCoopItem::None:
		default:
			return false;
		}
	}

	UStaticMesh* GetMesh(ETNCoopItem Kind)
	{
		static TMap<int32, UStaticMesh*> Cache;
		const int32 Key = static_cast<int32>(Kind);
		if (UStaticMesh** Found = Cache.Find(Key))
		{
			return *Found;
		}
		FBuffers Buffers;
		UStaticMesh* Mesh = BuildKind(Kind, Buffers) ? Finish(Buffers) : nullptr;
		Cache.Add(Key, Mesh);
		return Mesh;
	}

	// ── Iconos ──────────────────────────────────────────────────────────────────────────────────────────────────────

	/** Dibuja el objeto en Painter. false si no tiene icono. */
	bool PaintKind(TNHUDArt::FPainter& Painter, ETNCoopItem Kind)
	{
		switch (Kind)
		{
		case ETNCoopItem::None:
		default:
			return false;
		}
	}

	/** Segmentos encendidos de cada cifra (a, b, c, d, e, f, g en los bits 0-6), como un marcador de siete segmentos. */
	constexpr uint8 DigitSegments[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };

	/** Distancia con signo a la cifra Digit de siete segmentos con la esquina de arriba a la izquierda en (X, Y). */
	float DigitSdf(float px, float py, int32 Digit, float X, float Y)
	{
		constexpr float W = 11.f;
		constexpr float H = 9.f;
		constexpr float T = 2.4f;
		const FVector2f P[6] = { { X, Y }, { X + W, Y }, { X + W, Y + H }, { X + W, Y + 2.f * H }, { X, Y + 2.f * H }, { X, Y + H } };
		// a: 0-1, b: 1-2, c: 2-3, d: 3-4, e: 4-5, f: 5-0, g: 5-2.
		const int32 From[7] = { 0, 1, 2, 3, 4, 5, 5 };
		const int32 To[7] = { 1, 2, 3, 4, 5, 0, 2 };
		const uint8 Mask = DigitSegments[FMath::Clamp(Digit, 0, 9)];
		float Dist = 1000.f;
		for (int32 Seg = 0; Seg < 7; ++Seg)
		{
			if (Mask & (1 << Seg))
			{
				Dist = FMath::Min(Dist, TNHUDArt::Segment(px, py, P[From[Seg]].X, P[From[Seg]].Y, P[To[Seg]].X, P[To[Seg]].Y, T));
			}
		}
		return Dist;
	}

	/** La cuenta (2-99) en una chapa redonda abajo a la derecha del icono. */
	void PaintCount(TNHUDArt::FPainter& Painter, int32 Count)
	{
		using namespace TNHUDArt;
		const int32 Shown = FMath::Clamp(Count, 0, 99);
		if (Shown < 2)
		{
			return;
		}
		const bool bTwo = Shown >= 10;
		const float Cx = bTwo ? 104.f : 110.f;
		const float Cy = 108.f;
		const float Radius = bTwo ? 21.f : 16.f;
		Painter.Fill([=](float px, float py) { return Circle(px, py, Cx, Cy, Radius + 2.5f); }, Cream);
		Painter.Fill([=](float px, float py) { return Circle(px, py, Cx, Cy, Radius); }, NavyDeep);
		const auto Number = [=](float px, float py)
		{
			if (!bTwo)
			{
				return DigitSdf(px, py, Shown, Cx - 5.5f, Cy - 9.f);
			}
			return FMath::Min(DigitSdf(px, py, Shown / 10, Cx - 15.f, Cy - 9.f), DigitSdf(px, py, Shown % 10, Cx + 4.f, Cy - 9.f));
		};
		Painter.Fill(Number, Gold);
	}

	UTexture2D* PaintIcon(ETNCoopItem Kind, int32 Count)
	{
		TNHUDArt::FPainter Painter(IconSize, IconSize);
		if (!PaintKind(Painter, Kind))
		{
			return nullptr;
		}
		PaintCount(Painter, Count);
		return Painter.ToTexture(*FString::Printf(TEXT("TN_Coop_Item_%d_%d"), static_cast<int32>(Kind), Count));
	}
}

bool TNCoopItemArt::GetHeldLook(ETNCoopItem Kind, FHeldLook& OutLook)
{
	OutLook = FHeldLook();
	if (TNCoopItemArtDetail::IsHeadless() || Kind == ETNCoopItem::None || Kind >= ETNCoopItem::Count)
	{
		return false;
	}
	OutLook.Mesh = TNCoopItemArtDetail::GetMesh(Kind);
	return OutLook.Mesh != nullptr;
}

UTexture2D* TNCoopItemArt::GetIcon(ETNCoopItem Kind, int32 Count)
{
	if (TNCoopItemArtDetail::IsHeadless() || Kind == ETNCoopItem::None || Kind >= ETNCoopItem::Count)
	{
		return nullptr;
	}
	const int32 Shown = FMath::Clamp(Count, 1, 99);
	const FName Key(*FString::Printf(TEXT("CoopItem_%d_%d"), static_cast<int32>(Kind), Shown));
	return TNHUDArt::Cached(Key, [Kind, Shown]() -> UTexture2D* { return TNCoopItemArtDetail::PaintIcon(Kind, Shown); });
}

UStaticMesh* TNCoopItemArt::GetPoolMesh()
{
	if (TNCoopItemArtDetail::IsHeadless())
	{
		return nullptr;
	}
	static UStaticMesh* Cached = nullptr;
	static bool bBuilt = false;
	if (!bBuilt)
	{
		bBuilt = true;
		TNCoopItemArtDetail::FBuffers Buffers;
		TNCoopItemArtDetail::BuildPool(Buffers);
		Cached = TNCoopItemArtDetail::Finish(Buffers);
	}
	return Cached;
}
