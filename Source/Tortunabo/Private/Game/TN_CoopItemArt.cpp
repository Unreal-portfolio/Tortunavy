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

	/** Dirección I de Count repartidas por igual por la esfera (espiral de Fibonacci). */
	FVector SphereDir(int32 I, int32 Count)
	{
		const double Z = 1.0 - 2.0 * (I + 0.5) / FMath::Max(1, Count);
		const double R = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
		const double Phi = I * 2.39996322972865332;
		return FVector(FMath::Cos(Phi) * R, FMath::Sin(Phi) * R, Z);
	}

	/** Pincho cónico de Base hacia fuera (Dir), en dos tonos (la punta más oscura). */
	void AddSpike(FBuffers& B, const FVector& Base, const FVector& Dir, double Length, double Radius, const FLinearColor& Color, const FLinearColor& Tip)
	{
		const FVector Mid = Base + Dir * (Length * 0.55);
		Kit::AddFrustum(B, Base, Mid, Radius, Radius * 0.5, 6, Color, Color, false, false);
		Kit::AddFrustum(B, Mid, Base + Dir * Length, Radius * 0.5, 0.0, 6, Tip, Tip, false, false);
	}

	/** Pez globo inflado (unos 30 cm): cuerpo amarillo con la tripa clara, pinchos, ojos saltones, boquita y cola. */
	void BuildPufferFish(FBuffers& B)
	{
		const double Body = 12.0;
		Kit::AddEllipsoid(B, FVector::ZeroVector, FVector::ForwardVector, FVector::RightVector, FVector::UpVector, FVector(Body, Body * 0.95, Body * 0.9), 16, 8,
			Kit::Rgb(0xF2C14E, 0.15f));
		Kit::AddEllipsoid(B, FVector(1.0, 0.0, -4.5), FVector::ForwardVector, FVector::RightVector, FVector::UpVector, FVector(Body * 0.82, Body * 0.78, Body * 0.55),
			14, 6, Kit::Rgb(0xFFF1C9, 0.1f));
		constexpr int32 Spikes = 26;
		for (int32 Index = 0; Index < Spikes; ++Index)
		{
			const FVector Dir = SphereDir(Index, Spikes);
			if (Dir.X > 0.75)
			{
				continue; // la cara, sin pinchos
			}
			AddSpike(B, Dir * (Body * 0.92), Dir, 5.0, 1.5, Kit::Rgb(0xE0A73A, 0.1f), Kit::Rgb(0x8A5A1E, 0.1f));
		}
		for (const double Side : { -1.0, 1.0 })
		{
			Kit::AddBall(B, FVector(8.5, Side * 5.0, 4.0), 3.2, 10, Kit::Rgb(0xFFFFFF, 0.3f));
			Kit::AddBall(B, FVector(11.0, Side * 5.6, 4.4), 1.6, 8, Kit::Rgb(0x13233B, 0.5f));
		}
		Kit::AddEllipsoid(B, FVector(12.0, 0.0, -0.5), FVector::ForwardVector, FVector::RightVector, FVector::UpVector, FVector(1.6, 2.6, 1.8), 10, 5,
			Kit::Rgb(0xFF7A52, 0.2f));
		// Cola: dos lóbulos aplastados detrás.
		Kit::AddEllipsoid(B, FVector(-14.0, 0.0, 2.5), FVector::ForwardVector, FVector::RightVector, FVector::UpVector, FVector(4.0, 1.0, 4.5), 10, 5,
			Kit::Rgb(0xE8A23C, 0.1f));
		Kit::AddEllipsoid(B, FVector(-14.0, 0.0, -2.5), FVector::ForwardVector, FVector::RightVector, FVector::UpVector, FVector(4.0, 1.0, 4.5), 10, 5,
			Kit::Rgb(0xE8A23C, 0.1f));
	}

	/** Cáscara resbaladiza (unos 32 cm): cuatro tiras de piel de plátano abiertas en el suelo, con el rabito en medio. */
	void BuildPeel(FBuffers& B)
	{
		constexpr int32 Flaps = 4;
		for (int32 Index = 0; Index < Flaps; ++Index)
		{
			const double Yaw = Kit::KitTwoPi * (Index + 0.15 * (Index % 2)) / Flaps;
			const FVector Out(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.0);
			const FVector Side(-Out.Y, Out.X, 0.0);
			// La tira se curva hacia arriba en la punta: dos tramos, el de fuera algo levantado y con la punta oscura.
			const FVector Near = Out * 6.0 + FVector(0.0, 0.0, 1.0);
			const FVector Far = Out * 13.0 + FVector(0.0, 0.0, 2.5);
			Kit::AddEllipsoid(B, Near, Out, Side, FVector::UpVector, FVector(6.0, 3.6, 1.2), 10, 4, Kit::Rgb(0xF5D547, 0.1f));
			const FVector Lift = (Out + FVector(0.0, 0.0, 0.35)).GetSafeNormal();
			Kit::AddEllipsoid(B, Far, Lift, Side, FVector::CrossProduct(Lift, Side).GetSafeNormal() * -1.0, FVector(5.0, 3.0, 1.0), 10, 4, Kit::Rgb(0xE9C33A, 0.1f));
			Kit::AddBall(B, Out * 17.5 + FVector(0.0, 0.0, 4.0), 1.4, 6, Kit::Rgb(0x5A3A10, 0.f));
		}
		// Rabito y la pulpa que asoma.
		Kit::AddEllipsoid(B, FVector(0.0, 0.0, 2.5), FVector::ForwardVector, FVector::RightVector, FVector::UpVector, FVector(4.0, 4.0, 3.0), 10, 5,
			Kit::Rgb(0xFFF2C2, 0.05f));
		Kit::AddFrustum(B, FVector(0.0, 0.0, 4.5), FVector(0.0, 0.0, 9.5), 1.6, 1.1, 6, Kit::Rgb(0x6E4A1A, 0.f), Kit::Rgb(0x3A2408, 0.f), false, true);
	}

	/**
	 * Concha para lanzar (unos 26 cm): una caracola de espiral (no la vieira de los puntos, para no confundirlas), con
	 * nudos en las vueltas y la boca rosa.
	 */
	void BuildStunShell(FBuffers& B)
	{
		const FLinearColor Shell = Kit::Rgb(0xF4E6D8, 0.35f);
		const FLinearColor Band = Kit::Rgb(0xC98B6B, 0.3f);
		Kit::AddFrustum(B, FVector(-8.0, 0.0, 0.0), FVector(13.0, 0.0, 0.0), 9.0, 1.2, 14, Shell, Shell, true, false);
		// Vueltas de la espiral: bandas y nudos.
		for (int32 Turn = 0; Turn < 4; ++Turn)
		{
			const double X = -5.0 + Turn * 4.5;
			const double R = 9.0 - (X + 8.0) * (7.8 / 21.0);
			Kit::AddFrustum(B, FVector(X, 0.0, 0.0), FVector(X + 1.2, 0.0, 0.0), R + 0.6, R + 0.3, 14, Band, Band, false, false);
			for (int32 Knob = 0; Knob < 5; ++Knob)
			{
				const double A = Kit::KitTwoPi * (Knob + 0.5 * Turn) / 5.0;
				Kit::AddBall(B, FVector(X + 0.6, FMath::Cos(A) * (R + 0.6), FMath::Sin(A) * (R + 0.6)), 1.4, 6, Shell);
			}
		}
		// Boca abierta, rosa por dentro.
		Kit::AddEllipsoid(B, FVector(-9.0, 1.5, -1.5), FVector::ForwardVector, FVector::RightVector, FVector::UpVector, FVector(3.0, 8.0, 7.0), 12, 6,
			Kit::Rgb(0xFF9EB0, 0.4f));
		Kit::AddEllipsoid(B, FVector(-10.5, 1.5, -1.5), FVector::ForwardVector, FVector::RightVector, FVector::UpVector, FVector(1.5, 5.5, 4.5), 10, 5,
			Kit::Rgb(0xE0607A, 0.3f));
	}

	/** Arpón (unos 44 cm a lo largo de X): asta de madera, punta de metal con dos lengüetas, empuñadura y rollo de cuerda. */
	void BuildHarpoon(FBuffers& B)
	{
		const FLinearColor Wood = Kit::Rgb(0x9A6A3A, 0.1f);
		const FLinearColor Metal = Kit::Rgb(0xC9D2DC, 0.8f);
		const FLinearColor Rope = Kit::Rgb(0xE8D7A8, 0.f);
		Kit::AddRod(B, FVector(-22.0, 0.0, 0.0), FVector(12.0, 0.0, 0.0), 1.6, 8, Wood, FVector::UpVector);
		Kit::AddFrustum(B, FVector(12.0, 0.0, 0.0), FVector(22.0, 0.0, 0.0), 2.6, 0.0, 10, Metal, Metal, true, false);
		for (const double Side : { -1.0, 1.0 })
		{
			Kit::AddRod(B, FVector(15.0, 0.0, 0.0), FVector(11.0, Side * 3.6, 0.0), 0.7, 5, Metal, FVector::UpVector);
		}
		Kit::AddRod(B, FVector(-22.0, 0.0, 0.0), FVector(-12.0, 0.0, 0.0), 2.1, 8, Kit::Rgb(0x3A2A1E, 0.1f), FVector::UpVector);
		// Rollo de cuerda atado al asta.
		for (int32 Index = 0; Index < 10; ++Index)
		{
			const double A = Kit::KitTwoPi * Index / 10.0;
			Kit::AddBall(B, FVector(-6.0 + FMath::Sin(A) * 1.0, FMath::Cos(A) * 4.2, FMath::Sin(A) * 4.2 - 1.5), 1.3, 6, Rope);
		}
	}

	/** Corona de pinchos alrededor de la tortuga protegida. */
	void BuildPufferSpikes(FBuffers& B)
	{
		constexpr int32 Spikes = 34;
		constexpr double Radius = 58.0;
		for (int32 Index = 0; Index < Spikes; ++Index)
		{
			const FVector Dir = SphereDir(Index, Spikes);
			if (Dir.Z < -0.7)
			{
				continue; // por debajo, en el suelo, no se verían
			}
			AddSpike(B, Dir * Radius, Dir, 20.0, 3.6, Kit::Rgb(0xF2C14E, 0.2f), Kit::Rgb(0x8A5A1E, 0.15f));
		}
	}

	/** Construye la malla de Kind. false si no tiene. */
	bool BuildKind(ETNCoopItem Kind, FBuffers& B)
	{
		switch (Kind)
		{
		case ETNCoopItem::PufferFish:
			BuildPufferFish(B);
			return true;
		case ETNCoopItem::SlipperyPeel:
			BuildPeel(B);
			return true;
		case ETNCoopItem::StunShell:
			BuildStunShell(B);
			return true;
		case ETNCoopItem::Harpoon:
			BuildHarpoon(B);
			return true;
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

	/** Silueta con filo oscuro y degradado vertical (como los iconos de Todos contra Todos). */
	template <typename FSdf>
	void Body(TNHUDArt::FPainter& Painter, const FSdf& Sdf, uint32 Edge, uint32 Top, uint32 Bottom, float TopY, float BottomY)
	{
		using namespace TNHUDArt;
		Painter.Fill([&Sdf](float px, float py) { return Sdf(px, py) - 2.f; }, Hex(Edge));
		const FLinearColor TopColor = Hex(Top);
		const FLinearColor BottomColor = Hex(Bottom);
		Painter.Layer(Sdf, [&TopColor, &BottomColor, TopY, BottomY](float, float py)
		{
			return Mix(TopColor, BottomColor, (py - TopY) / FMath::Max(1.f, BottomY - TopY));
		});
	}

	/** Estrella de Points puntas alrededor de (Cx, Cy) entre los radios Inner y Outer. */
	TArray<FVector2f> StarPoints(float Cx, float Cy, int32 Points, float Inner, float Outer)
	{
		TArray<FVector2f> Out;
		for (int32 Index = 0; Index < Points * 2; ++Index)
		{
			const float Angle = PI * Index / Points;
			const float Radius = (Index % 2) ? Inner : Outer;
			Out.Add(FVector2f(Cx + FMath::Cos(Angle) * Radius, Cy + FMath::Sin(Angle) * Radius));
		}
		return Out;
	}

	void PaintPufferFish(TNHUDArt::FPainter& Painter)
	{
		using namespace TNHUDArt;
		const TArray<FVector2f> Star = StarPoints(60.f, 64.f, 14, 34.f, 48.f);
		const TArray<FVector2f> TailPoints = { { 22.f, 64.f }, { 4.f, 44.f }, { 4.f, 84.f } };
		const auto Spikes = [&Star](float px, float py) { return Polygon(px, py, Star); };
		const auto Tail = [&TailPoints](float px, float py) { return Polygon(px, py, TailPoints); };
		const auto Fish = [](float px, float py) { return Circle(px, py, 60.f, 64.f, 36.f); };
		Painter.Sticker([&](float px, float py) { return FMath::Min(Spikes(px, py), Tail(px, py)); }, 5.f);
		Body(Painter, Tail, 0x5A3A10, 0xF2B24A, 0xC9832A, 44.f, 84.f);
		Body(Painter, Spikes, 0x5A3A10, 0xC9862E, 0x8A5A1E, 16.f, 112.f);
		Body(Painter, Fish, 0x5A3A10, 0xFFD86B, 0xF0A93A, 28.f, 100.f);
		Painter.Fill([&](float px, float py) { return FMath::Max(Ellipse(px, py, 62.f, 84.f, 26.f, 14.f), Fish(px, py) + 1.f); }, Hex(0xFFF3CF));
		Painter.Fill([](float px, float py) { return Circle(px, py, 76.f, 52.f, 10.f); }, Hex(0xFFFFFF));
		Painter.Fill([](float px, float py) { return Circle(px, py, 79.f, 53.f, 5.f); }, Hex(0x13233B));
		Painter.Fill([](float px, float py) { return Ellipse(px, py, 95.f, 68.f, 4.f, 5.f); }, Hex(0xFF7A52));
		Painter.Fill([](float px, float py) { return Ellipse(px, py, 50.f, 42.f, 10.f, 4.f); }, Hex(0xFFFFFF, 0.5f));
	}

	void PaintPeel(TNHUDArt::FPainter& Painter)
	{
		using namespace TNHUDArt;
		// Cuatro tiras abiertas desde el centro, cada una una cápsula gruesa con la punta oscura.
		const FVector2f Center(64.f, 70.f);
		const float Angles[4] = { -150.f, -30.f, 40.f, 140.f };
		const auto Flap = [&Center, &Angles](float px, float py, int32 Index)
		{
			const float A = FMath::DegreesToRadians(Angles[Index]);
			return Segment(px, py, Center.X, Center.Y, Center.X + FMath::Cos(A) * 40.f, Center.Y + FMath::Sin(A) * 34.f, 11.f);
		};
		const auto All = [&Flap](float px, float py)
		{
			return FMath::Min(FMath::Min(Flap(px, py, 0), Flap(px, py, 1)), FMath::Min(Flap(px, py, 2), Flap(px, py, 3)));
		};
		const auto Stem = [](float px, float py) { return Box(px, py, 64.f, 40.f, 5.f, 14.f, 3.f); };
		Painter.Sticker([&](float px, float py) { return FMath::Min(All(px, py), Stem(px, py)); }, 5.f);
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const auto One = [&Flap, Index](float px, float py) { return Flap(px, py, Index); };
			Body(Painter, One, 0x5A4410, 0xFFE36B, 0xE0B530, 30.f, 110.f);
			const float A = FMath::DegreesToRadians(Angles[Index]);
			const float Tx = Center.X + FMath::Cos(A) * 40.f;
			const float Ty = Center.Y + FMath::Sin(A) * 34.f;
			Painter.Fill([Tx, Ty](float px, float py) { return Circle(px, py, Tx, Ty, 7.f); }, Hex(0x6E4A1A));
		}
		Painter.Fill([](float px, float py) { return Circle(px, py, 64.f, 70.f, 14.f); }, Hex(0xFFF3CF));
		Body(Painter, Stem, 0x2A1A06, 0x8A5A24, 0x5A3A10, 26.f, 56.f);
	}

	void PaintStunShell(TNHUDArt::FPainter& Painter)
	{
		using namespace TNHUDArt;
		// Caracola de lado: cono con la punta arriba a la derecha y la boca abajo a la izquierda, con bandas y chispa de golpe.
		const TArray<FVector2f> Cone = { { 30.f, 70.f }, { 50.f, 38.f }, { 104.f, 18.f }, { 86.f, 74.f }, { 58.f, 98.f } };
		const auto ShellBody = [&Cone](float px, float py) { return Polygon(px, py, Cone) - 6.f; };
		const auto Mouth = [](float px, float py) { return Ellipse(px, py, 40.f, 84.f, 22.f, 16.f); };
		const TArray<FVector2f> Spark = StarPoints(102.f, 96.f, 4, 5.f, 16.f);
		const auto SparkSdf = [&Spark](float px, float py) { return Polygon(px, py, Spark); };
		Painter.Sticker([&](float px, float py) { return FMath::Min(FMath::Min(ShellBody(px, py), Mouth(px, py)), SparkSdf(px, py)); }, 5.f);
		TNCoopItemArtDetail::Body(Painter, ShellBody, 0x5A3020, 0xFFF4E6, 0xE2C2A6, 18.f, 98.f);
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const float T = 0.3f + Index * 0.2f;
			const FVector2f A = FMath::Lerp(FVector2f(40.f, 54.f), FVector2f(100.f, 22.f), T);
			const FVector2f C = FMath::Lerp(FVector2f(52.f, 96.f), FVector2f(92.f, 70.f), T);
			Painter.Fill([&, A, C](float px, float py) { return FMath::Max(Segment(px, py, A.X, A.Y, C.X, C.Y, 2.5f), ShellBody(px, py) + 1.f); }, Hex(0xC98B6B));
		}
		TNCoopItemArtDetail::Body(Painter, Mouth, 0x5A2030, 0xFFB3C1, 0xE0607A, 70.f, 100.f);
		Painter.Fill([](float px, float py) { return Ellipse(px, py, 40.f, 86.f, 11.f, 7.f); }, Hex(0xA8344E));
		Painter.Fill(SparkSdf, Gold);
	}

	void PaintHarpoon(TNHUDArt::FPainter& Painter)
	{
		using namespace TNHUDArt;
		// En diagonal: empuñadura abajo a la izquierda, punta arriba a la derecha y la cuerda en bucle.
		const auto Shaft = [](float px, float py) { return Segment(px, py, 18.f, 110.f, 92.f, 36.f, 5.f); };
		const auto Grip = [](float px, float py) { return Segment(px, py, 16.f, 112.f, 36.f, 92.f, 7.f); };
		const TArray<FVector2f> HeadPoints = { { 86.f, 28.f }, { 118.f, 10.f }, { 100.f, 42.f } };
		const TArray<FVector2f> BarbPoints = { { 90.f, 38.f }, { 80.f, 30.f }, { 96.f, 30.f }, { 98.f, 46.f } };
		const auto Head = [&HeadPoints](float px, float py) { return Polygon(px, py, HeadPoints); };
		const auto Barb = [&BarbPoints](float px, float py) { return Polygon(px, py, BarbPoints); };
		const auto Loop = [](float px, float py) { return FMath::Abs(Ellipse(px, py, 52.f, 84.f, 18.f, 11.f)) - 2.5f; };
		const auto All = [&](float px, float py)
		{
			return FMath::Min(FMath::Min(FMath::Min(Shaft(px, py), Grip(px, py)), FMath::Min(Head(px, py), Barb(px, py))), Loop(px, py));
		};
		Painter.Sticker(All, 6.f);
		Painter.Fill([&](float px, float py) { return Loop(px, py) + 1.2f; }, Hex(0x6A5A30));
		Painter.Fill(Loop, Hex(0xF0E2B6));
		Body(Painter, Shaft, 0x3A2410, 0xC89A5E, 0x8A5A2C, 36.f, 110.f);
		Body(Painter, Grip, 0x1A120A, 0x5A4030, 0x2A1E14, 92.f, 112.f);
		Body(Painter, Barb, 0x2A3038, 0xE6ECF2, 0x8A96A4, 28.f, 46.f);
		Body(Painter, Head, 0x2A3038, 0xF4F8FC, 0x9AA6B4, 10.f, 42.f);
	}

	/** Dibuja el objeto en Painter. false si no tiene icono. */
	bool PaintKind(TNHUDArt::FPainter& Painter, ETNCoopItem Kind)
	{
		switch (Kind)
		{
		case ETNCoopItem::PufferFish:
			PaintPufferFish(Painter);
			return true;
		case ETNCoopItem::SlipperyPeel:
			PaintPeel(Painter);
			return true;
		case ETNCoopItem::StunShell:
			PaintStunShell(Painter);
			return true;
		case ETNCoopItem::Harpoon:
			PaintHarpoon(Painter);
			return true;
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

UStaticMesh* TNCoopItemArt::GetPufferSpikesMesh()
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
		TNCoopItemArtDetail::BuildPufferSpikes(Buffers);
		Cached = TNCoopItemArtDetail::Finish(Buffers);
	}
	return Cached;
}
