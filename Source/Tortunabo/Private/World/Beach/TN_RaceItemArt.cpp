#include "TN_RaceItemArt.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Misc/App.h"
#include "UObject/Package.h"
#include "../../Lobby/Playground/TN_PlaygroundMeshKit.h"
#include "../../UI/HUD/TN_HUDArt.h"

// ─────────────────────────────────────────────────────────────────────────────
// Arte de los objetos de carrera dibujado en código: mallas de juguete (kit del parque de pruebas del lobby, con el brillo
// en el alfa del color de vértice) e iconos estilo pegatina (pintor de distancias con signo del HUD).
// ─────────────────────────────────────────────────────────────────────────────

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto del
// bloque.
namespace TNRaceItemArtDetail
{
	using FBuffers = TNPlaygroundKit::FBuffers;
	namespace Kit = TNPlaygroundKit;

	// ── Medidas (cm) de lo que se ajusta ───────────────────────────────────────────────────────────────────────────────
	/** Semilado de la caja de objetos (36 cm; con la «?» en relieve, unos 39) y ancho del bisel de sus aristas. */
	constexpr double BoxHalf = 18.0;
	constexpr double BoxBevel = 2.5;
	/** Coco turbo suelto: radio. */
	constexpr double CoconutRadius = 13.0;
	/** Cocos del triple: radio y distancia entre centros (se pegan un poco). */
	constexpr double ClusterRadius = 10.0;
	constexpr double ClusterSpacing = 17.0;
	/** Lado mayor (cm) al que se ajustan las mallas hechas en unidades de diseño. */
	constexpr double PelicanSize = 35.0;
	constexpr double CrabSize = 32.0;
	constexpr double GullSize = 38.0;
	constexpr double CloudSize = 38.0;
	/** Inclinación (grados de cabeceo) con la que el disco se pone de canto en la mano. */
	constexpr double FrisbeeHandPitch = 55.0;
	/** Desplazamiento vertical del dibujo de la «?» para que quede centrada en la cara (unidades del diseño). */
	constexpr double QuestionShift = 1.7;

	// ─────────────────────────────────────────────────────────────────────────
	// Ayudas de malla
	// ─────────────────────────────────────────────────────────────────────────

	/** Color de la paleta (0xRRGGBB, sRGB) con brillo en el alfa. */
	FLinearColor Tint(uint32 Rgb24, float Shine = 0.f)
	{
		return Kit::Rgb(Rgb24, Shine);
	}

	/** Vector unitario perpendicular a Dir. */
	FVector PerpTo(const FVector& Dir)
	{
		return FVector::CrossProduct(Dir, FMath::Abs(Dir.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
	}

	/** Caja orientada por tres ejes unitarios perpendiculares (Half en esos ejes). */
	void AddFrameBox(FBuffers& B, const FVector& Center, const FVector& AxisX, const FVector& AxisY, const FVector& AxisZ, const FVector& Half,
		const FLinearColor& Color)
	{
		const auto Corner = [&Center, &AxisX, &AxisY, &AxisZ, &Half](double Sx, double Sy, double Sz)
		{
			return Center + AxisX * (Sx * Half.X) + AxisY * (Sy * Half.Y) + AxisZ * (Sz * Half.Z);
		};
		B.AddQuad(Corner(-1, -1, 1), Corner(1, -1, 1), Corner(1, 1, 1), Corner(-1, 1, 1), AxisZ, Color);
		B.AddQuad(Corner(-1, -1, -1), Corner(-1, 1, -1), Corner(1, 1, -1), Corner(1, -1, -1), -AxisZ, Kit::Shade(Color, 0.8));
		B.AddQuad(Corner(1, -1, -1), Corner(1, 1, -1), Corner(1, 1, 1), Corner(1, -1, 1), AxisX, Kit::Shade(Color, 0.92));
		B.AddQuad(Corner(-1, -1, -1), Corner(-1, -1, 1), Corner(-1, 1, 1), Corner(-1, 1, -1), -AxisX, Kit::Shade(Color, 0.92));
		B.AddQuad(Corner(-1, 1, -1), Corner(-1, 1, 1), Corner(1, 1, 1), Corner(1, 1, -1), AxisY, Kit::Shade(Color, 0.96));
		B.AddQuad(Corner(-1, -1, -1), Corner(1, -1, -1), Corner(1, -1, 1), Corner(-1, -1, 1), -AxisY, Kit::Shade(Color, 0.96));
	}

	/** Elipsoide con los ejes del mundo. */
	void AddBlob(FBuffers& B, const FVector& Center, const FVector& Radii, const FLinearColor& Color, int32 Seg = 12)
	{
		Kit::AddEllipsoid(B, Center, FVector::ForwardVector, FVector::RightVector, FVector::UpVector, Radii, Seg, FMath::Max(4, Seg / 2), Color);
	}

	/** Elipsoide inclinado alrededor de X (alas): la punta de +Y·Side sube TiltDeg grados. */
	void AddWingBlob(FBuffers& B, const FVector& Center, const FVector& Radii, double TiltDeg, double Side, const FLinearColor& Color, int32 Seg = 12)
	{
		const double Tilt = FMath::DegreesToRadians(TiltDeg);
		const FVector AxisY(0.0, Side * FMath::Cos(Tilt), FMath::Sin(Tilt));
		const FVector AxisZ(0.0, -Side * FMath::Sin(Tilt), FMath::Cos(Tilt));
		Kit::AddEllipsoid(B, Center, FVector::ForwardVector, AxisY, AxisZ, Radii, Seg, FMath::Max(4, Seg / 2), Color);
	}

	/** Elipsoide inclinado alrededor de Y: con PitchDeg > 0 la parte de +X baja (picos, cuerpos que miran hacia abajo). */
	void AddPitchBlob(FBuffers& B, const FVector& Center, const FVector& Radii, double PitchDeg, const FLinearColor& Color, int32 Seg = 12)
	{
		const double Pitch = FMath::DegreesToRadians(PitchDeg);
		const FVector AxisX(FMath::Cos(Pitch), 0.0, -FMath::Sin(Pitch));
		const FVector AxisZ(FMath::Sin(Pitch), 0.0, FMath::Cos(Pitch));
		Kit::AddEllipsoid(B, Center, AxisX, FVector::RightVector, AxisZ, Radii, Seg, FMath::Max(4, Seg / 2), Color);
	}

	/** Anilla (toro) de radio Major y grosor Minor en el plano de AxisU/AxisV. */
	void AddRing(FBuffers& B, const FVector& Center, const FVector& AxisU, const FVector& AxisV, double Major, double Minor, int32 Segments,
		const FLinearColor& Color)
	{
		TArray<FVector> Path;
		for (int32 Index = 0; Index <= Segments; ++Index)
		{
			const double Angle = Kit::KitTwoPi * (Index % Segments) / Segments;
			Path.Add(Center + (AxisU * FMath::Cos(Angle) + AxisV * FMath::Sin(Angle)) * Major);
		}
		const TArray<double> Radii = { Minor };
		const TArray<FLinearColor> Colors = { Color };
		Kit::AddTube(B, Path, Radii, 6, Colors, FVector::CrossProduct(AxisU, AxisV).GetSafeNormal(), false);
	}

	/** Cilindro de sección ovalada y eje vertical: de Base a Base + Height, con los semiejes de abajo (0) y de arriba (1). */
	void AddOvalFrustum(FBuffers& B, const FVector& Base, double Height, double Rx0, double Ry0, double Rx1, double Ry1, int32 Seg,
		const FLinearColor& Color, const FLinearColor& CapColor, bool bCapBottom, bool bCapTop)
	{
		if (Height < 0.01 || Seg < 3)
		{
			return;
		}
		const double Slope = ((Rx0 + Ry0) - (Rx1 + Ry1)) * 0.5 / Height;
		for (int32 k = 0; k < Seg; ++k)
		{
			const double A0 = Kit::KitTwoPi * k / Seg;
			const double A1 = Kit::KitTwoPi * (k + 1) / Seg;
			const double C0 = FMath::Cos(A0), S0 = FMath::Sin(A0), C1 = FMath::Cos(A1), S1 = FMath::Sin(A1);
			const FVector P[4] = { Base + FVector(Rx0 * C0, Ry0 * S0, 0.0), Base + FVector(Rx0 * C1, Ry0 * S1, 0.0),
				Base + FVector(Rx1 * C1, Ry1 * S1, Height), Base + FVector(Rx1 * C0, Ry1 * S0, Height) };
			const FVector N0 = (FVector(C0 / Rx0, S0 / Ry0, 0.0).GetSafeNormal() + FVector(0.0, 0.0, Slope)).GetSafeNormal();
			const FVector N1 = (FVector(C1 / Rx0, S1 / Ry0, 0.0).GetSafeNormal() + FVector(0.0, 0.0, Slope)).GetSafeNormal();
			const FVector N[4] = { N0, N1, N1, N0 };
			Kit::SmoothQuad1(B, P, N, Color);
			if (bCapBottom)
			{
				Kit::SmoothTri(B, Base, P[0], P[1], -FVector::UpVector, -FVector::UpVector, -FVector::UpVector, CapColor);
			}
			if (bCapTop)
			{
				Kit::SmoothTri(B, Base + FVector(0.0, 0.0, Height), P[3], P[2], FVector::UpVector, FVector::UpVector, FVector::UpVector, CapColor);
			}
		}
	}

	/** Banda de revolución (eje Z) entre (R0, Z0) y (R1, Z1), con la cara hacia arriba: discos abombados. */
	void AddDishBand(FBuffers& B, const FVector& Center, double R0, double Z0, double R1, double Z1, int32 Seg, const FLinearColor& Color)
	{
		const double Width = FMath::Max(0.01, R1 - R0);
		const double Lean = (Z0 - Z1) / Width;
		for (int32 k = 0; k < Seg; ++k)
		{
			const double A0 = Kit::KitTwoPi * k / Seg;
			const double A1 = Kit::KitTwoPi * (k + 1) / Seg;
			const double C0 = FMath::Cos(A0), S0 = FMath::Sin(A0), C1 = FMath::Cos(A1), S1 = FMath::Sin(A1);
			const FVector P[4] = { Center + FVector(R0 * C0, R0 * S0, Z0), Center + FVector(R0 * C1, R0 * S1, Z0), Center + FVector(R1 * C1, R1 * S1, Z1),
				Center + FVector(R1 * C0, R1 * S0, Z1) };
			const FVector N0 = FVector(C0 * Lean, S0 * Lean, 1.0).GetSafeNormal();
			const FVector N1 = FVector(C1 * Lean, S1 * Lean, 1.0).GetSafeNormal();
			const FVector N[4] = { N0, N1, N1, N0 };
			Kit::SmoothQuad1(B, P, N, Color);
		}
	}

	/** Estrella plana de cuatro puntas (de dos caras) en el plano de AxisU/AxisV; Inner es la fracción del radio de las muescas. */
	void AddStar4(FBuffers& B, const FVector& Center, const FVector& AxisU, const FVector& AxisV, double Radius, double Inner, const FLinearColor& Color)
	{
		const FVector Face = FVector::CrossProduct(AxisU, AxisV).GetSafeNormal();
		for (int32 k = 0; k < 4; ++k)
		{
			const double A0 = Kit::KitPi * 0.5 * k;
			const double AMid = A0 + Kit::KitPi * 0.25;
			const double A1 = A0 + Kit::KitPi * 0.5;
			const FVector Tip0 = Center + (AxisU * FMath::Cos(A0) + AxisV * FMath::Sin(A0)) * Radius;
			const FVector Notch = Center + (AxisU * FMath::Cos(AMid) + AxisV * FMath::Sin(AMid)) * (Radius * Inner);
			const FVector Tip1 = Center + (AxisU * FMath::Cos(A1) + AxisV * FMath::Sin(A1)) * Radius;
			B.AddTri(Center, Tip0, Notch, Face, Color);
			B.AddTri(Center, Tip0, Notch, -Face, Color);
			B.AddTri(Center, Notch, Tip1, Face, Color);
			B.AddTri(Center, Notch, Tip1, -Face, Color);
		}
	}

	/** Destello: dos estrellas de cuatro puntas cruzadas en vertical (se ve desde cualquier lado). */
	void AddSparkle(FBuffers& B, const FVector& Center, double Radius, const FLinearColor& Color)
	{
		AddStar4(B, Center, FVector::ForwardVector, FVector::UpVector, Radius, 0.2, Color);
		AddStar4(B, Center, FVector::RightVector, FVector::UpVector, Radius, 0.2, Color);
	}

	/** Varilla a rayas: trozos de Stripe cm que alternan ColorA y ColorB; devuelve el índice de la siguiente raya. */
	int32 AddStripedRod(FBuffers& B, const FVector& From, const FVector& To, double Radius, double Stripe, const FLinearColor& ColorA,
		const FLinearColor& ColorB, int32 FirstIndex)
	{
		const double Length = FVector::Dist(From, To);
		const int32 Pieces = FMath::Max(1, FMath::CeilToInt(static_cast<float>(Length / FMath::Max(0.1, Stripe))));
		const FVector Ref = PerpTo((To - From).GetSafeNormal());
		for (int32 Piece = 0; Piece < Pieces; ++Piece)
		{
			const double T0 = static_cast<double>(Piece) / Pieces;
			const double T1 = static_cast<double>(Piece + 1) / Pieces;
			Kit::AddRod(B, From + (To - From) * T0, From + (To - From) * T1, Radius, 6, ((FirstIndex + Piece) % 2) == 0 ? ColorA : ColorB, Ref);
		}
		return FirstIndex + Pieces;
	}

	/** Mueve la malla para que el centro de su caja esté en el origen y, con MaxSize > 0, la escala hasta que su lado mayor mida MaxSize. */
	void FitAndCenter(FBuffers& B, double MaxSize)
	{
		if (B.Verts.Num() == 0)
		{
			return;
		}
		FVector Lo(DBL_MAX);
		FVector Hi(-DBL_MAX);
		for (const FVector& Vert : B.Verts)
		{
			Lo.X = FMath::Min(Lo.X, Vert.X);
			Lo.Y = FMath::Min(Lo.Y, Vert.Y);
			Lo.Z = FMath::Min(Lo.Z, Vert.Z);
			Hi.X = FMath::Max(Hi.X, Vert.X);
			Hi.Y = FMath::Max(Hi.Y, Vert.Y);
			Hi.Z = FMath::Max(Hi.Z, Vert.Z);
		}
		const FVector Size = Hi - Lo;
		const double Longest = FMath::Max(Size.X, FMath::Max(Size.Y, Size.Z));
		const double Factor = (MaxSize > 0.0 && Longest > 0.01) ? MaxSize / Longest : 1.0;
		const FVector Mid = (Lo + Hi) * 0.5;
		for (FVector& Vert : B.Verts)
		{
			Vert = (Vert - Mid) * Factor;
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Caja de objetos: cubo de juguete con una «?»
	// ─────────────────────────────────────────────────────────────────────────

	/** Color de la cara de un eje (0 X, 1 Y, 2 Z) y signo: los siete colores de juguete, uno por cara. */
	FLinearColor BoxFaceColor(int32 Axis, int32 Sign)
	{
		return Kit::ToyColor(Axis * 2 + (Sign > 0 ? 0 : 1), 0.25f);
	}

	/**
	 * «?» de varillas pegada a una cara: blanca con un contorno oscuro debajo. Origin es el centro de la cara, Up hacia donde
	 * apunta la parte de arriba de la marca y Out la normal de la cara. La derecha de quien mira es Out x Up.
	 */
	void AddQuestionMark(FBuffers& B, const FVector& Origin, const FVector& Up, const FVector& Out, double Scale)
	{
		const FVector Right = FVector::CrossProduct(Out, Up).GetSafeNormal();
		// Recorrido de la marca en (u, v) con v hacia arriba: el gancho de arriba, la curva hacia el centro y el palo.
		TArray<FVector2D> Line;
		for (int32 Index = 0; Index <= 8; ++Index)
		{
			const double Angle = FMath::DegreesToRadians(165.0 - 210.0 * Index / 8.0);
			Line.Add(FVector2D(4.8 * FMath::Cos(Angle), 5.5 + 4.8 * FMath::Sin(Angle)));
		}
		Line.Add(FVector2D(2.0, 0.6));
		Line.Add(FVector2D(0.4, -0.7));
		Line.Add(FVector2D(0.0, -2.6));
		const auto ToWorld = [&Origin, &Up, &Right, &Out, Scale](double U, double V, double Lift)
		{
			return Origin + Right * (U * Scale) + Up * ((V - QuestionShift) * Scale) + Out * (Lift * Scale);
		};
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			const bool bOutline = Pass == 0;
			const double Radius = (bOutline ? 1.3 : 0.9) * Scale;
			const double Lift = bOutline ? 0.0 : 0.5;
			const FLinearColor Ink = bOutline ? Tint(0x1B2A4A) : Tint(0xFFFFFF, 0.4f);
			TArray<FVector> Path;
			for (const FVector2D& Point : Line)
			{
				Path.Add(ToWorld(Point.X, Point.Y, Lift));
			}
			const TArray<double> Radii = { Radius };
			const TArray<FLinearColor> Colors = { Ink };
			Kit::AddTube(B, Path, Radii, 6, Colors, Out, true);
			Kit::AddBall(B, ToWorld(0.0, -6.6, Lift), Radius * 1.15, 8, Ink);
		}
	}

	void BuildBox(FBuffers& B)
	{
		constexpr double Flat = BoxHalf - BoxBevel;
		const FVector Axes[3] = { FVector::ForwardVector, FVector::RightVector, FVector::UpVector };
		const FLinearColor Frame = Tint(0xFFFFFF, 0.55f);
		// Las seis caras, un poco metidas para dejar sitio a los biseles.
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			for (int32 Sign = -1; Sign <= 1; Sign += 2)
			{
				const FVector Normal = Axes[Axis] * static_cast<double>(Sign);
				const FVector AxisU = Axes[(Axis + 1) % 3];
				const FVector AxisV = Axes[(Axis + 2) % 3];
				const FVector Mid = Normal * BoxHalf;
				B.AddQuad(Mid - AxisU * Flat - AxisV * Flat, Mid + AxisU * Flat - AxisV * Flat, Mid + AxisU * Flat + AxisV * Flat,
					Mid - AxisU * Flat + AxisV * Flat, Normal, BoxFaceColor(Axis, Sign));
			}
		}
		// Los doce biseles: mezcla de los colores de las dos caras vecinas, aclarada.
		for (int32 AxisA = 0; AxisA < 3; ++AxisA)
		{
			const int32 AxisB = (AxisA + 1) % 3;
			const int32 AxisC = (AxisA + 2) % 3;
			for (int32 SignA = -1; SignA <= 1; SignA += 2)
			{
				for (int32 SignB = -1; SignB <= 1; SignB += 2)
				{
					const double Sa = static_cast<double>(SignA);
					const double Sb = static_cast<double>(SignB);
					const FVector Ea = Axes[AxisA] * Sa;
					const FVector Eb = Axes[AxisB] * Sb;
					const FVector Ec = Axes[AxisC];
					const FVector P1 = Ea * BoxHalf + Eb * Flat - Ec * Flat;
					const FVector P2 = Ea * BoxHalf + Eb * Flat + Ec * Flat;
					const FVector P3 = Ea * Flat + Eb * BoxHalf + Ec * Flat;
					const FVector P4 = Ea * Flat + Eb * BoxHalf - Ec * Flat;
					const FLinearColor Blend = Kit::Mix(Kit::Mix(BoxFaceColor(AxisA, SignA), BoxFaceColor(AxisB, SignB), 0.5), Frame, 0.4);
					B.AddQuad(P1, P2, P3, P4, Ea + Eb, Blend);
				}
			}
		}
		// Las ocho esquinas, blancas y brillantes.
		for (int32 SignX = -1; SignX <= 1; SignX += 2)
		{
			for (int32 SignY = -1; SignY <= 1; SignY += 2)
			{
				for (int32 SignZ = -1; SignZ <= 1; SignZ += 2)
				{
					const double Sx = static_cast<double>(SignX);
					const double Sy = static_cast<double>(SignY);
					const double Sz = static_cast<double>(SignZ);
					B.AddTri(FVector(Sx * BoxHalf, Sy * Flat, Sz * Flat), FVector(Sx * Flat, Sy * BoxHalf, Sz * Flat), FVector(Sx * Flat, Sy * Flat, Sz * BoxHalf),
						FVector(Sx, Sy, Sz), Frame);
				}
			}
		}
		// La «?» en las cuatro caras de los lados, arriba y abajo (así la caja queda centrada en su malla).
		AddQuestionMark(B, FVector(BoxHalf, 0.0, 0.0), FVector::UpVector, FVector::ForwardVector, 1.25);
		AddQuestionMark(B, FVector(-BoxHalf, 0.0, 0.0), FVector::UpVector, -FVector::ForwardVector, 1.25);
		AddQuestionMark(B, FVector(0.0, BoxHalf, 0.0), FVector::UpVector, FVector::RightVector, 1.25);
		AddQuestionMark(B, FVector(0.0, -BoxHalf, 0.0), FVector::UpVector, -FVector::RightVector, 1.25);
		AddQuestionMark(B, FVector(0.0, 0.0, BoxHalf), FVector::ForwardVector, FVector::UpVector, 1.25);
		AddQuestionMark(B, FVector(0.0, 0.0, -BoxHalf), FVector::ForwardVector, -FVector::UpVector, 1.25);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Cocos: turbo, triple (3, 2, 1) y dorado
	// ─────────────────────────────────────────────────────────────────────────

	/** Colores de un coco. */
	struct FCoconutStyle
	{
		FLinearColor Body;
		FLinearColor FurA;
		FLinearColor FurB;
		FLinearColor Sticker;
		FLinearColor Bolt;
		FLinearColor StrawA;
		FLinearColor StrawB;
		FLinearColor StrawCap;
	};

	FCoconutStyle MakeCoconutStyle(bool bGolden)
	{
		FCoconutStyle Style;
		if (bGolden)
		{
			Style.Body = Tint(0xFFBE1E, 1.f);
			Style.FurA = Tint(0xE8A010, 0.9f);
			Style.FurB = Tint(0xFFE066, 1.f);
			Style.Sticker = Tint(0xFFF6D8, 1.f);
			Style.Bolt = Tint(0xE8590C, 0.8f);
			Style.StrawA = Tint(0xFFD23F, 1.f);
			Style.StrawB = Tint(0xFFFFFF, 1.f);
			Style.StrawCap = Tint(0xC98A00, 0.8f);
		}
		else
		{
			Style.Body = Tint(0x6E4526);
			Style.FurA = Tint(0x5A361B);
			Style.FurB = Tint(0x8B5E34);
			Style.Sticker = Tint(0xFFD23F, 0.35f);
			Style.Bolt = Tint(0x22263A, 0.2f);
			Style.StrawA = Tint(0x2EC46A, 0.4f);
			Style.StrawB = Tint(0xFFFFFF, 0.4f);
			Style.StrawCap = Tint(0x1B7A45);
		}
		return Style;
	}

	/**
	 * Un coco peludo de radio Radius con el centro en Center: la esfera, los mechones (conos de punta fina repartidos por la
	 * esfera), una pajita rayada verde y blanca con codo por arriba (hacia StrawDir) y una pegatina amarilla con un rayo
	 * mirando a StickerDir.
	 */
	void AddCoconut(FBuffers& B, const FVector& Center, double Radius, const FVector& StickerDir, const FVector& StrawDir, const FCoconutStyle& Style,
		bool bStraw, bool bSticker)
	{
		const FVector Up = StrawDir.GetSafeNormal();
		FVector Fwd = StickerDir - Up * FVector::DotProduct(StickerDir, Up);
		if (!Fwd.Normalize())
		{
			Fwd = PerpTo(Up);
		}
		const FVector Side = FVector::CrossProduct(Up, Fwd).GetSafeNormal();

		Kit::AddBall(B, Center, Radius, 14, Style.Body);

		// Mechones: repartidos por la esfera (espiral áurea), inclinados en remolino, sin tapar la pajita ni la pegatina.
		constexpr int32 Tufts = 130;
		const double GoldenAngle = Kit::KitPi * (3.0 - FMath::Sqrt(5.0));
		for (int32 Index = 0; Index < Tufts; ++Index)
		{
			const double Height = 1.0 - 2.0 * (Index + 0.5) / Tufts;
			const double Ring = FMath::Sqrt(FMath::Max(0.0, 1.0 - Height * Height));
			const double Angle = GoldenAngle * Index;
			const FVector Dir = Up * Height + Fwd * (FMath::Cos(Angle) * Ring) + Side * (FMath::Sin(Angle) * Ring);
			if ((bStraw && FVector::DotProduct(Dir, Up) > 0.88) || (bSticker && FVector::DotProduct(Dir, Fwd) > 0.84))
			{
				continue;
			}
			const FVector Swirl = FVector::CrossProduct(Dir, Up).GetSafeNormal();
			const FVector TipDir = (Dir + Swirl * 0.4).GetSafeNormal();
			const FVector Root = Center + Dir * (Radius * 0.95);
			const FVector Tip = Root + TipDir * (Radius * (0.17 + 0.07 * Kit::Hash01(Index, 5, 29u)));
			const FLinearColor Fur = Kit::Shade((Index % 3) == 0 ? Style.FurB : Style.FurA, 0.8 + 0.4 * Kit::Hash01(Index, 3, 17u));
			Kit::AddFrustum(B, Root, Tip, Radius * 0.07, Radius * 0.008, 4, Fur, Fur, false, false);
		}

		if (bSticker)
		{
			// Pegatina: una moneda gruesa hundida en la esfera, con el rayo encima.
			const FVector Vertical = Up;
			const FVector Across = FVector::CrossProduct(Fwd, Vertical).GetSafeNormal();
			Kit::AddFrustum(B, Center + Fwd * (Radius * 0.85), Center + Fwd * (Radius * 1.04), Radius * 0.47, Radius * 0.44, 16, Style.Sticker, Style.Sticker,
				false, true);
			const double Unit = Radius * 0.075;
			const FVector BoltCenter = Center + Fwd * (Radius * 1.05);
			const auto BoltPoint = [&BoltCenter, &Across, &Vertical, Unit](double U, double V) { return BoltCenter + Across * (U * Unit) + Vertical * (V * Unit); };
			const FVector V1 = BoltPoint(0.6, 4.6);
			const FVector V2 = BoltPoint(-2.6, -0.4);
			const FVector V3 = BoltPoint(-0.2, -0.4);
			const FVector V4 = BoltPoint(-1.2, -4.6);
			const FVector V5 = BoltPoint(2.6, 0.8);
			const FVector V6 = BoltPoint(0.2, 0.8);
			const FVector V7 = BoltPoint(1.6, 4.6);
			B.AddTri(V1, V2, V6, Fwd, Style.Bolt);
			B.AddTri(V1, V6, V7, Fwd, Style.Bolt);
			B.AddTri(V2, V3, V6, Fwd, Style.Bolt);
			B.AddTri(V3, V5, V6, Fwd, Style.Bolt);
			B.AddTri(V3, V4, V5, Fwd, Style.Bolt);
		}

		if (bStraw)
		{
			// Pajita: un tramo casi vertical, un codo y otro tramo inclinado hacia delante; termina en una boca oscura.
			const double StrawRadius = Radius * 0.085;
			const FVector StrawBase = Center + Up * (Radius * 0.72) - Fwd * (Radius * 0.05);
			const FVector Elbow = Center + Up * (Radius * 1.62) - Fwd * (Radius * 0.16);
			const FVector StrawTip = Elbow + Fwd * (Radius * 0.34) + Up * (Radius * 0.22);
			int32 Stripe = AddStripedRod(B, StrawBase, Elbow, StrawRadius, Radius * 0.16, Style.StrawA, Style.StrawB, 0);
			Kit::AddBall(B, Elbow, StrawRadius, 6, ((Stripe % 2) == 0) ? Style.StrawA : Style.StrawB);
			Stripe = AddStripedRod(B, Elbow, StrawTip, StrawRadius, Radius * 0.16, Style.StrawA, Style.StrawB, Stripe);
			Kit::AddDisc(B, StrawTip, (StrawTip - Elbow).GetSafeNormal(), StrawRadius * 0.85, 6, Style.StrawCap);
		}
	}

	/** Un coco turbo suelto. */
	void BuildCoconut(FBuffers& B)
	{
		AddCoconut(B, FVector::ZeroVector, CoconutRadius, FVector::ForwardVector, FVector::UpVector, MakeCoconutStyle(false), true, true);
		FitAndCenter(B, 0.0);
	}

	/** El triple coco con Count cocos (1, 2 o 3): un racimo en el suelo, en triángulo. */
	void BuildCoconutCluster(FBuffers& B, int32 Count)
	{
		const FCoconutStyle Style = MakeCoconutStyle(false);
		TArray<FVector> Spots;
		TArray<double> Facing;
		if (Count >= 3)
		{
			Spots.Add(FVector(ClusterSpacing * 0.5774, 0.0, 0.0));
			Spots.Add(FVector(-ClusterSpacing * 0.2887, ClusterSpacing * 0.5, 0.0));
			Spots.Add(FVector(-ClusterSpacing * 0.2887, -ClusterSpacing * 0.5, 0.0));
			Facing.Add(0.0);
			Facing.Add(120.0);
			Facing.Add(-120.0);
		}
		else if (Count == 2)
		{
			Spots.Add(FVector(0.0, ClusterSpacing * 0.5, 0.0));
			Spots.Add(FVector(0.0, -ClusterSpacing * 0.5, 0.0));
			Facing.Add(40.0);
			Facing.Add(-40.0);
		}
		else
		{
			Spots.Add(FVector::ZeroVector);
			Facing.Add(0.0);
		}
		for (int32 Index = 0; Index < Spots.Num(); ++Index)
		{
			const double Angle = FMath::DegreesToRadians(Facing[Index]);
			AddCoconut(B, Spots[Index], ClusterRadius, FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0), FVector::UpVector, Style, true, true);
		}
		FitAndCenter(B, 0.0);
	}

	/** El coco dorado: todo brillo, con destellos de estrella de cuatro puntas alrededor. */
	void BuildGoldenCoconut(FBuffers& B)
	{
		AddCoconut(B, FVector::ZeroVector, CoconutRadius, FVector::ForwardVector, FVector::UpVector, MakeCoconutStyle(true), true, true);
		const FLinearColor Glint = Tint(0xFFFFFF, 1.f);
		AddSparkle(B, FVector(9.0, -15.0, 15.0), 5.5, Glint);
		AddSparkle(B, FVector(-13.0, 13.0, 11.0), 3.8, Tint(0xFFF3B0, 1.f));
		AddSparkle(B, FVector(15.0, 12.0, -7.0), 3.2, Glint);
		FitAndCenter(B, 0.0);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Pelícano taxi
	// ─────────────────────────────────────────────────────────────────────────

	void BuildPelican(FBuffers& B)
	{
		const FLinearColor White = Tint(0xF7F5EE, 0.12f);
		const FLinearColor Shadow = Tint(0xE3DFD2, 0.08f);
		const FLinearColor WingGray = Tint(0xC4CAD6, 0.08f);
		const FLinearColor WingDark = Tint(0x464D5E, 0.1f);
		const FLinearColor Orange = Tint(0xFF9F1C, 0.25f);
		const FLinearColor Hook = Tint(0xE8590C, 0.25f);
		const FLinearColor Pouch = Tint(0xFFD166, 0.3f);
		const FLinearColor Sign = Tint(0xFFD23F, 0.3f);
		const FLinearColor Black = Tint(0x1A1E28, 0.1f);

		// Cuerpo, pecho y cola.
		AddBlob(B, FVector(0.0, 0.0, 0.0), FVector(12.0, 7.5, 7.0), White, 14);
		AddBlob(B, FVector(6.0, 0.0, 0.5), FVector(7.0, 6.2, 6.2), White, 12);
		AddPitchBlob(B, FVector(-13.5, 0.0, 1.8), FVector(6.5, 3.8, 1.6), 10.0, Shadow, 10);
		// Alas plegadas a los lados, con la punta oscura.
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const double Side = Index == 0 ? 1.0 : -1.0;
			AddBlob(B, FVector(-1.5, Side * 6.4, 1.2), FVector(9.0, 1.9, 4.8), WingGray, 12);
			AddBlob(B, FVector(-9.0, Side * 6.2, 0.2), FVector(4.4, 1.6, 2.6), WingDark, 10);
		}
		// Patas palmeadas.
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const double Side = Index == 0 ? 1.0 : -1.0;
			Kit::AddRod(B, FVector(1.0, Side * 3.2, -5.5), FVector(1.5, Side * 3.2, -10.2), 0.95, 6, Orange, FVector::ForwardVector);
			AddBlob(B, FVector(3.4, Side * 3.2, -10.7), FVector(3.4, 2.1, 0.5), Orange, 8);
		}
		// Cuello en S y cabeza.
		TArray<FVector> Neck;
		Neck.Add(FVector(7.0, 0.0, 3.0));
		Neck.Add(FVector(10.5, 0.0, 6.5));
		Neck.Add(FVector(11.0, 0.0, 10.5));
		Neck.Add(FVector(12.5, 0.0, 14.0));
		Neck.Add(FVector(14.0, 0.0, 16.5));
		const TArray<double> NeckRadii = { 3.6, 3.1, 2.7, 2.5, 2.5 };
		const TArray<FLinearColor> NeckColors = { White };
		Kit::AddTube(B, Neck, NeckRadii, 10, NeckColors, FVector::RightVector, false);
		Kit::AddBall(B, FVector(14.6, 0.0, 17.6), 4.0, 12, White);
		// Ojos.
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const double Side = Index == 0 ? 1.0 : -1.0;
			Kit::AddBall(B, FVector(15.7, Side * 3.0, 18.6), 1.05, 8, Black);
			Kit::AddBall(B, FVector(16.3, Side * 3.2, 19.1), 0.38, 6, Tint(0xFFFFFF, 0.6f));
		}
		// Pico largo con bolsa y gancho.
		AddPitchBlob(B, FVector(23.2, 0.0, 17.2), FVector(8.4, 2.5, 1.4), 3.0, Orange, 12);
		Kit::AddBall(B, FVector(31.2, 0.0, 16.5), 1.05, 8, Hook);
		AddPitchBlob(B, FVector(22.4, 0.0, 15.1), FVector(7.8, 2.6, 2.1), -2.0, Pouch, 12);
		AddBlob(B, FVector(21.0, 0.0, 13.9), FVector(5.4, 2.3, 2.0), Pouch, 10);
		// Cartelito de taxi en la cabeza: poste, caja amarilla y damero.
		const FVector SignCenter(14.6, 0.0, 24.3);
		Kit::AddAxisBox(B, FVector(14.6, 0.0, 21.9), FVector(1.3, 0.8, 0.9), Tint(0x333B4A, 0.3f));
		Kit::AddAxisBox(B, SignCenter, FVector(3.6, 1.7, 1.5), Sign);
		for (int32 Column = 0; Column < 4; ++Column)
		{
			for (int32 Row = 0; Row < 2; ++Row)
			{
				if (((Column + Row) % 2) != 0)
				{
					continue;
				}
				const double Cx = SignCenter.X + (Column - 1.5) * 1.8;
				const double Cz = SignCenter.Z + (Row == 0 ? 0.78 : -0.78);
				Kit::AddAxisBox(B, FVector(Cx, 0.0, Cz), FVector(0.88, 1.78, 0.7), Black);
			}
		}
		FitAndCenter(B, PelicanSize);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Protector solar
	// ─────────────────────────────────────────────────────────────────────────

	void BuildSunscreen(FBuffers& B)
	{
		const FLinearColor Orange = Tint(0xFF9E1B, 0.35f);
		const FLinearColor OrangeDark = Tint(0xD9780A, 0.3f);
		const FLinearColor White = Tint(0xFAFAF5, 0.4f);
		const FLinearColor WhiteShade = Tint(0xDCDCD2, 0.3f);
		const FLinearColor Cream = Tint(0xFFF0B8, 0.3f);
		const FLinearColor SunRed = Tint(0xFF5A1F, 0.35f);
		// Cuerpo ovalado, hombros que se cierran hacia el cuello, cuello y tapón.
		AddOvalFrustum(B, FVector(0.0, 0.0, 0.0), 15.0, 4.9, 7.4, 4.7, 7.2, 20, Orange, OrangeDark, true, false);
		AddOvalFrustum(B, FVector(0.0, 0.0, 15.0), 4.6, 4.7, 7.2, 3.1, 3.1, 20, Orange, Orange, false, false);
		Kit::AddFrustum(B, FVector(0.0, 0.0, 19.4), FVector(0.0, 0.0, 21.6), 3.1, 3.1, 14, WhiteShade, WhiteShade, false, false);
		Kit::AddFrustum(B, FVector(0.0, 0.0, 21.5), FVector(0.0, 0.0, 27.0), 3.7, 3.4, 16, White, White, false, true);
		for (int32 Ridge = 0; Ridge < 4; ++Ridge)
		{
			const double Z0 = 22.3 + 1.3 * Ridge;
			Kit::AddFrustum(B, FVector(0.0, 0.0, Z0), FVector(0.0, 0.0, Z0 + 0.55), 3.85, 3.85, 16, WhiteShade, WhiteShade, false, false);
		}
		Kit::AddFrustum(B, FVector(0.0, 0.0, 26.9), FVector(0.0, 0.0, 28.0), 1.7, 1.4, 10, White, White, false, true);
		// Moneda con un sol en el frente (+X).
		constexpr double SunZ = 8.0;
		Kit::AddFrustum(B, FVector(3.6, 0.0, SunZ), FVector(5.05, 0.0, SunZ), 3.9, 3.8, 18, Cream, Cream, false, true);
		Kit::AddDisc(B, FVector(5.1, 0.0, SunZ), FVector::ForwardVector, 2.0, 14, SunRed);
		for (int32 Ray = 0; Ray < 10; ++Ray)
		{
			const double Angle = Kit::KitTwoPi * Ray / 10.0;
			const auto RayPoint = [Angle](double Radius, double Turn)
			{
				return FVector(5.1, Radius * FMath::Cos(Angle + Turn), SunZ + Radius * FMath::Sin(Angle + Turn));
			};
			B.AddTri(RayPoint(2.5, -0.15), RayPoint(2.5, 0.15), RayPoint(3.55, 0.0), FVector::ForwardVector, SunRed);
		}
		FitAndCenter(B, 0.0);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Cangrejo teledirigido
	// ─────────────────────────────────────────────────────────────────────────

	void BuildHomingCrab(FBuffers& B)
	{
		const FLinearColor Red = Tint(0xE23B3B, 0.3f);
		const FLinearColor RedLight = Tint(0xFF6B5E, 0.3f);
		const FLinearColor RedDark = Tint(0xA81F27, 0.2f);
		const FLinearColor White = Tint(0xFFFFFF, 0.4f);
		const FLinearColor Black = Tint(0x151821, 0.2f);
		const FLinearColor Metal = Tint(0x9AA3B2, 0.8f);
		const FLinearColor Receiver = Tint(0x2E3440, 0.3f);
		const FLinearColor Lamp = Tint(0xFFE14D, 1.f);

		// Caparazón achatado con manchas claras.
		AddBlob(B, FVector(0.0, 0.0, 0.0), FVector(9.0, 12.0, 5.5), Red, 14);
		for (int32 Spot = 0; Spot < 4; ++Spot)
		{
			const double Y = (Spot % 2 == 0 ? -1.0 : 1.0) * (3.0 + 2.5 * (Spot / 2));
			const double X = -2.0 + 3.0 * (Spot / 2);
			AddBlob(B, FVector(X, Y, 4.5), FVector(1.7, 1.7, 0.7), RedLight, 8);
		}
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const double Side = Index == 0 ? 1.0 : -1.0;
			// Ojos en pedúnculo, mirando al frente.
			Kit::AddRod(B, FVector(6.5, Side * 3.2, 3.5), FVector(8.2, Side * 3.4, 7.6), 0.9, 6, RedLight, FVector::RightVector);
			Kit::AddBall(B, FVector(8.4, Side * 3.4, 8.7), 2.3, 10, White);
			Kit::AddBall(B, FVector(10.0, Side * 3.4, 8.9), 1.15, 8, Black);
			// Brazo y pinza abierta hacia delante.
			Kit::AddRod(B, FVector(5.0, Side * 8.5, 0.8), FVector(9.0, Side * 15.0, 2.2), 1.5, 8, Red, FVector::UpVector);
			AddBlob(B, FVector(13.0, Side * 16.5, 3.0), FVector(5.2, 4.2, 3.2), Red, 12);
			AddPitchBlob(B, FVector(19.0, Side * 18.8, 3.2), FVector(4.4, 1.5, 1.6), 0.0, RedLight, 10);
			AddPitchBlob(B, FVector(19.0, Side * 14.2, 3.0), FVector(4.2, 1.4, 1.5), 0.0, RedLight, 10);
			// Tres patas por lado, con codo.
			for (int32 Leg = 0; Leg < 3; ++Leg)
			{
				const double X0 = 3.0 - 4.0 * Leg;
				const FVector Hip(X0, Side * 9.5, -1.5);
				const FVector Knee(X0 - 1.0, Side * 15.5, 1.5);
				const FVector Foot(X0 - 3.0, Side * 20.0, -5.0);
				Kit::AddRod(B, Hip, Knee, 0.85, 6, RedDark, FVector::UpVector);
				Kit::AddRod(B, Knee, Foot, 0.8, 6, RedDark, FVector::UpVector);
				Kit::AddBall(B, Knee, 0.85, 6, RedDark);
				Kit::AddBall(B, Foot, 0.95, 6, RedDark);
			}
		}
		// Antena del mando a distancia: receptor, varilla y bolita brillante.
		Kit::AddAxisBox(B, FVector(-5.0, 0.0, 4.6), FVector(2.4, 3.2, 1.2), Receiver);
		Kit::AddBall(B, FVector(-3.6, 0.0, 5.9), 0.75, 6, Tint(0xFF3B30, 1.f));
		Kit::AddRod(B, FVector(-5.4, 0.0, 5.6), FVector(-6.6, 0.0, 17.2), 0.6, 6, Metal, FVector::RightVector);
		Kit::AddBall(B, FVector(-6.7, 0.0, 18.8), 1.9, 10, Lamp);
		FitAndCenter(B, CrabSize);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Gaviota justiciera
	// ─────────────────────────────────────────────────────────────────────────

	void BuildGull(FBuffers& B)
	{
		const FLinearColor White = Tint(0xF8F8F4, 0.1f);
		const FLinearColor WingGray = Tint(0xA5AEBD, 0.08f);
		const FLinearColor WingTip = Tint(0x2B303B, 0.1f);
		const FLinearColor Beak = Tint(0xFFC42E, 0.3f);
		const FLinearColor BeakRed = Tint(0xE63946, 0.3f);
		const FLinearColor Legs = Tint(0xFF9A2E, 0.25f);
		const FLinearColor Dark = Tint(0x1E232D, 0.2f);
		const FLinearColor EyeRing = Tint(0xFFD23F, 0.35f);

		// Cuerpo, pecho, cuello, cola.
		AddBlob(B, FVector(0.0, 0.0, 0.0), FVector(10.0, 4.6, 4.4), White, 14);
		AddBlob(B, FVector(5.0, 0.0, -0.5), FVector(6.0, 4.4, 4.2), White, 12);
		AddBlob(B, FVector(9.0, 0.0, 1.6), FVector(4.0, 3.4, 3.4), White, 10);
		AddBlob(B, FVector(-12.0, 0.0, 0.4), FVector(5.5, 3.5, 0.8), Tint(0xE9EAEE, 0.08f), 10);
		// Cabeza grande, pico amarillo con gancho y mancha roja.
		Kit::AddBall(B, FVector(11.5, 0.0, 3.2), 4.2, 12, White);
		Kit::AddFrustum(B, FVector(14.8, 0.0, 2.7), FVector(21.4, 0.0, 1.4), 1.9, 0.65, 10, Beak, Beak, false, false);
		Kit::AddBall(B, FVector(21.5, 0.0, 1.2), 0.8, 6, Beak);
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const double Side = Index == 0 ? 1.0 : -1.0;
			// Ojo amarillo con pupila y ceja fruncida: la punta de dentro baja (cara de enfadada).
			Kit::AddBall(B, FVector(13.9, Side * 3.1, 4.0), 1.25, 8, EyeRing);
			Kit::AddBall(B, FVector(14.6, Side * 3.25, 4.0), 0.72, 6, Dark);
			Kit::AddRod(B, FVector(12.85, Side * 3.7, 6.3), FVector(15.4, Side * 0.9, 5.2), 0.7, 6, Dark, FVector::UpVector);
			Kit::AddBall(B, FVector(19.8, Side * 0.75, 0.9), 0.45, 6, BeakRed);
			// Alas en uve: la de dentro y la de fuera, con la punta negra.
			AddWingBlob(B, FVector(-0.5, Side * 9.5, 3.0), FVector(5.8, 8.5, 0.9), 14.0, Side, White, 12);
			AddWingBlob(B, FVector(-2.2, Side * 20.5, 6.5), FVector(4.4, 7.0, 0.7), 24.0, Side, WingGray, 12);
			AddWingBlob(B, FVector(-4.5, Side * 26.5, 8.6), FVector(2.6, 3.6, 0.55), 26.0, Side, WingTip, 10);
			// Patas con las garras hacia delante.
			const FVector Hip(2.0, Side * 2.2, -3.8);
			const FVector Foot(4.5, Side * 2.6, -7.8);
			Kit::AddRod(B, Hip, Foot, 0.55, 6, Legs, FVector::RightVector);
			for (int32 Toe = -1; Toe <= 1; ++Toe)
			{
				Kit::AddRod(B, Foot, FVector(7.6, Side * 2.6 + Toe * 1.3, -8.6), 0.4, 5, Legs, FVector::UpVector);
			}
		}
		FitAndCenter(B, GullSize);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Mina de arena
	// ─────────────────────────────────────────────────────────────────────────

	void BuildSandMine(FBuffers& B)
	{
		const FLinearColor Olive = Tint(0x6B7A2E, 0.2f);
		const FLinearColor OliveDark = Tint(0x4B5722, 0.2f);
		const FLinearColor OliveBottom = Tint(0x37401A, 0.1f);
		const FLinearColor Metal = Tint(0xC9D2B4, 0.65f);
		const FLinearColor Bolt = Tint(0x9BA38A, 0.7f);
		const FLinearColor Black = Tint(0x1B1E17, 0.2f);
		const FLinearColor Red = Tint(0xFF2A2A, 1.f);

		Kit::AddFrustum(B, FVector(0.0, 0.0, 0.0), FVector(0.0, 0.0, 5.0), 14.5, 13.4, 22, Olive, OliveBottom, true, false);
		Kit::AddAnnulus(B, FVector(0.0, 0.0, 5.0), FVector::UpVector, 11.0, 13.4, 22, OliveDark);
		AddBlob(B, FVector(0.0, 0.0, 5.0), FVector(12.0, 12.0, 4.6), Olive, 20);
		for (int32 Index = 0; Index < 8; ++Index)
		{
			const double Angle = Kit::KitTwoPi * (Index + 0.5) / 8.0;
			Kit::AddBall(B, FVector(13.9 * FMath::Cos(Angle), 13.9 * FMath::Sin(Angle), 2.6), 0.95, 6, Bolt);
		}
		// Tres pinchos a 120 grados (uno mira al frente).
		for (int32 Spike = 0; Spike < 3; ++Spike)
		{
			const double Angle = Kit::KitTwoPi * Spike / 3.0;
			const FVector Foot(7.5 * FMath::Cos(Angle), 7.5 * FMath::Sin(Angle), 6.5);
			Kit::AddFrustum(B, Foot, Foot + FVector(0.0, 0.0, 10.0), 1.9, 0.15, 8, Metal, Metal, false, false);
			Kit::AddFrustum(B, Foot + FVector(0.0, 0.0, 1.6), Foot + FVector(0.0, 0.0, 2.8), 2.7, 2.2, 8, Bolt, Bolt, false, true);
		}
		// Luz roja del centro sobre un soporte negro.
		Kit::AddFrustum(B, FVector(0.0, 0.0, 8.6), FVector(0.0, 0.0, 10.2), 2.8, 2.6, 12, Black, Black, false, true);
		Kit::AddBall(B, FVector(0.0, 0.0, 11.7), 2.5, 10, Red);
		FitAndCenter(B, 0.0);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Nube de tormenta
	// ─────────────────────────────────────────────────────────────────────────

	void BuildStormCloud(FBuffers& B)
	{
		struct FPuff
		{
			double X;
			double Y;
			double Z;
			double Radius;
			uint32 Rgb24;
		};
		const FPuff Puffs[] = {
			{ 0.0, 0.0, 0.0, 9.5, 0x59606E }, { -9.5, 2.0, -1.0, 7.5, 0x4F5665 }, { 9.5, -2.0, -1.0, 8.0, 0x4B5262 }, { 0.0, -8.5, -1.5, 6.8, 0x454B5A },
			{ 1.0, 8.5, -1.5, 7.0, 0x4A5160 }, { -3.0, -1.0, 5.5, 7.2, 0x6C7484 }, { 5.0, 2.0, 4.8, 6.2, 0x666E7E }, { -15.0, -2.0, -2.5, 5.0, 0x474D5C },
			{ 15.0, 3.0, -2.5, 5.0, 0x474D5C }, { -6.0, -7.0, 2.5, 5.5, 0x5A6170 }, { 7.0, 7.0, 2.0, 5.5, 0x5A6170 } };
		AddBlob(B, FVector(0.0, 0.0, -5.5), FVector(16.0, 12.5, 3.0), Tint(0x3A404E, 0.05f), 16);
		for (const FPuff& Puff : Puffs)
		{
			Kit::AddBall(B, FVector(Puff.X, Puff.Y, Puff.Z), Puff.Radius, 14, Tint(Puff.Rgb24, 0.05f));
		}
		// Rayo amarillo debajo: tres tramos en zigzag, en un plano girado 40 grados para que se vea desde casi todos los lados.
		const double Turn = FMath::DegreesToRadians(40.0);
		const FVector Across(FMath::Cos(Turn), FMath::Sin(Turn), 0.0);
		const FVector Thick(-FMath::Sin(Turn), FMath::Cos(Turn), 0.0);
		const FLinearColor Yellow = Tint(0xFFE14D, 1.f);
		const FVector2D Zig[4] = { FVector2D(3.5, -7.5), FVector2D(-3.0, -14.5), FVector2D(3.2, -14.2), FVector2D(-2.5, -24.0) };
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const FVector From = Across * Zig[Index].X + FVector::UpVector * Zig[Index].Y;
			const FVector To = Across * Zig[Index + 1].X + FVector::UpVector * Zig[Index + 1].Y;
			const FVector Dir = (To - From).GetSafeNormal();
			const FVector Wide = FVector::CrossProduct(Thick, Dir).GetSafeNormal();
			AddFrameBox(B, (From + To) * 0.5, Dir, Thick, Wide, FVector(FVector::Dist(From, To) * 0.5 + 1.0, 1.0, 2.1), Yellow);
		}
		FitAndCenter(B, CloudSize);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Disco volador
	// ─────────────────────────────────────────────────────────────────────────

	void BuildFrisbee(FBuffers& B)
	{
		const FVector Center = FVector::ZeroVector;
		// Anillos de colores del centro al borde, con la cúpula suave y el labio que baja.
		AddDishBand(B, Center, 0.0, 1.6, 3.5, 1.55, 24, Tint(0xFFCB3D, 0.45f));
		AddDishBand(B, Center, 3.5, 1.55, 6.5, 1.35, 24, Tint(0xFF6A52, 0.4f));
		AddDishBand(B, Center, 6.5, 1.35, 9.5, 1.0, 24, Tint(0xFFF5DC, 0.4f));
		AddDishBand(B, Center, 9.5, 1.0, 12.5, 0.45, 24, Tint(0x2EC4B6, 0.4f));
		AddDishBand(B, Center, 12.5, 0.45, 14.4, -0.3, 24, Tint(0x9B5DE5, 0.4f));
		AddDishBand(B, Center, 14.4, -0.3, 15.0, -1.6, 24, Tint(0xFFCB3D, 0.45f));
		Kit::AddDisc(B, FVector(0.0, 0.0, -1.6), -FVector::UpVector, 15.0, 24, Tint(0xE9E2D0, 0.2f));
		FitAndCenter(B, 0.0);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Silbato del sargento
	// ─────────────────────────────────────────────────────────────────────────

	void BuildWhistle(FBuffers& B)
	{
		const FLinearColor Brass = Tint(0xE2B33C, 0.85f);
		const FLinearColor BrassDark = Tint(0xB98A26, 0.7f);
		const FLinearColor Dark = Tint(0x2A2118, 0.1f);
		const FLinearColor Pea = Tint(0xFAFAF5, 0.5f);
		const FLinearColor Steel = Tint(0xC5CBD6, 0.9f);
		const FLinearColor Cord = Tint(0xE63946, 0.15f);

		// Cámara redonda (eje Y) y tubo de la boquilla hacia delante.
		Kit::AddFrustum(B, FVector(-3.0, -4.8, 0.0), FVector(-3.0, 4.8, 0.0), 5.8, 5.8, 22, Brass, BrassDark, true, true);
		Kit::AddFrustum(B, FVector(-3.0, -4.9, 0.0), FVector(-3.0, -4.3, 0.0), 6.0, 6.0, 22, BrassDark, BrassDark, false, false);
		Kit::AddFrustum(B, FVector(-3.0, 4.3, 0.0), FVector(-3.0, 4.9, 0.0), 6.0, 6.0, 22, BrassDark, BrassDark, false, false);
		Kit::AddFrustum(B, FVector(-3.0, 0.0, -1.3), FVector(14.0, 0.0, -1.3), 3.4, 2.8, 18, Brass, Brass, false, false);
		Kit::AddDisc(B, FVector(14.02, 0.0, -1.3), FVector::ForwardVector, 2.1, 12, Dark);
		Kit::AddAnnulus(B, FVector(14.03, 0.0, -1.3), FVector::ForwardVector, 2.1, 2.8, 18, BrassDark);
		// Ventana de aire con su filo.
		AddFrameBox(B, FVector(7.5, 0.0, 1.7), FVector::ForwardVector, FVector::RightVector, FVector::UpVector, FVector(1.7, 1.9, 0.12), Dark);
		AddFrameBox(B, FVector(9.5, 0.0, 2.0), FVector::ForwardVector, FVector::RightVector, FVector::UpVector, FVector(0.5, 2.0, 0.4), BrassDark);
		// Boca redonda arriba de la cámara con la bolita dentro.
		Kit::AddFrustum(B, FVector(-3.0, 0.0, 4.5), FVector(-3.0, 0.0, 5.95), 2.95, 2.7, 14, BrassDark, BrassDark, false, false);
		Kit::AddDisc(B, FVector(-3.0, 0.0, 5.0), FVector::UpVector, 2.6, 12, Dark);
		Kit::AddBall(B, FVector(-3.0, 0.0, 5.3), 2.1, 10, Pea);
		// Anilla trasera y cordón enhebrado.
		AddRing(B, FVector(-10.6, 0.0, 1.5), FVector::ForwardVector, FVector::UpVector, 2.6, 0.55, 14, Steel);
		AddRing(B, FVector(-10.6, 0.0, 1.5 - 5.0), FVector::RightVector, FVector::UpVector, 5.0, 0.45, 20, Cord);
		FitAndCenter(B, 0.0);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Reparto y caché de mallas
	// ─────────────────────────────────────────────────────────────────────────

	/** Dibuja la malla de Kind en B; false si ese objeto no tiene. */
	bool BuildKind(ETNRaceItem Kind, FBuffers& B)
	{
		switch (Kind)
		{
			case ETNRaceItem::Box:
				BuildBox(B);
				return true;
			case ETNRaceItem::Coconut:
				BuildCoconut(B);
				return true;
			case ETNRaceItem::TripleCoconut3:
				BuildCoconutCluster(B, 3);
				return true;
			case ETNRaceItem::TripleCoconut2:
				BuildCoconutCluster(B, 2);
				return true;
			case ETNRaceItem::TripleCoconut1:
				BuildCoconutCluster(B, 1);
				return true;
			case ETNRaceItem::GoldenCoconut:
				BuildGoldenCoconut(B);
				return true;
			case ETNRaceItem::PelicanTaxi:
				BuildPelican(B);
				return true;
			case ETNRaceItem::Sunscreen:
				BuildSunscreen(B);
				return true;
			case ETNRaceItem::HomingCrab:
				BuildHomingCrab(B);
				return true;
			case ETNRaceItem::GullStrike:
				BuildGull(B);
				return true;
			case ETNRaceItem::SandMine:
				BuildSandMine(B);
				return true;
			case ETNRaceItem::StormCloud:
				BuildStormCloud(B);
				return true;
			case ETNRaceItem::Frisbee:
				BuildFrisbee(B);
				return true;
			case ETNRaceItem::Whistle:
				BuildWhistle(B);
				return true;
			default:
				return false;
		}
	}

	/** La malla de Kind, construida una sola vez y fuera del recolector (las filas del inventario la referencian). */
	UStaticMesh* GetMesh(ETNRaceItem Kind)
	{
		static TMap<int32, UStaticMesh*> Cache;
		const int32 Key = static_cast<int32>(Kind);
		if (UStaticMesh** Found = Cache.Find(Key))
		{
			return *Found;
		}
		UStaticMesh* Mesh = nullptr;
		FBuffers Buffers;
		if (BuildKind(Kind, Buffers) && !Buffers.IsEmpty())
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

	// ─────────────────────────────────────────────────────────────────────────
	// Iconos (128x128, pegatina del HUD)
	// ─────────────────────────────────────────────────────────────────────────

	/** Lado en píxeles de los iconos cuadrados de los objetos. */
	constexpr int32 IconSize = 128;

	/** Silueta con filo oscuro y degradado vertical: el relleno básico de las pegatinas. */
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

	/** Rayo de 7 vértices centrado en (Cx, Cy) con la escala Unit (el rayo mide 9,2 x 5,2 unidades). */
	TArray<FVector2f> BoltPoints(float Cx, float Cy, float Unit)
	{
		TArray<FVector2f> Points;
		Points.Add(FVector2f(Cx + 0.6f * Unit, Cy - 4.6f * Unit));
		Points.Add(FVector2f(Cx - 2.6f * Unit, Cy + 0.4f * Unit));
		Points.Add(FVector2f(Cx - 0.2f * Unit, Cy + 0.4f * Unit));
		Points.Add(FVector2f(Cx - 1.2f * Unit, Cy + 4.6f * Unit));
		Points.Add(FVector2f(Cx + 2.6f * Unit, Cy - 0.8f * Unit));
		Points.Add(FVector2f(Cx + 0.2f * Unit, Cy - 0.8f * Unit));
		Points.Add(FVector2f(Cx + 1.6f * Unit, Cy - 4.6f * Unit));
		return Points;
	}

	/** Distancia a la silueta de un coco peludo: círculo con mechones en punta. */
	float FuzzyBallSdf(float px, float py, float Cx, float Cy, float Radius)
	{
		const float Angle = FMath::Atan2(py - Cy, px - Cx);
		const float WaveA = FMath::Abs(FMath::Frac(Angle * 19.f / (2.f * PI)) * 2.f - 1.f);
		const float WaveB = FMath::Abs(FMath::Frac(Angle * 7.f / (2.f * PI) + 0.3f) * 2.f - 1.f);
		return TNHUDArt::Circle(px, py, Cx, Cy, 0.f) - Radius * (0.955f + 0.05f * WaveA + 0.03f * WaveB);
	}

	/** Dibuja un coco peludo (con la pegatina del rayo) de radio Radius en (Cx, Cy). */
	void PaintCoconutBall(TNHUDArt::FPainter& Painter, float Cx, float Cy, float Radius, bool bGolden, bool bSticker)
	{
		using namespace TNHUDArt;
		const auto Ball = [Cx, Cy, Radius](float px, float py) { return FuzzyBallSdf(px, py, Cx, Cy, Radius); };
		const FLinearColor RimColor = bGolden ? Hex(0x8A5A00) : Hex(0x2E1B0C);
		const FLinearColor Light = bGolden ? Hex(0xFFE27A) : Hex(0xA47142);
		const FLinearColor Shadow = bGolden ? Hex(0xE89A0C) : Hex(0x5A3818);
		const FLinearColor Strand = bGolden ? Hex(0xB9760A, 0.75f) : Hex(0x3A2210, 0.75f);
		PaintBody(Painter, Ball, RimColor, Light, Shadow, Cy - Radius, Cy + Radius);
		// Mechones: trazos cortos y radiales repartidos por la bola.
		for (int32 Index = 0; Index < 28; ++Index)
		{
			const float Angle = Index * 2.39996f;
			const float Dist = Radius * (0.22f + 0.7f * FMath::Frac(Index * 0.61803f));
			const float Sx = Cx + FMath::Cos(Angle) * Dist;
			const float Sy = Cy + FMath::Sin(Angle) * Dist;
			const float Ex = Sx + FMath::Cos(Angle) * Radius * 0.16f;
			const float Ey = Sy + FMath::Sin(Angle) * Radius * 0.16f;
			Painter.Fill([&](float px, float py) { return FMath::Max(Segment(px, py, Sx, Sy, Ex, Ey, 1.1f), Ball(px, py) + 1.f); }, Strand);
		}
		// Brillo arriba a la izquierda.
		Painter.Fill([&](float px, float py) { return FMath::Max(Ellipse(px, py, Cx - Radius * 0.38f, Cy - Radius * 0.45f, Radius * 0.22f, Radius * 0.12f), Ball(px, py) + 2.f); },
			Hex(0xFFFFFF, bGolden ? 0.75f : 0.35f));
		if (bSticker)
		{
			const float StickerX = Cx + Radius * 0.08f;
			const float StickerY = Cy + Radius * 0.14f;
			const float StickerR = Radius * 0.5f;
			const TArray<FVector2f> Bolt = BoltPoints(StickerX, StickerY, StickerR * 0.17f);
			Painter.Fill([&](float px, float py) { return Circle(px, py, StickerX, StickerY, StickerR + 1.6f); }, RimColor);
			Painter.Fill([&](float px, float py) { return Circle(px, py, StickerX, StickerY, StickerR); }, bGolden ? Hex(0xFFF6D8) : Gold);
			Painter.Fill([&](float px, float py) { return Polygon(px, py, Bolt) - 0.8f; }, bGolden ? Hex(0xB8480A) : Hex(0x22263A));
			Painter.Fill([&](float px, float py) { return Polygon(px, py, Bolt); }, bGolden ? Hex(0xFF8A1F) : Hex(0xFF5A1F));
		}
	}

	/** Pajita rayada verde y blanca de (X0, Y0) a (X1, Y1). */
	void PaintStrawLeg(TNHUDArt::FPainter& Painter, float X0, float Y0, float X1, float Y1, float Radius, int32 Pieces, int32 FirstIndex, bool bGolden)
	{
		using namespace TNHUDArt;
		for (int32 Piece = 0; Piece < Pieces; ++Piece)
		{
			const float T0 = static_cast<float>(Piece) / Pieces;
			const float T1 = static_cast<float>(Piece + 1) / Pieces;
			const float Ax = FMath::Lerp(X0, X1, T0);
			const float Ay = FMath::Lerp(Y0, Y1, T0);
			const float Bx = FMath::Lerp(X0, X1, T1);
			const float By = FMath::Lerp(Y0, Y1, T1);
			const bool bA = ((FirstIndex + Piece) % 2) == 0;
			const FLinearColor Stripe = bA ? (bGolden ? Hex(0xFFD23F) : Hex(0x2EC46A)) : Hex(0xFFFFFF);
			Painter.Fill([=](float px, float py) { return Segment(px, py, Ax, Ay, Bx, By, Radius); }, Stripe);
		}
	}

	UTexture2D* PaintBoxIcon()
	{
		using namespace TNHUDArt;
		FPainter Painter(IconSize, IconSize);
		const TArray<FVector2f> TopPoints = { { 22.f, 40.f }, { 92.f, 40.f }, { 108.f, 22.f }, { 38.f, 22.f } };
		const TArray<FVector2f> SidePoints = { { 92.f, 40.f }, { 108.f, 22.f }, { 108.f, 92.f }, { 92.f, 110.f } };
		const auto Front = [](float px, float py) { return Box(px, py, 57.f, 75.f, 35.f, 35.f, 3.f); };
		const auto Top = [&TopPoints](float px, float py) { return Polygon(px, py, TopPoints); };
		const auto Side = [&SidePoints](float px, float py) { return Polygon(px, py, SidePoints); };
		const auto All = [&](float px, float py) { return FMath::Min(Front(px, py), FMath::Min(Top(px, py), Side(px, py))); };
		Painter.Sticker(All, 6.f);
		Painter.Fill(All, Hex(0x13233B));
		Painter.Layer([&](float px, float py) { return Front(px, py) + 1.6f; }, [](float, float py) { return Mix(Hex(0xFF8A70), Hex(0xE8503A), (py - 40.f) / 70.f); });
		Painter.Layer([&](float px, float py) { return Top(px, py) + 1.6f; }, [](float px, float) { return Mix(Hex(0xFFE58A), Hex(0xFFC93D), (px - 22.f) / 80.f); });
		Painter.Layer([&](float px, float py) { return Side(px, py) + 1.6f; }, [](float, float py) { return Mix(Hex(0x2EC4B6), Hex(0x1A8F86), (py - 22.f) / 88.f); });
		// Brillo del canto de arriba y de la izquierda de la cara de delante.
		Painter.Fill([&](float px, float py) { return FMath::Max(FMath::Abs(py - 45.f) - 1.2f, Front(px, py) + 4.f); }, Hex(0xFFFFFF, 0.35f));
		// La «?»: gancho, palo y punto, blanca con contorno oscuro.
		const auto Mark = [](float px, float py)
		{
			float Dist = Arc(px, py, 57.f, 62.f, 11.f, -3.5f, 0.9f, 4.6f);
			Dist = FMath::Min(Dist, Segment(px, py, 63.8f, 70.6f, 57.5f, 79.5f, 4.6f));
			Dist = FMath::Min(Dist, Segment(px, py, 57.5f, 79.5f, 57.5f, 85.5f, 4.6f));
			return FMath::Min(Dist, Circle(px, py, 57.5f, 97.f, 4.6f));
		};
		Painter.Fill([&](float px, float py) { return Mark(px, py) - 2.4f; }, Hex(0x13233B));
		Painter.Fill(Mark, FLinearColor::White);
		return Painter.ToTexture(TEXT("TN_Race_Box"));
	}

	/** Icono de un coco turbo o dorado suelto: bola peluda con pajita y pegatina. */
	UTexture2D* PaintCoconutIcon(bool bGolden)
	{
		using namespace TNHUDArt;
		FPainter Painter(IconSize, IconSize);
		const float Cx = 60.f;
		const float Cy = 76.f;
		const float Radius = 35.f;
		// Silueta de la pegatina: la bola y la pajita con su codo.
		const auto Straw = [](float px, float py)
		{
			return FMath::Min(Segment(px, py, 66.f, 50.f, 72.f, 22.f, 5.4f), Segment(px, py, 72.f, 22.f, 96.f, 16.f, 5.4f));
		};
		const auto Ball = [Cx, Cy, Radius](float px, float py) { return FuzzyBallSdf(px, py, Cx, Cy, Radius); };
		Painter.Sticker([&](float px, float py) { return FMath::Min(Ball(px, py), Straw(px, py)); }, 6.f);
		// Pajita por detrás de la bola.
		Painter.Fill(Straw, bGolden ? Hex(0x8A5A00) : Hex(0x12472B));
		PaintStrawLeg(Painter, 66.f, 50.f, 72.f, 22.f, 4.0f, 5, 0, bGolden);
		PaintStrawLeg(Painter, 72.f, 22.f, 96.f, 16.f, 4.0f, 4, 5, bGolden);
		PaintCoconutBall(Painter, Cx, Cy, Radius, bGolden, true);
		if (bGolden)
		{
			// Destellos de estrella de cuatro puntas.
			const TArray<FVector2f> StarA = StarPoints(104.f, 62.f, 13.f, 0.3f, 4);
			const TArray<FVector2f> StarB = StarPoints(18.f, 34.f, 10.f, 0.3f, 4);
			const TArray<FVector2f> StarC = StarPoints(110.f, 100.f, 8.f, 0.3f, 4);
			Painter.Fill([&](float px, float py) { return Polygon(px, py, StarA) - 1.6f; }, Hex(0xB8760A));
			Painter.Fill([&](float px, float py) { return Polygon(px, py, StarB) - 1.6f; }, Hex(0xB8760A));
			Painter.Fill([&](float px, float py) { return Polygon(px, py, StarC) - 1.6f; }, Hex(0xB8760A));
			Painter.Fill([&](float px, float py) { return FMath::Min(Polygon(px, py, StarA), FMath::Min(Polygon(px, py, StarB), Polygon(px, py, StarC))); },
				FLinearColor::White);
		}
		return Painter.ToTexture(bGolden ? TEXT("TN_Race_GoldenCoconut") : TEXT("TN_Race_Coconut"));
	}

	/** Icono del triple coco: en triángulo, Count cocos llenos y el resto en un contorno discontinuo. */
	UTexture2D* PaintTripleIcon(int32 Count)
	{
		using namespace TNHUDArt;
		FPainter Painter(IconSize, IconSize);
		const float Radius = 26.f;
		const FVector2f Spots[3] = { FVector2f(64.f, 40.f), FVector2f(38.f, 84.f), FVector2f(90.f, 84.f) };
		// Se gastan de arriba abajo: quedan los de abajo.
		const bool bFull[3] = { Count >= 3, Count >= 2, Count >= 1 };
		const auto Cluster = [&](float px, float py)
		{
			float Dist = FLT_MAX;
			for (int32 Index = 0; Index < 3; ++Index)
			{
				if (bFull[Index])
				{
					Dist = FMath::Min(Dist, FuzzyBallSdf(px, py, Spots[Index].X, Spots[Index].Y, Radius));
				}
			}
			return Dist;
		};
		Painter.Sticker(Cluster, 6.f);
		// De atrás hacia delante: el de arriba primero.
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (bFull[Index])
			{
				PaintCoconutBall(Painter, Spots[Index].X, Spots[Index].Y, Radius, false, true);
			}
		}
		// Los cocos que ya no están: contorno discontinuo crema.
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (bFull[Index])
			{
				continue;
			}
			const float Gx = Spots[Index].X;
			const float Gy = Spots[Index].Y;
			Painter.Fill([&](float px, float py)
			{
				const float Angle = FMath::Atan2(py - Gy, px - Gx);
				const float Dash = FMath::Frac(Angle * 10.f / (2.f * PI)) < 0.55f ? -1.f : 1.f;
				return FMath::Max(FMath::Abs(Circle(px, py, Gx, Gy, Radius - 2.f)) - 1.8f, Dash);
			}, Hex(0xFFFBF0, 0.6f));
		}
		return Painter.ToTexture(Count >= 3 ? TEXT("TN_Race_TripleCoconut3") : (Count == 2 ? TEXT("TN_Race_TripleCoconut2") : TEXT("TN_Race_TripleCoconut1")));
	}

	UTexture2D* PaintPelicanIcon()
	{
		using namespace TNHUDArt;
		FPainter Painter(IconSize, IconSize);
		const auto Neck = [](float px, float py) { return Box(px, py, 38.f, 98.f, 15.f, 16.f, 8.f); };
		const auto Head = [](float px, float py) { return Circle(px, py, 44.f, 66.f, 21.f); };
		const TArray<FVector2f> BillPoints = { { 58.f, 55.f }, { 110.f, 62.f }, { 120.f, 68.f }, { 110.f, 72.f }, { 58.f, 70.f } };
		const TArray<FVector2f> PouchPoints = { { 60.f, 70.f }, { 110.f, 72.f }, { 104.f, 90.f }, { 86.f, 99.f }, { 68.f, 92.f } };
		const TArray<FVector2f> PostPoints = { { 32.f, 36.f }, { 38.f, 36.f }, { 38.f, 47.f }, { 32.f, 47.f } };
		const auto Bill = [&BillPoints](float px, float py) { return Polygon(px, py, BillPoints); };
		const auto Pouch = [&PouchPoints](float px, float py) { return Polygon(px, py, PouchPoints); };
		const auto Post = [&PostPoints](float px, float py) { return Polygon(px, py, PostPoints); };
		const auto Sign = [](float px, float py) { return Box(px, py, 50.f, 27.f, 25.f, 9.f, 3.f); };
		const auto All = [&](float px, float py)
		{
			return FMath::Min(FMath::Min(Neck(px, py), Head(px, py)), FMath::Min(FMath::Min(Bill(px, py), Pouch(px, py)), FMath::Min(Sign(px, py), Post(px, py))));
		};
		Painter.Sticker(All, 6.f);
		// Bolsa y pico por detrás de la cabeza.
		PaintBody(Painter, Pouch, Hex(0x7A4A0A), Hex(0xFFE08A), Hex(0xF2B13A), 70.f, 99.f);
		PaintBody(Painter, Bill, Hex(0x7A3A0A), Hex(0xFFB03A), Hex(0xEE8A1C), 55.f, 72.f);
		PaintBody(Painter, Neck, Hex(0x3C4457), Hex(0xFFFFFF), Hex(0xD5DAE3), 82.f, 114.f);
		PaintBody(Painter, Head, Hex(0x3C4457), Hex(0xFFFFFF), Hex(0xDDE1EA), 45.f, 87.f);
		// Ojo con brillo y ceja de pelícano serio.
		Painter.Fill([](float px, float py) { return Circle(px, py, 53.f, 62.f, 4.8f); }, Hex(0x151821));
		Painter.Fill([](float px, float py) { return Circle(px, py, 54.6f, 60.4f, 1.5f); }, FLinearColor::White);
		Painter.Fill([](float px, float py) { return Ellipse(px, py, 84.f, 86.f, 12.f, 3.f); }, Hex(0xFFFFFF, 0.35f));
		// Cartelito de taxi: caja amarilla con damero, sobre dos postes.
		PaintBody(Painter, Post, Hex(0x2E3440), Hex(0x59606E), Hex(0x2E3440), 36.f, 47.f);
		PaintBody(Painter, Sign, Hex(0x13233B), Hex(0xFFE27A), Hex(0xFFC42E), 18.f, 36.f);
		for (int32 Column = 0; Column < 8; ++Column)
		{
			for (int32 Row = 0; Row < 2; ++Row)
			{
				if (((Column + Row) % 2) != 0)
				{
					continue;
				}
				const float Cx = 28.f + Column * 6.f;
				const float Cy = 22.f + Row * 6.f;
				Painter.Fill([=](float px, float py) { return Box(px, py, Cx, Cy, 3.f, 3.f, 0.f); }, Hex(0x13233B));
			}
		}
		return Painter.ToTexture(TEXT("TN_Race_PelicanTaxi"));
	}

	UTexture2D* PaintSunscreenIcon()
	{
		using namespace TNHUDArt;
		FPainter Painter(IconSize, IconSize);
		const auto Body = [](float px, float py) { return Box(px, py, 64.f, 80.f, 27.f, 30.f, 12.f); };
		const TArray<FVector2f> ShoulderPoints = { { 40.f, 56.f }, { 88.f, 56.f }, { 76.f, 34.f }, { 52.f, 34.f } };
		const auto Shoulder = [&ShoulderPoints](float px, float py) { return Polygon(px, py, ShoulderPoints); };
		const auto Cap = [](float px, float py) { return Box(px, py, 64.f, 24.f, 12.f, 12.f, 4.f); };
		const auto Bottle = [&](float px, float py) { return FMath::Min(Body(px, py), Shoulder(px, py)); };
		const auto All = [&](float px, float py) { return FMath::Min(Bottle(px, py), Cap(px, py)); };
		Painter.Sticker(All, 6.f);
		PaintBody(Painter, Bottle, Hex(0x7A3A08), Hex(0xFFC03A), Hex(0xF07A14), 34.f, 110.f);
		// Etiqueta crema con el sol.
		const auto Label = [](float px, float py) { return Box(px, py, 64.f, 82.f, 27.f, 18.f, 3.f); };
		Painter.Fill([&](float px, float py) { return FMath::Max(Label(px, py), Body(px, py)); }, Hex(0xFFF3C4));
		Painter.Fill([](float px, float py) { return Circle(px, py, 64.f, 82.f, 9.f) - 1.4f; }, Hex(0xB8480A));
		Painter.Fill([](float px, float py) { return Circle(px, py, 64.f, 82.f, 9.f); }, Hex(0xFF6A1F));
		for (int32 Ray = 0; Ray < 8; ++Ray)
		{
			const float Angle = Ray * PI / 4.f;
			const float Ax = 64.f + FMath::Cos(Angle) * 12.f;
			const float Ay = 82.f + FMath::Sin(Angle) * 12.f;
			const float Bx = 64.f + FMath::Cos(Angle) * 16.f;
			const float By = 82.f + FMath::Sin(Angle) * 16.f;
			Painter.Fill([=](float px, float py) { return Segment(px, py, Ax, Ay, Bx, By, 1.7f); }, Hex(0xFF6A1F));
		}
		// Tapón blanco con estrías y brillo de la botella.
		PaintBody(Painter, Cap, Hex(0x5C6478), Hex(0xFFFFFF), Hex(0xD9DEE8), 12.f, 36.f);
		for (const float Rx : { 56.f, 64.f, 72.f })
		{
			Painter.Fill([=](float px, float py) { return FMath::Max(Segment(px, py, Rx, 16.f, Rx, 33.f, 0.9f), Cap(px, py) + 2.f); }, Hex(0x9AA3B8, 0.7f));
		}
		Painter.Fill([&](float px, float py) { return FMath::Max(Segment(px, py, 45.f, 62.f, 45.f, 100.f, 2.2f), Body(px, py) + 3.f); }, Hex(0xFFFFFF, 0.4f));
		return Painter.ToTexture(TEXT("TN_Race_Sunscreen"));
	}

	UTexture2D* PaintCrabIcon()
	{
		using namespace TNHUDArt;
		FPainter Painter(IconSize, IconSize);
		const FLinearColor RedTop = Hex(0xFF6B5E);
		const FLinearColor RedBottom = Hex(0xC92A32);
		const FLinearColor Outline = Hex(0x4A0F14);
		const auto Shell = [](float px, float py) { return Ellipse(px, py, 64.f, 78.f, 32.f, 22.f); };
		// Pinzas (izquierda y, espejadas, derecha): brazo, palma y dos dedos en uve.
		const auto Claw = [](float px, float py, float Mirror)
		{
			const float Mx = Mirror > 0.f ? px : IconSize - px;
			float Dist = Segment(Mx, py, 44.f, 66.f, 30.f, 52.f, 5.f);
			Dist = FMath::Min(Dist, Circle(Mx, py, 28.f, 46.f, 11.f));
			Dist = FMath::Min(Dist, Segment(Mx, py, 22.f, 40.f, 18.f, 22.f, 5.5f));
			return FMath::Min(Dist, Segment(Mx, py, 34.f, 38.f, 37.f, 20.f, 4.5f));
		};
		const auto Legs = [](float px, float py)
		{
			const float Mx = FMath::Min(px, IconSize - px);
			float Dist = FLT_MAX;
			for (int32 Leg = 0; Leg < 3; ++Leg)
			{
				const float Y0 = 82.f + Leg * 8.f;
				Dist = FMath::Min(Dist, Segment(Mx, py, 40.f, Y0, 24.f, Y0 + 8.f, 3.f));
				Dist = FMath::Min(Dist, Segment(Mx, py, 24.f, Y0 + 8.f, 18.f, Y0 + 15.f, 2.6f));
			}
			return Dist;
		};
		const auto Eyes = [](float px, float py)
		{
			return FMath::Min(Circle(px, py, 51.f, 52.f, 7.5f), Circle(px, py, 77.f, 52.f, 7.5f));
		};
		const auto Antenna = [](float px, float py)
		{
			return FMath::Min(Segment(px, py, 64.f, 60.f, 64.f, 26.f, 2.2f), Circle(px, py, 64.f, 20.f, 8.f));
		};
		const auto All = [&](float px, float py)
		{
			return FMath::Min(FMath::Min(Shell(px, py), Legs(px, py)), FMath::Min(FMath::Min(Claw(px, py, 1.f), Claw(px, py, -1.f)), FMath::Min(Eyes(px, py), Antenna(px, py))));
		};
		Painter.Sticker(All, 6.f);
		// Patas, pinzas y antena por detrás del caparazón.
		Painter.Fill([&](float px, float py) { return Legs(px, py) + 1.6f; }, Outline);
		Painter.Fill(Legs, Hex(0xB02830));
		Painter.Fill([&](float px, float py) { return Antenna(px, py) + 1.6f; }, Hex(0x3A3F4B));
		Painter.Fill([](float px, float py) { return Segment(px, py, 64.f, 60.f, 64.f, 26.f, 2.2f); }, Hex(0xA9B1C2));
		Painter.Fill([](float px, float py) { return Circle(px, py, 64.f, 20.f, 8.f) + 0.f; }, Hex(0xFFE14D));
		Painter.Fill([](float px, float py) { return Circle(px, py, 61.f, 17.f, 2.6f); }, Hex(0xFFFFFF, 0.85f));
		for (const float Mirror : { 1.f, -1.f })
		{
			PaintBody(Painter, [&](float px, float py) { return Claw(px, py, Mirror); }, Outline, RedTop, RedBottom, 18.f, 66.f);
		}
		PaintBody(Painter, Shell, Outline, RedTop, RedBottom, 56.f, 100.f);
		// Manchas del caparazón y ojos.
		Painter.Fill([](float px, float py) { return FMath::Min(Ellipse(px, py, 50.f, 84.f, 5.f, 3.4f), Ellipse(px, py, 78.f, 84.f, 5.f, 3.4f)); }, Hex(0xFFA090, 0.7f));
		Painter.Fill([](float px, float py) { return Ellipse(px, py, 46.f, 68.f, 10.f, 4.f); }, Hex(0xFFFFFF, 0.3f));
		Painter.Fill([&](float px, float py) { return Eyes(px, py) + 1.8f; }, Outline);
		Painter.Fill(Eyes, FLinearColor::White);
		Painter.Fill([](float px, float py) { return FMath::Min(Circle(px, py, 53.f, 52.f, 3.8f), Circle(px, py, 79.f, 52.f, 3.8f)); }, Hex(0x151821));
		return Painter.ToTexture(TEXT("TN_Race_HomingCrab"));
	}

	UTexture2D* PaintGullIcon()
	{
		using namespace TNHUDArt;
		FPainter Painter(IconSize, IconSize);
		const TArray<FVector2f> WingPoints = { { 42.f, 74.f }, { 12.f, 46.f }, { 6.f, 18.f }, { 30.f, 30.f }, { 52.f, 56.f } };
		const TArray<FVector2f> TipPoints = { { 6.f, 18.f }, { 30.f, 30.f }, { 24.f, 40.f }, { 11.f, 36.f } };
		const TArray<FVector2f> BeakPoints = { { 50.f, 66.f }, { 78.f, 66.f }, { 72.f, 88.f }, { 64.f, 94.f }, { 56.f, 88.f } };
		const auto Wing = [&WingPoints](float px, float py)
		{
			const float Mx = FMath::Min(px, IconSize - px);
			return Polygon(Mx, py, WingPoints);
		};
		const auto Tip = [&TipPoints](float px, float py)
		{
			const float Mx = FMath::Min(px, IconSize - px);
			return Polygon(Mx, py, TipPoints);
		};
		const auto Chest = [](float px, float py) { return Ellipse(px, py, 64.f, 98.f, 27.f, 16.f); };
		const auto Head = [](float px, float py) { return Circle(px, py, 64.f, 58.f, 27.f); };
		const auto Beak = [&BeakPoints](float px, float py) { return Polygon(px, py, BeakPoints); };
		const auto All = [&](float px, float py) { return FMath::Min(FMath::Min(Wing(px, py), Chest(px, py)), FMath::Min(Head(px, py), Beak(px, py))); };
		Painter.Sticker(All, 6.f);
		PaintBody(Painter, Wing, Hex(0x2B303B), Hex(0xC9D0DC), Hex(0x8E97A8), 18.f, 74.f);
		Painter.Fill(Tip, Hex(0x2B303B));
		PaintBody(Painter, Chest, Hex(0x3C4457), Hex(0xFFFFFF), Hex(0xD5DAE3), 82.f, 114.f);
		PaintBody(Painter, Head, Hex(0x3C4457), Hex(0xFFFFFF), Hex(0xDDE1EA), 31.f, 85.f);
		PaintBody(Painter, Beak, Hex(0x7A4A0A), Hex(0xFFD24A), Hex(0xF2A81C), 66.f, 94.f);
		Painter.Fill([](float px, float py) { return Segment(px, py, 64.f, 68.f, 64.f, 86.f, 0.9f); }, Hex(0x9A5E0A, 0.7f));
		Painter.Fill([](float px, float py) { return Circle(px, py, 64.f, 89.f, 2.4f); }, Hex(0xE63946));
		// Ojos amarillos y cejas fruncidas: cara de enfadada.
		for (const float Side : { -1.f, 1.f })
		{
			const float Ex = 64.f + Side * 13.f;
			Painter.Fill([=](float px, float py) { return Circle(px, py, Ex, 58.f, 7.2f); }, Hex(0x3C4457));
			Painter.Fill([=](float px, float py) { return Circle(px, py, Ex, 58.f, 5.6f); }, Hex(0xFFD23F));
			Painter.Fill([=](float px, float py) { return Circle(px, py, Ex - Side * 0.8f, 59.f, 2.9f); }, Hex(0x151821));
			Painter.Fill([=](float px, float py) { return Segment(px, py, 64.f + Side * 28.f, 39.f, 64.f + Side * 5.f, 51.f, 3.2f); }, Hex(0x151821));
		}
		return Painter.ToTexture(TEXT("TN_Race_GullStrike"));
	}

	UTexture2D* PaintMineIcon()
	{
		using namespace TNHUDArt;
		FPainter Painter(IconSize, IconSize);
		const auto Base = [](float px, float py) { return Box(px, py, 64.f, 90.f, 48.f, 13.f, 11.f); };
		const auto Dome = [](float px, float py) { return FMath::Max(Ellipse(px, py, 64.f, 80.f, 38.f, 26.f), py - 84.f); };
		const TArray<FVector2f> SpikeL = { { 22.f, 76.f }, { 44.f, 74.f }, { 30.f, 42.f } };
		const TArray<FVector2f> SpikeC = { { 52.f, 66.f }, { 76.f, 66.f }, { 64.f, 26.f } };
		const TArray<FVector2f> SpikeR = { { 84.f, 74.f }, { 106.f, 76.f }, { 98.f, 42.f } };
		const auto Spikes = [&SpikeL, &SpikeC, &SpikeR](float px, float py)
		{
			return FMath::Min(Polygon(px, py, SpikeL), FMath::Min(Polygon(px, py, SpikeC), Polygon(px, py, SpikeR)));
		};
		const auto Light = [](float px, float py) { return Circle(px, py, 64.f, 62.f, 10.f); };
		const auto All = [&](float px, float py)
		{
			return FMath::Min(FMath::Min(Base(px, py), Dome(px, py)), FMath::Min(Spikes(px, py), Light(px, py)));
		};
		Painter.Sticker(All, 6.f);
		PaintBody(Painter, Spikes, Hex(0x2C3320), Hex(0xEEF2DA), Hex(0x9AA486), 22.f, 74.f);
		PaintBody(Painter, Dome, Hex(0x262C14), Hex(0x8FA046), Hex(0x55622A), 60.f, 84.f);
		PaintBody(Painter, Base, Hex(0x262C14), Hex(0x7C8C38), Hex(0x3F4A1B), 77.f, 103.f);
		// Remaches del canto y franja de aviso.
		for (int32 Index = 0; Index < 5; ++Index)
		{
			Painter.Fill([=](float px, float py) { return Circle(px, py, 30.f + Index * 17.f, 92.f, 2.6f); }, Hex(0xC9D2B4));
		}
		Painter.Fill([&](float px, float py) { return FMath::Max(FMath::Abs(py - 84.f) - 1.4f, Base(px, py) + 2.f); }, Hex(0xFFD23F, 0.85f));
		// Luz roja con su resplandor.
		Painter.Fill([](float px, float py) { return Circle(px, py, 64.f, 62.f, 15.f); }, Hex(0xFF2A2A, 0.28f));
		Painter.Fill([&](float px, float py) { return Light(px, py) + 1.8f; }, Hex(0x3A0A0A));
		Painter.Layer(Light, [](float px, float py) { return Mix(Hex(0xFF8A7A), Hex(0xD01818), FMath::Clamp(Circle(px, py, 61.f, 58.f, 0.f) / 14.f, 0.f, 1.f)); });
		Painter.Fill([](float px, float py) { return Circle(px, py, 60.f, 57.f, 2.8f); }, Hex(0xFFFFFF, 0.85f));
		return Painter.ToTexture(TEXT("TN_Race_SandMine"));
	}

	UTexture2D* PaintStormIcon()
	{
		using namespace TNHUDArt;
		FPainter Painter(IconSize, IconSize);
		const auto Cloud = [](float px, float py)
		{
			float Dist = Circle(px, py, 40.f, 54.f, 21.f);
			Dist = FMath::Min(Dist, Circle(px, py, 66.f, 40.f, 28.f));
			Dist = FMath::Min(Dist, Circle(px, py, 92.f, 54.f, 20.f));
			return FMath::Min(Dist, Box(px, py, 66.f, 62.f, 41.f, 13.f, 12.f));
		};
		const TArray<FVector2f> Bolt = { { 72.f, 66.f }, { 52.f, 92.f }, { 65.f, 92.f }, { 56.f, 116.f }, { 88.f, 82.f }, { 74.f, 82.f }, { 86.f, 66.f } };
		const auto BoltSdf = [&Bolt](float px, float py) { return Polygon(px, py, Bolt); };
		Painter.Sticker([&](float px, float py) { return FMath::Min(Cloud(px, py), BoltSdf(px, py)); }, 5.f);
		PaintBody(Painter, Cloud, Hex(0x1B1F2E), Hex(0x8C94AC), Hex(0x2C3145), 12.f, 76.f);
		// Panza más oscura y brillo arriba.
		Painter.Fill([&](float px, float py) { return FMath::Max(Ellipse(px, py, 66.f, 74.f, 40.f, 8.f), Cloud(px, py) + 1.f); }, Hex(0x151826, 0.35f));
		Painter.Fill([&](float px, float py) { return FMath::Max(Ellipse(px, py, 58.f, 26.f, 12.f, 5.f), Cloud(px, py) + 2.f); }, Hex(0xFFFFFF, 0.28f));
		Painter.Fill([&](float px, float py) { return BoltSdf(px, py) - 1.8f; }, Hex(0x3B2A08));
		Painter.Layer(BoltSdf, [](float, float py) { return Mix(Hex(0xFFF07A), Hex(0xFFB818), (py - 66.f) / 50.f); });
		return Painter.ToTexture(TEXT("TN_Race_StormCloud"));
	}

	UTexture2D* PaintFrisbeeIcon()
	{
		using namespace TNHUDArt;
		FPainter Painter(IconSize, IconSize);
		// Disco visto en perspectiva: el canto (elipse desplazada hacia abajo) y la cara con los anillos.
		const auto Edge = [](float px, float py) { return Ellipse(px, py, 64.f, 74.f, 52.f, 27.f); };
		const auto Face = [](float px, float py) { return Ellipse(px, py, 64.f, 64.f, 52.f, 27.f); };
		const auto All = [&](float px, float py) { return FMath::Min(Edge(px, py), Face(px, py)); };
		Painter.Sticker(All, 6.f);
		Painter.Fill([&](float px, float py) { return Edge(px, py) - 0.f; }, Hex(0x13233B));
		Painter.Layer([&](float px, float py) { return Edge(px, py) + 2.f; }, [](float, float py) { return Mix(Hex(0xF2B01E), Hex(0xB8720A), (py - 60.f) / 40.f); });
		Painter.Fill([&](float px, float py) { return Face(px, py) + 0.f; }, Hex(0x13233B));
		// Anillos concéntricos de colores.
		const FLinearColor Rings[5] = { Hex(0x9B5DE5), Hex(0x2EC4B6), Hex(0xFFF5DC), Hex(0xFF6A52), Hex(0xFFCB3D) };
		const float Scales[5] = { 0.95f, 0.78f, 0.6f, 0.42f, 0.24f };
		for (int32 Index = 0; Index < 5; ++Index)
		{
			const float Scale = Scales[Index];
			Painter.Fill([=](float px, float py) { return Ellipse(px, py, 64.f, 64.f, 52.f * Scale, 27.f * Scale) + (Index == 0 ? 2.f : 0.f); }, Rings[Index]);
		}
		Painter.Fill([](float px, float py) { return Ellipse(px, py, 44.f, 52.f, 14.f, 4.f); }, Hex(0xFFFFFF, 0.5f));
		return Painter.ToTexture(TEXT("TN_Race_Frisbee"));
	}

	UTexture2D* PaintWhistleIcon()
	{
		using namespace TNHUDArt;
		FPainter Painter(IconSize, IconSize);
		const TArray<FVector2f> TubePoints = { { 58.f, 66.f }, { 112.f, 72.f }, { 112.f, 92.f }, { 58.f, 100.f } };
		const auto Tube = [&TubePoints](float px, float py) { return Polygon(px, py, TubePoints); };
		const auto Drum = [](float px, float py) { return Circle(px, py, 50.f, 86.f, 30.f); };
		const auto Loop = [](float px, float py) { return FMath::Abs(Circle(px, py, 30.f, 34.f, 17.f)) - 2.6f; };
		const auto Ring = [](float px, float py) { return FMath::Abs(Circle(px, py, 36.f, 60.f, 8.f)) - 2.6f; };
		const auto All = [&](float px, float py)
		{
			return FMath::Min(FMath::Min(Tube(px, py), Drum(px, py)), FMath::Min(Loop(px, py), Ring(px, py)));
		};
		Painter.Sticker(All, 6.f);
		// Cordón y anilla por detrás, luego el latón.
		Painter.Fill([&](float px, float py) { return Loop(px, py) + 1.4f; }, Hex(0x5C0F16));
		Painter.Fill(Loop, Hex(0xE63946));
		Painter.Fill([&](float px, float py) { return Ring(px, py) + 1.4f; }, Hex(0x3C4457));
		Painter.Fill(Ring, Hex(0xD5DAE3));
		PaintBody(Painter, Tube, Hex(0x5A3F0A), Hex(0xFFE28A), Hex(0xC68F1E), 66.f, 100.f);
		PaintBody(Painter, Drum, Hex(0x5A3F0A), Hex(0xFFE28A), Hex(0xC08A1C), 56.f, 116.f);
		// Boca de la boquilla, ventana de aire y la bolita dentro de la cámara.
		Painter.Fill([](float px, float py) { return Ellipse(px, py, 111.f, 82.f, 3.f, 8.f); }, Hex(0x2A1A05));
		const TArray<FVector2f> WindowPoints = { { 74.f, 68.f }, { 92.f, 70.f }, { 96.f, 78.f }, { 76.f, 78.f } };
		Painter.Fill([&](float px, float py) { return Polygon(px, py, WindowPoints); }, Hex(0x2A1A05));
		Painter.Fill([](float px, float py) { return Circle(px, py, 50.f, 86.f, 18.f) + 0.f; }, Hex(0x5A3F0A));
		Painter.Fill([](float px, float py) { return Circle(px, py, 50.f, 86.f, 16.f); }, Hex(0x2A1A05));
		Painter.Layer([](float px, float py) { return Circle(px, py, 50.f, 86.f, 11.f); },
			[](float px, float py) { return Mix(Hex(0xFFFFFF), Hex(0xB9C0CE), FMath::Clamp(Circle(px, py, 46.f, 82.f, 0.f) / 14.f, 0.f, 1.f)); });
		Painter.Fill([](float px, float py) { return Ellipse(px, py, 40.f, 66.f, 9.f, 4.f); }, Hex(0xFFFFFF, 0.5f));
		return Painter.ToTexture(TEXT("TN_Race_Whistle"));
	}

	/** Pinta el icono de Kind (128x128). */
	UTexture2D* PaintIcon(ETNRaceItem Kind)
	{
		switch (Kind)
		{
			case ETNRaceItem::Box:
				return PaintBoxIcon();
			case ETNRaceItem::Coconut:
				return PaintCoconutIcon(false);
			case ETNRaceItem::TripleCoconut3:
				return PaintTripleIcon(3);
			case ETNRaceItem::TripleCoconut2:
				return PaintTripleIcon(2);
			case ETNRaceItem::TripleCoconut1:
				return PaintTripleIcon(1);
			case ETNRaceItem::GoldenCoconut:
				return PaintCoconutIcon(true);
			case ETNRaceItem::PelicanTaxi:
				return PaintPelicanIcon();
			case ETNRaceItem::Sunscreen:
				return PaintSunscreenIcon();
			case ETNRaceItem::HomingCrab:
				return PaintCrabIcon();
			case ETNRaceItem::GullStrike:
				return PaintGullIcon();
			case ETNRaceItem::SandMine:
				return PaintMineIcon();
			case ETNRaceItem::StormCloud:
				return PaintStormIcon();
			case ETNRaceItem::Frisbee:
				return PaintFrisbeeIcon();
			case ETNRaceItem::Whistle:
				return PaintWhistleIcon();
			default:
				return nullptr;
		}
	}

	/** true si esta máquina no dibuja nada (servidor dedicado o sin motor gráfico). */
	bool IsHeadless()
	{
		return IsRunningDedicatedServer() || !FApp::CanEverRender();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// API
// ─────────────────────────────────────────────────────────────────────────────

bool TNRaceItemArt::GetHeldLook(ETNRaceItem Kind, FHeldLook& OutLook)
{
	OutLook = FHeldLook();
	if (TNRaceItemArtDetail::IsHeadless() || Kind == ETNRaceItem::None || Kind >= ETNRaceItem::Count)
	{
		return false;
	}
	UStaticMesh* Mesh = TNRaceItemArtDetail::GetMesh(Kind);
	if (!Mesh)
	{
		return false;
	}
	OutLook.Mesh = Mesh;
	OutLook.Scale = FVector::OneVector;
	// El disco se pone de canto en la mano para que se lea como un disco; en el suelo se ve plano.
	OutLook.Rotation = Kind == ETNRaceItem::Frisbee ? FRotator(TNRaceItemArtDetail::FrisbeeHandPitch, 0.0, 0.0) : FRotator::ZeroRotator;
	return true;
}

UTexture2D* TNRaceItemArt::GetIcon(ETNRaceItem Kind)
{
	if (TNRaceItemArtDetail::IsHeadless() || Kind == ETNRaceItem::None || Kind >= ETNRaceItem::Count)
	{
		return nullptr;
	}
	const FString KeyText = FString::Printf(TEXT("RaceItem_%s"), *TNRaceItems::CodeName(Kind));
	return TNHUDArt::Cached(FName(*KeyText), [Kind]() -> UTexture2D* { return TNRaceItemArtDetail::PaintIcon(Kind); });
}
