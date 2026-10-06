#include "TN_RaceItemArtExtra.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Misc/App.h"
#include "UObject/Package.h"
#include "../../Lobby/Playground/TN_PlaygroundMeshKit.h"
#include "../../UI/HUD/TN_HUDArt.h"

// ─────────────────────────────────────────────────────────────────────────────
// Arte de los objetos de carrera de la issue #786: mallas de juguete (kit del parque, brillo en el alfa del color de
// vértice) e iconos estilo pegatina, como TN_RaceItemArt.cpp.
// ─────────────────────────────────────────────────────────────────────────────

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto del
// bloque.
namespace TNRaceItemArtExtraDetail
{
	using FBuffers = TNPlaygroundKit::FBuffers;
	namespace Kit = TNPlaygroundKit;

	constexpr int32 IconSize = 128;

	FLinearColor Tint(uint32 Rgb24, float Shine = 0.f)
	{
		return Kit::Rgb(Rgb24, Shine);
	}

	bool IsHeadless()
	{
		return IsRunningDedicatedServer() || !FApp::CanEverRender();
	}

	/** Centra la malla en su caja (las de la mano: ATN_PickupInteractableBase supone el origen en el centro). */
	void Center(FBuffers& B)
	{
		if (B.Verts.Num() == 0)
		{
			return;
		}
		FBox Bounds(ForceInit);
		for (const FVector& Vert : B.Verts)
		{
			Bounds += Vert;
		}
		const FVector Mid = Bounds.GetCenter();
		for (FVector& Vert : B.Verts)
		{
			Vert -= Mid;
		}
	}

	/** Cuadrilátero de dos caras (láminas finas: la ola, el agua). */
	void TwoSidedQuad(FBuffers& B, const FVector (&P)[4], const FVector& Normal, const FLinearColor (&C)[4])
	{
		const FVector N[4] = { Normal, Normal, Normal, Normal };
		Kit::SmoothQuad(B, P, N, C);
		const FVector Back[4] = { -Normal, -Normal, -Normal, -Normal };
		Kit::SmoothQuad(B, P, Back, C);
	}

	// ── Tabla de surf ────────────────────────────────────────────────────────

	/** Tabla de Length x Width (cm), punta hacia +X, con la cara de abajo en Z = 0; franja y quilla. */
	void AddBoard(FBuffers& B, double Length, double Width, double Thick, const FLinearColor& Body, const FLinearColor& Stripe, bool bFin)
	{
		TArray<FVector2D> Outline;
		constexpr int32 Steps = 18;
		for (int32 i = 0; i < Steps; ++i)
		{
			const double A = Kit::KitTwoPi * i / Steps;
			const double C = FMath::Cos(A);
			const double S = FMath::Sin(A);
			// Punta afilada delante (+X) y cola redonda detrás: el ancho se estrecha hacia la punta.
			const double Taper = C > 0.0 ? 1.0 - 0.55 * C * C : 1.0 - 0.1 * C * C;
			Outline.Add(FVector2D(C * Length * 0.5, S * Width * 0.5 * Taper));
		}
		Kit::AddSlab(B, FVector(0.0, 0.0, Thick * 0.5), FVector::ForwardVector, FVector::RightVector, FVector::UpVector, Outline, Thick, Body);
		// Franja de lado a lado y raya central.
		Kit::AddAxisBox(B, FVector(Length * 0.08, 0.0, Thick + 0.15), FVector(Length * 0.06, Width * 0.4, 0.2), Stripe);
		Kit::AddAxisBox(B, FVector(-Length * 0.05, 0.0, Thick + 0.1), FVector(Length * 0.32, Width * 0.03, 0.15), Stripe);
		if (bFin)
		{
			const TArray<FVector2D> FinOutline = { { 0.0, 0.0 }, { Length * 0.08, 0.0 }, { Length * 0.01, -Width * 0.32 } };
			Kit::AddSlab(B, FVector(-Length * 0.4, 0.0, 0.0), FVector::ForwardVector, FVector::UpVector, FVector::RightVector, FinOutline, 1.2, Stripe);
		}
	}

	void BuildSurfItem(FBuffers& B)
	{
		AddBoard(B, 36.0, 11.5, 1.6, Tint(0x2EC4B6, 0.35f), Tint(0xFFCB3D, 0.3f), true);
		// Florecita en la punta.
		Kit::AddBall(B, FVector(9.0, 0.0, 1.9), 1.4, 8, Tint(0xFF7EB6, 0.3f));
		Center(B);
	}

	void BuildSurfBoardPiece(FBuffers& B)
	{
		AddBoard(B, 170.0, 52.0, 7.0, Tint(0x2EC4B6, 0.35f), Tint(0xFFCB3D, 0.3f), true);
		Kit::AddBall(B, FVector(45.0, 0.0, 7.5), 6.0, 10, Tint(0xFF7EB6, 0.3f));
	}

	// ── Ola ──────────────────────────────────────────────────────────────────

	void BuildWavePiece(FBuffers& B)
	{
		// Perfil (X, Z) de la base a la cresta que se enrosca hacia delante; el ancho va a lo largo de Y.
		const FVector2D Profile[] = { { -90.0, 0.0 }, { -55.0, 35.0 }, { -25.0, 85.0 }, { 0.0, 130.0 }, { 25.0, 158.0 }, { 55.0, 150.0 }, { 72.0, 122.0 } };
		constexpr int32 NumProfile = UE_ARRAY_COUNT(Profile);
		constexpr int32 Cols = 10;
		constexpr double HalfWidth = 150.0;
		const auto Color = [=](int32 Row) -> FLinearColor
		{
			const double T = static_cast<double>(Row) / (NumProfile - 1);
			return T < 0.72 ? Kit::Mix(Kit::Rgb(0x1F6FB2, 0.45f), Kit::Rgb(0x4CC9F0, 0.5f), T / 0.72) : Kit::Mix(Kit::Rgb(0x9BE7F5, 0.5f), Kit::Rgb(0xFFFFFF, 0.3f), (T - 0.72) / 0.28);
		};
		for (int32 c = 0; c < Cols; ++c)
		{
			const double Y0 = -HalfWidth + 2.0 * HalfWidth * c / Cols;
			const double Y1 = -HalfWidth + 2.0 * HalfWidth * (c + 1) / Cols;
			// Los lados bajan: la ola es más alta en el centro.
			const auto Height = [=](double Y) { return 0.55 + 0.45 * FMath::Cos(0.5 * PI * Y / HalfWidth); };
			for (int32 r = 0; r + 1 < NumProfile; ++r)
			{
				const FVector P[4] = {
					FVector(Profile[r].X, Y0, Profile[r].Y * Height(Y0)),
					FVector(Profile[r].X, Y1, Profile[r].Y * Height(Y1)),
					FVector(Profile[r + 1].X, Y1, Profile[r + 1].Y * Height(Y1)),
					FVector(Profile[r + 1].X, Y0, Profile[r + 1].Y * Height(Y0)) };
				const FVector2D Along = Profile[r + 1] - Profile[r];
				const FVector Normal = FVector(-Along.Y, 0.0, Along.X).GetSafeNormal();
				const FLinearColor C[4] = { Color(r), Color(r), Color(r + 1), Color(r + 1) };
				TwoSidedQuad(B, P, Normal, C);
			}
		}
		// Espuma por la cresta.
		for (int32 k = 0; k <= 12; ++k)
		{
			const double Y = -HalfWidth * 0.9 + 1.8 * HalfWidth * k / 12.0;
			const double H = 0.55 + 0.45 * FMath::Cos(0.5 * PI * Y / HalfWidth);
			Kit::AddBall(B, FVector(40.0, Y, 156.0 * H), 9.0 + 4.0 * FMath::Abs(FMath::Sin(k * 1.7)), 8, Tint(0xFFFFFF, 0.2f));
		}
	}

	// ── Cohete de feria ──────────────────────────────────────────────────────

	/** Cohete de Length cm hacia +X desde la tobera (X = 0), con franjas, punta, aletas y varilla. */
	void AddRocket(FBuffers& B, double Length, double Radius, bool bStick)
	{
		const FLinearColor Red = Tint(0xE63946, 0.35f);
		const FLinearColor White = Tint(0xFFF5DC, 0.35f);
		const FLinearColor Blue = Tint(0x3A5BD9, 0.35f);
		const FLinearColor Gold = Tint(0xFFCB3D, 0.6f);
		const FLinearColor Dark = Tint(0x2B2B33, 0.3f);
		const double BodyLength = Length * 0.72;
		constexpr int32 Bands = 6;
		for (int32 k = 0; k < Bands; ++k)
		{
			const double X0 = BodyLength * k / Bands;
			const double X1 = BodyLength * (k + 1) / Bands;
			Kit::AddFrustum(B, FVector(X0, 0.0, 0.0), FVector(X1, 0.0, 0.0), Radius, Radius, 16, k % 2 == 0 ? Red : White, Dark, k == 0, false);
		}
		// Punta azul con estrella dorada.
		Kit::AddFrustum(B, FVector(BodyLength, 0.0, 0.0), FVector(Length, 0.0, 0.0), Radius * 1.05, 0.4, 16, Blue, Blue, true, false);
		Kit::AddBall(B, FVector(Length, 0.0, 0.0), Radius * 0.22, 8, Gold);
		// Tobera oscura.
		Kit::AddFrustum(B, FVector(-Radius * 0.5, 0.0, 0.0), FVector(0.0, 0.0, 0.0), Radius * 0.8, Radius * 0.65, 12, Dark, Dark, true, false);
		// Tres aletas.
		for (int32 Fin = 0; Fin < 3; ++Fin)
		{
			const double Angle = Kit::KitTwoPi * Fin / 3.0 + 0.5 * PI;
			const FVector Out(0.0, FMath::Cos(Angle), FMath::Sin(Angle));
			const TArray<FVector2D> FinOutline = { { 0.0, Radius * 0.9 }, { Length * 0.22, Radius * 0.9 }, { -Length * 0.04, Radius * 2.2 } };
			Kit::AddSlab(B, FVector::ZeroVector, FVector::ForwardVector, Out, FVector::CrossProduct(FVector::ForwardVector, Out), FinOutline, Radius * 0.18, Blue);
		}
		if (bStick)
		{
			Kit::AddRod(B, FVector(BodyLength * 0.5, 0.0, -Radius), FVector(-Length * 0.45, 0.0, -Radius * 1.1), Radius * 0.14, 6, Tint(0xC8A165, 0.1f), FVector::UpVector);
		}
	}

	void BuildRocketItem(FBuffers& B)
	{
		AddRocket(B, 32.0, 4.0, true);
		Center(B);
	}

	void BuildRocketPiece(FBuffers& B)
	{
		AddRocket(B, 95.0, 12.0, false);
	}

	void BuildFlamePiece(FBuffers& B)
	{
		Kit::AddFrustum(B, FVector::ZeroVector, FVector(-70.0, 0.0, 0.0), 11.0, 0.5, 12, Tint(0xFF6A1F, 0.f), Tint(0xFF6A1F, 0.f), true, false);
		Kit::AddFrustum(B, FVector(1.0, 0.0, 0.0), FVector(-44.0, 0.0, 0.0), 7.5, 0.5, 12, Tint(0xFFE45C, 0.f), Tint(0xFFF7C2, 0.f), true, false);
	}

	// ── Remolino ─────────────────────────────────────────────────────────────

	/** Color del agua del remolino en el radio relativo T (0 centro, 1 borde) y el ángulo Angle: brazos en espiral claros. */
	FLinearColor WhirlColor(double T, double Angle)
	{
		const double Arm = 0.5 + 0.5 * FMath::Sin(3.0 * Angle + 9.0 * T);
		const FLinearColor Deep = Kit::Mix(Kit::Rgb(0x0B3B6F, 0.6f), Kit::Rgb(0x1F6FB2, 0.6f), T);
		const FLinearColor Light = Kit::Mix(Kit::Rgb(0x4CC9F0, 0.6f), Kit::Rgb(0xDFF7FF, 0.5f), T);
		return Kit::Mix(Deep, Light, Arm * Arm * (0.35 + 0.65 * T));
	}

	/** Embudo de agua de radio Radius con el ojo hundido Sink en el centro, en Rings anillos y Seg porciones. */
	void AddWhirlFunnel(FBuffers& B, double Radius, double Sink, int32 Rings, int32 Seg)
	{
		const auto At = [Radius, Sink](double T, double Angle)
		{
			const double Z = -Sink * FMath::Square(1.0 - T);
			return FVector(Radius * T * FMath::Cos(Angle), Radius * T * FMath::Sin(Angle), Z);
		};
		for (int32 r = 0; r < Rings; ++r)
		{
			const double T0 = static_cast<double>(r) / Rings;
			const double T1 = static_cast<double>(r + 1) / Rings;
			for (int32 s = 0; s < Seg; ++s)
			{
				const double A0 = Kit::KitTwoPi * s / Seg;
				const double A1 = Kit::KitTwoPi * (s + 1) / Seg;
				const FVector P[4] = { At(T0, A0), At(T1, A0), At(T1, A1), At(T0, A1) };
				const FVector N[4] = { FVector::UpVector, FVector::UpVector, FVector::UpVector, FVector::UpVector };
				const FLinearColor C[4] = { WhirlColor(T0, A0), WhirlColor(T1, A0), WhirlColor(T1, A1), WhirlColor(T0, A1) };
				Kit::SmoothQuad(B, P, N, C);
			}
		}
	}

	void BuildWhirlWaterPiece(FBuffers& B)
	{
		AddWhirlFunnel(B, 300.0, 45.0, 10, 36);
	}

	void BuildWhirlFoamPiece(FBuffers& B)
	{
		const FLinearColor Foam = Tint(0xF4FDFF, 0.2f);
		Kit::AddAnnulus(B, FVector(0.0, 0.0, 2.0), FVector::UpVector, 282.0, 306.0, 40, Foam);
		for (int32 k = 0; k < 18; ++k)
		{
			const double Angle = Kit::KitTwoPi * k / 18.0;
			const double R = 294.0 + 8.0 * FMath::Sin(k * 2.3);
			Kit::AddEllipsoid(B, FVector(R * FMath::Cos(Angle), R * FMath::Sin(Angle), 4.0), FVector::ForwardVector, FVector::RightVector, FVector::UpVector,
				FVector(16.0, 16.0, 6.0), 8, 4, Foam);
		}
	}

	void BuildWhirlItem(FBuffers& B)
	{
		// Botella de cristal con un remolino dentro: embudo pequeño y corcho.
		AddWhirlFunnel(B, 13.0, 9.0, 5, 20);
		const FLinearColor Glass = Tint(0xBFEFFF, 0.8f);
		Kit::AddAnnulus(B, FVector(0.0, 0.0, 0.5), FVector::UpVector, 12.5, 14.0, 20, Glass);
		Kit::AddFrustum(B, FVector(0.0, 0.0, -10.0), FVector(0.0, 0.0, -1.0), 10.0, 14.0, 20, Tint(0x1F6FB2, 0.6f), Tint(0x0B3B6F, 0.5f), true, false);
		// Espiral blanca que baja hacia el ojo.
		TArray<FVector> Path;
		TArray<double> Radii;
		TArray<FLinearColor> Colors;
		for (int32 k = 0; k <= 24; ++k)
		{
			const double T = static_cast<double>(k) / 24.0;
			const double Angle = 3.0 * Kit::KitTwoPi * T;
			const double R = 12.0 * (1.0 - T) + 1.0;
			Path.Add(FVector(R * FMath::Cos(Angle), R * FMath::Sin(Angle), 2.0 - 9.0 * T * T));
			Radii.Add(0.9 * (1.0 - 0.6 * T));
			Colors.Add(Tint(0xFFFFFF, 0.3f));
		}
		Kit::AddTube(B, Path, Radii, 6, Colors, FVector::UpVector, true);
		Center(B);
	}

	// ── Caña de pescar ───────────────────────────────────────────────────────

	/** Caña de Length cm hacia +X desde el mango, con carrete y anillas. */
	void AddRod(FBuffers& B, double Length, double Scale)
	{
		const FLinearColor Cork = Tint(0xC8A165, 0.1f);
		const FLinearColor Shaft = Tint(0x3A5BD9, 0.5f);
		const FLinearColor Steel = Tint(0xB9C0CE, 0.8f);
		const FLinearColor Reel = Tint(0xFF6A52, 0.4f);
		Kit::AddFrustum(B, FVector::ZeroVector, FVector(Length * 0.22, 0.0, 0.0), 1.6 * Scale, 1.3 * Scale, 10, Cork, Cork, true, false);
		Kit::AddFrustum(B, FVector(Length * 0.22, 0.0, 0.0), FVector(Length, 0.0, 0.0), 0.8 * Scale, 0.25 * Scale, 8, Shaft, Shaft, false, true);
		// Carrete debajo del mango con su manivela.
		Kit::AddFrustum(B, FVector(Length * 0.16, -2.2 * Scale, -3.2 * Scale), FVector(Length * 0.16, 2.2 * Scale, -3.2 * Scale), 3.0 * Scale, 3.0 * Scale, 14, Reel, Steel, true, true);
		Kit::AddRod(B, FVector(Length * 0.16, 2.2 * Scale, -3.2 * Scale), FVector(Length * 0.16 + 2.0 * Scale, 3.6 * Scale, -5.0 * Scale), 0.35 * Scale, 6, Steel, FVector::UpVector);
		for (int32 Ring = 1; Ring <= 3; ++Ring)
		{
			const double X = Length * (0.35 + 0.2 * Ring);
			const double Radius = (0.8 - 0.15 * Ring) * Scale + 0.6 * Scale;
			Kit::AddAnnulus(B, FVector(X, 0.0, -Radius - 0.4 * Scale), FVector::ForwardVector, Radius * 0.55, Radius * 0.75, 8, Steel);
		}
	}

	/** Anzuelo con corcho: el corcho en Bob, el anzuelo colgando debajo. */
	void AddHookAndBobber(FBuffers& B, const FVector& Bob, double Scale)
	{
		const FLinearColor Red = Tint(0xE63946, 0.4f);
		const FLinearColor White = Tint(0xFFFFFF, 0.4f);
		const FLinearColor Steel = Tint(0xB9C0CE, 0.85f);
		Kit::AddEllipsoid(B, Bob + FVector(0.0, 0.0, 1.2 * Scale), FVector::ForwardVector, FVector::RightVector, FVector::UpVector, FVector(2.6, 2.6, 2.0) * Scale, 10, 5, Red);
		Kit::AddEllipsoid(B, Bob - FVector(0.0, 0.0, 1.2 * Scale), FVector::ForwardVector, FVector::RightVector, FVector::UpVector, FVector(2.6, 2.6, 2.0) * Scale, 10, 5, White);
		TArray<FVector> Path;
		TArray<double> Radii;
		TArray<FLinearColor> Colors;
		Path.Add(Bob - FVector(0.0, 0.0, 3.0 * Scale));
		Path.Add(Bob - FVector(0.0, 0.0, 8.0 * Scale));
		for (int32 k = 0; k <= 6; ++k)
		{
			const double Angle = PI * k / 6.0;
			Path.Add(Bob + FVector(1.8 * Scale * (1.0 - FMath::Cos(Angle)), 0.0, -8.0 * Scale - 1.8 * Scale * FMath::Sin(Angle)));
		}
		for (int32 k = 0; k < Path.Num(); ++k)
		{
			Radii.Add(0.3 * Scale);
			Colors.Add(Steel);
		}
		Kit::AddTube(B, Path, Radii, 5, Colors, FVector::ForwardVector, true);
	}

	void BuildRodItem(FBuffers& B)
	{
		AddRod(B, 38.0, 0.8);
		// Sedal colgando de la punta con el corcho.
		Kit::AddRod(B, FVector(38.0, 0.0, 0.0), FVector(38.0, 0.0, -8.0), 0.15, 4, Tint(0xFFFFFF, 0.2f), FVector::ForwardVector);
		AddHookAndBobber(B, FVector(38.0, 0.0, -10.0), 0.7);
		Center(B);
	}

	void BuildRodPiece(FBuffers& B)
	{
		AddRod(B, 120.0, 2.0);
	}

	void BuildHookPiece(FBuffers& B)
	{
		AddHookAndBobber(B, FVector(0.0, 0.0, 11.0), 1.4);
	}

	void BuildLinePiece(FBuffers& B)
	{
		Kit::AddRod(B, FVector::ZeroVector, FVector(1.0, 0.0, 0.0), 0.45, 4, Tint(0xF4F4F4, 0.3f), FVector::UpVector);
	}

	// ── Iconos ───────────────────────────────────────────────────────────────

	template <typename FSdf>
	void PaintBody(TNHUDArt::FPainter& Painter, const FSdf& Sdf, const FLinearColor& EdgeColor, const FLinearColor& TopColor, const FLinearColor& BottomColor,
		float TopY, float BottomY)
	{
		Painter.Fill([&Sdf](float px, float py) { return Sdf(px, py) - 2.f; }, EdgeColor);
		Painter.Layer(Sdf, [&TopColor, &BottomColor, TopY, BottomY](float, float py)
		{
			return TNHUDArt::Mix(TopColor, BottomColor, (py - TopY) / FMath::Max(1.f, BottomY - TopY));
		});
	}

	UTexture2D* PaintSurfIcon()
	{
		using namespace TNHUDArt;
		FPainter Painter(IconSize, IconSize);
		// Tabla en diagonal sobre una ola.
		const auto Board = [](float px, float py)
		{
			const float Cx = 64.f, Cy = 58.f, Angle = -0.75f;
			const float Lx = (px - Cx) * FMath::Cos(Angle) + (py - Cy) * FMath::Sin(Angle);
			const float Ly = -(px - Cx) * FMath::Sin(Angle) + (py - Cy) * FMath::Cos(Angle);
			return Ellipse(Lx, Ly, 0.f, 0.f, 50.f, 13.f);
		};
		const auto Wave = [](float px, float py)
		{
			const float Crest = 92.f + 9.f * FMath::Sin(px * 0.11f);
			return FMath::Max(Crest - py, Box(px, py, 64.f, 104.f, 58.f, 22.f, 10.f));
		};
		const auto All = [&](float px, float py) { return FMath::Min(Board(px, py), Wave(px, py)); };
		Painter.Sticker(All, 6.f);
		PaintBody(Painter, Board, Hex(0x0E5A54), Hex(0x6FF0E2), Hex(0x1FA597), 14.f, 102.f);
		Painter.Fill([&](float px, float py) { return FMath::Max(Segment(px, py, 36.f, 86.f, 92.f, 30.f, 2.4f), Board(px, py) + 3.f); }, Hex(0xFFCB3D));
		PaintBody(Painter, Wave, Hex(0x0B3B6F), Hex(0x4CC9F0), Hex(0x1F6FB2), 84.f, 124.f);
		for (int32 k = 0; k < 6; ++k)
		{
			const float Fx = 16.f + k * 19.f;
			Painter.Fill([=](float px, float py) { return Circle(px, py, Fx, 90.f + 9.f * FMath::Sin(Fx * 0.11f), 5.5f); }, Hex(0xFFFFFF));
		}
		return Painter.ToTexture(TEXT("TN_Race_TablaSurf"));
	}

	UTexture2D* PaintRodIcon()
	{
		using namespace TNHUDArt;
		FPainter Painter(IconSize, IconSize);
		const auto Rod = [](float px, float py) { return Segment(px, py, 22.f, 112.f, 100.f, 16.f, 3.5f); };
		const auto Grip = [](float px, float py) { return Segment(px, py, 18.f, 117.f, 38.f, 92.f, 6.5f); };
		const auto Reel = [](float px, float py) { return Circle(px, py, 44.f, 98.f, 10.f); };
		const auto Line = [](float px, float py) { return FMath::Abs(Segment(px, py, 100.f, 16.f, 98.f, 70.f, 0.f)) - 1.2f; };
		const auto Bob = [](float px, float py) { return Circle(px, py, 98.f, 80.f, 11.f); };
		const auto All = [&](float px, float py)
		{
			return FMath::Min(FMath::Min(FMath::Min(Rod(px, py), Grip(px, py)), FMath::Min(Reel(px, py), Line(px, py))), Bob(px, py));
		};
		Painter.Sticker(All, 6.f);
		Painter.Fill(Line, Hex(0x3C4457));
		PaintBody(Painter, Rod, Hex(0x1B2B6B), Hex(0x6F8CFF), Hex(0x3A5BD9), 16.f, 112.f);
		PaintBody(Painter, Grip, Hex(0x5A3F0A), Hex(0xE8C690), Hex(0xB08850), 92.f, 117.f);
		PaintBody(Painter, Reel, Hex(0x7A1A12), Hex(0xFF9A88), Hex(0xE04A3A), 88.f, 108.f);
		Painter.Fill([](float px, float py) { return Circle(px, py, 44.f, 98.f, 3.f); }, Hex(0xDDE1EA));
		// Corcho rojo arriba y blanco abajo.
		Painter.Fill([&](float px, float py) { return Bob(px, py) - 2.f; }, Hex(0x3C1010));
		Painter.Fill([&](float px, float py) { return FMath::Max(Bob(px, py), py - 80.f); }, Hex(0xE63946));
		Painter.Fill([&](float px, float py) { return FMath::Max(Bob(px, py), 80.f - py); }, Hex(0xFFFFFF));
		Painter.Fill([](float px, float py) { return Ellipse(px, py, 94.f, 75.f, 3.5f, 2.f); }, Hex(0xFFFFFF, 0.6f));
		return Painter.ToTexture(TEXT("TN_Race_CanaPescar"));
	}

	UTexture2D* PaintWhirlIcon()
	{
		using namespace TNHUDArt;
		FPainter Painter(IconSize, IconSize);
		const auto Pool = [](float px, float py) { return Ellipse(px, py, 64.f, 70.f, 54.f, 40.f); };
		Painter.Sticker(Pool, 6.f);
		Painter.Fill([&](float px, float py) { return Pool(px, py) - 2.f; }, Hex(0x07264A));
		Painter.Layer(Pool, [](float px, float py)
		{
			const float Dx = (px - 64.f) / 54.f;
			const float Dy = (py - 70.f) / 40.f;
			const float R = FMath::Sqrt(Dx * Dx + Dy * Dy);
			const float Angle = FMath::Atan2(Dy, Dx);
			const float Arm = 0.5f + 0.5f * FMath::Sin(3.f * Angle + 11.f * R);
			const FLinearColor Deep = Mix(Hex(0x0B3B6F), Hex(0x1F6FB2), R);
			return Mix(Deep, Hex(0xDFF7FF), Arm * Arm * (0.25f + 0.75f * R));
		});
		Painter.Fill([](float px, float py) { return Ellipse(px, py, 64.f, 72.f, 9.f, 6.f); }, Hex(0x041628));
		Painter.Fill([](float px, float py) { return FMath::Abs(Ellipse(px, py, 64.f, 70.f, 50.f, 36.f)) - 2.f; }, Hex(0xFFFFFF, 0.8f));
		return Painter.ToTexture(TEXT("TN_Race_Remolino"));
	}

	UTexture2D* PaintRocketIcon()
	{
		using namespace TNHUDArt;
		FPainter Painter(IconSize, IconSize);
		// Cohete en diagonal hacia arriba a la derecha, con llama detrás.
		const auto Local = [](float px, float py, float& OutX, float& OutY)
		{
			const float Angle = 0.785f;
			OutX = (px - 60.f) * FMath::Cos(Angle) - (py - 68.f) * FMath::Sin(Angle);
			OutY = (px - 60.f) * FMath::Sin(Angle) + (py - 68.f) * FMath::Cos(Angle);
		};
		const auto Body = [&Local](float px, float py)
		{
			float X, Y;
			Local(px, py, X, Y);
			return Box(X, Y, 0.f, 6.f, 13.f, 30.f, 5.f);
		};
		const auto Nose = [&Local](float px, float py)
		{
			float X, Y;
			Local(px, py, X, Y);
			const TArray<FVector2f> Points = { { -13.f, -22.f }, { 13.f, -22.f }, { 0.f, -48.f } };
			return Polygon(X, Y, Points);
		};
		const auto Flame = [&Local](float px, float py)
		{
			float X, Y;
			Local(px, py, X, Y);
			return Ellipse(X, Y, 0.f, 50.f, 10.f, 18.f);
		};
		const auto All = [&](float px, float py) { return FMath::Min(FMath::Min(Body(px, py), Nose(px, py)), Flame(px, py)); };
		Painter.Sticker(All, 6.f);
		PaintBody(Painter, Flame, Hex(0xB8480A), Hex(0xFFE45C), Hex(0xFF6A1F), 70.f, 120.f);
		PaintBody(Painter, Body, Hex(0x5C0F16), Hex(0xFF8A90), Hex(0xE63946), 30.f, 100.f);
		Painter.Fill([&](float px, float py)
		{
			float X, Y;
			Local(px, py, X, Y);
			return FMath::Max(FMath::Abs(FMath::Fmod(Y + 100.f, 16.f) - 8.f) - 3.f, Body(px, py) + 1.5f);
		}, Hex(0xFFF5DC));
		PaintBody(Painter, Nose, Hex(0x14246B), Hex(0x8FA6FF), Hex(0x3A5BD9), 12.f, 50.f);
		return Painter.ToTexture(TEXT("TN_Race_CoheteFeria"));
	}

	/** Malla de pieza construida una vez y fuera del recolector (se reutiliza en cada uso). */
	UStaticMesh* MakeMesh(TFunctionRef<void(FBuffers&)> Build)
	{
		FBuffers Buffers;
		Build(Buffers);
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
}

// ─────────────────────────────────────────────────────────────────────────────
// API
// ─────────────────────────────────────────────────────────────────────────────

bool TNRaceItemArtExtra::BuildItemMesh(ETNRaceItem Kind, TNProcMesh::FTNProcMeshBuffers& B)
{
	using namespace TNRaceItemArtExtraDetail;
	switch (Kind)
	{
		case ETNRaceItem::TablaSurf:
			BuildSurfItem(B);
			return true;
		case ETNRaceItem::CanaPescar:
			BuildRodItem(B);
			return true;
		case ETNRaceItem::Remolino:
			BuildWhirlItem(B);
			return true;
		case ETNRaceItem::CoheteFeria:
			BuildRocketItem(B);
			return true;
		default:
			return false;
	}
}

UTexture2D* TNRaceItemArtExtra::PaintIcon(ETNRaceItem Kind)
{
	using namespace TNRaceItemArtExtraDetail;
	switch (Kind)
	{
		case ETNRaceItem::TablaSurf:
			return PaintSurfIcon();
		case ETNRaceItem::CanaPescar:
			return PaintRodIcon();
		case ETNRaceItem::Remolino:
			return PaintWhirlIcon();
		case ETNRaceItem::CoheteFeria:
			return PaintRocketIcon();
		default:
			return nullptr;
	}
}

UStaticMesh* TNRaceItemArtExtra::GetRidePiece(ERidePiece Piece)
{
	using namespace TNRaceItemArtExtraDetail;
	if (IsHeadless())
	{
		return nullptr;
	}
	static TMap<uint8, UStaticMesh*> Cache;
	const uint8 Key = static_cast<uint8>(Piece);
	if (UStaticMesh** Found = Cache.Find(Key))
	{
		return *Found;
	}
	UStaticMesh* Mesh = nullptr;
	switch (Piece)
	{
		case ERidePiece::SurfBoard:  Mesh = MakeMesh([](FBuffers& B) { BuildSurfBoardPiece(B); }); break;
		case ERidePiece::Wave:       Mesh = MakeMesh([](FBuffers& B) { BuildWavePiece(B); }); break;
		case ERidePiece::Rocket:     Mesh = MakeMesh([](FBuffers& B) { BuildRocketPiece(B); }); break;
		case ERidePiece::Flame:      Mesh = MakeMesh([](FBuffers& B) { BuildFlamePiece(B); }); break;
		case ERidePiece::WhirlWater: Mesh = MakeMesh([](FBuffers& B) { BuildWhirlWaterPiece(B); }); break;
		case ERidePiece::WhirlFoam:  Mesh = MakeMesh([](FBuffers& B) { BuildWhirlFoamPiece(B); }); break;
		case ERidePiece::FishLine:   Mesh = MakeMesh([](FBuffers& B) { BuildLinePiece(B); }); break;
		case ERidePiece::Hook:       Mesh = MakeMesh([](FBuffers& B) { BuildHookPiece(B); }); break;
		case ERidePiece::Rod:        Mesh = MakeMesh([](FBuffers& B) { BuildRodPiece(B); }); break;
		default: break;
	}
	Cache.Add(Key, Mesh);
	return Mesh;
}
