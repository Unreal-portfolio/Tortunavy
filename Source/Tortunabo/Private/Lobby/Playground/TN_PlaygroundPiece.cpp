#include "Lobby/Playground/TN_PlaygroundPiece.h"
#include "Multiplayer/TN_LocalViews.h"
#include "Lobby/Playground/TN_PlaygroundSynthComponent.h"
#include "Art/TN_Art.h"
#include "Core/TN_Log.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "TN_PlaygroundMeshKit.h"

/**
 * Geometría de las piezas del parque de pruebas. Cada constructor rellena la malla (buffers de TNProcMesh) y las
 * colisiones convexas con las mismas medidas; el origen de todas está en el suelo.
 */
namespace TNPlaygroundPieceDetail
{
	using FBuffers = TNPlaygroundKit::FBuffers;
	using FHulls = TArray<TArray<FVector>>;

	/**
	 * Túnel: medio ancho de dentro, grosor de las paredes, techo de colisión, bóveda (lados y clave), bocas y cima. Se
	 * pasa de pie (la tortuga mide 140 con 34 de radio): techo a 178 y 184 de ancho.
	 */
	constexpr double TunnelInnerHW = 92.0;
	constexpr double TunnelWallT = 40.0;
	constexpr double TunnelRoofBottom = 178.0;
	constexpr double TunnelVaultSide = 140.0;
	constexpr double TunnelVaultTop = 190.0;
	constexpr double TunnelMouthSide = 165.0;
	constexpr double TunnelMouthTop = 225.0;
	constexpr double TunnelFunnel = 45.0;
	constexpr double TunnelTopZ = 262.0;
	/** Barra giratoria: cubo del centro. */
	constexpr double HubRadius = 40.0;
	constexpr double HubTopRadius = 32.0;
	constexpr double HubHeight = 50.0;
	/** Poste: boca y culo de cada cubo. */
	constexpr double BucketRimR = 62.0;
	constexpr double BucketBaseR = 49.0;
	/** Galleta grande y columna. */
	constexpr double CookieThick = 17.0;
	constexpr double CookieColumnR = 52.0;
	/** Segmentos del contorno de las galletas. */
	constexpr int32 CookieSeg = 30;
	/** Tobogán: plataforma de la charnela, medio ancho en la charnela y altura del labio. */
	constexpr double SlidePlatformDepth = 90.0;
	constexpr double SlideHingeHalf = 44.0;
	constexpr double SlideLip = 4.0;
	constexpr double StepRun = 45.0;
	constexpr double MaxStepRise = 40.0;

	FLinearColor TintColor(ETNPlaygroundTint Which, float Shine = 0.14f)
	{
		return TNPlaygroundKit::ToyColor(static_cast<int32>(Which), Shine);
	}

	// ── Arte (Docs/Arte_Assets.md) ───────────────────────────────────────────
	// Cada pieza de arte se modela con las medidas por defecto de ATN_PlaygroundPiece (las de la tabla) y se estira a las de
	// cada copia. Sin sustituto, nada de esto se ve: la malla generada sale igual que siempre.
	constexpr double RefPostHeight = 180.0;
	constexpr int32 RefStepCount = 3;
	constexpr double RefStepHeight = 36.0;
	constexpr double RefStepDepth = 60.0;
	constexpr double RefStepWidth = 120.0;
	constexpr double RefSlideHeight = 160.0;
	constexpr double RefSlideAngle = 46.0;
	constexpr double RefSlideWidth = 190.0;
	constexpr double RefPlatformHeight = 140.0;
	constexpr double RefCookieRadius = 115.0;
	constexpr double RefTunnelLength = 220.0;
	constexpr double RefArmLength = 230.0;
	constexpr double RefArmHeight = 30.0;

	/** Pieza de arte del cuerpo (la malla que no gira). Variantes de otra forma, otro nombre: la rampa sin escalera. */
	FName BodyArtSlot(ETNPlaygroundPieceType Type, bool bSlideSteps)
	{
		switch (Type)
		{
		case ETNPlaygroundPieceType::PopsicleSteps: return TN_ART("Lobby.Playground.PopsicleSteps");
		case ETNPlaygroundPieceType::ShellSlide:
			return bSlideSteps ? TN_ART("Lobby.Playground.ShellSlide") : TN_ART("Lobby.Playground.ShellSlideNoSteps");
		case ETNPlaygroundPieceType::CookiePlatform: return TN_ART("Lobby.Playground.CookiePlatform");
		case ETNPlaygroundPieceType::CastleTunnel: return TN_ART("Lobby.Playground.CastleTunnel");
		case ETNPlaygroundPieceType::SpadeSpinner: return TN_ART("Lobby.Playground.SpadeSpinnerHub");
		default: return TN_ART("Lobby.Playground.BucketPost");
		}
	}

	/** Pieza de arte de las palas que giran: una por número de brazos (la forma cambia). */
	FName SpinArtSlot(int32 Arms)
	{
		switch (Arms)
		{
		case 1: return TN_ART("Lobby.Playground.SpadeSpinnerArms1");
		case 3: return TN_ART("Lobby.Playground.SpadeSpinnerArms3");
		default: return TN_ART("Lobby.Playground.SpadeSpinnerArms2");
		}
	}

	/** Escala de la malla de arte del cuerpo para las medidas de esta copia respecto a las de referencia. */
	FVector BodyArtScale(const ATN_PlaygroundPiece& Piece)
	{
		switch (Piece.PieceType)
		{
		case ETNPlaygroundPieceType::PopsicleSteps:
		{
			const double Count = FMath::Clamp(Piece.StepCount, 1, 5);
			return FVector((Count * Piece.StepDepth) / (RefStepCount * RefStepDepth), Piece.StepWidth / RefStepWidth,
				(Count * Piece.StepHeight) / (RefStepCount * RefStepHeight));
		}
		case ETNPlaygroundPieceType::ShellSlide:
		{
			// Largo de la rampa (por la pendiente), medio ancho del labio y alto de la charnela.
			const auto RunFor = [](double Height, double AngleDeg) { return (Height - SlideLip) / FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(AngleDeg, 20.0, 60.0))); };
			const auto LipHalfFor = [](double Width) { return FMath::Max(SlideHingeHalf + 10.0, Width * 0.5); };
			return FVector(RunFor(Piece.SlideHeight, Piece.SlideAngle) / RunFor(RefSlideHeight, RefSlideAngle), LipHalfFor(Piece.SlideWidth) / LipHalfFor(RefSlideWidth),
				Piece.SlideHeight / RefSlideHeight);
		}
		case ETNPlaygroundPieceType::CookiePlatform:
			return FVector(Piece.CookieRadius / RefCookieRadius, Piece.CookieRadius / RefCookieRadius, Piece.PlatformHeight / RefPlatformHeight);
		case ETNPlaygroundPieceType::CastleTunnel:
			return FVector(Piece.TunnelLength / RefTunnelLength, 1.0, 1.0);
		case ETNPlaygroundPieceType::SpadeSpinner:
			return FVector::OneVector;
		default:
			return FVector(1.0, 1.0, Piece.PostHeight / RefPostHeight);
		}
	}

	/** Escala de la malla de arte de las palas: largo del brazo y altura del mango. */
	FVector SpinArtScale(const ATN_PlaygroundPiece& Piece)
	{
		const double Length = Piece.ArmLength / RefArmLength;
		return FVector(Length, Length, (Piece.ArmHeight + 12.0) / (RefArmHeight + 12.0));
	}

	/** Solo simulan el movimiento de un personaje el servidor y el cliente que lo controla. */
	bool SimulatesMovement(const APawn* Pawn)
	{
		return Pawn && (Pawn->IsLocallyControlled() || Pawn->HasAuthority());
	}

	/** Bloque de arena con la tapa más clara, marcas de cubo en los lados y su colisión. */
	void AddSandBlock(FBuffers& B, FHulls& Hulls, const FVector& Center, const FVector& Half)
	{
		TNPlaygroundKit::AddAxisBox(B, Center, Half, TNPlaygroundKit::Rgb(0xE8C889));
		TNPlaygroundKit::AddAxisBox(B, Center + FVector(0.0, 0.0, Half.Z + 1.2), FVector(Half.X - 6.0, Half.Y - 6.0, 1.2), TNPlaygroundKit::Rgb(0xF6E0AE));
		for (double Z = 55.0; Z < Half.Z * 2.0 - 25.0; Z += 70.0)
		{
			TNPlaygroundKit::AddAxisBox(B, FVector(Center.X, Center.Y, Center.Z - Half.Z + Z), FVector(Half.X + 3.0, Half.Y + 3.0, 5.0), TNPlaygroundKit::Rgb(0xD2AC6A));
		}
		Hulls.Add(TNPlaygroundKit::HullAxisBox(Center, Half));
	}

	/** Escalera de arena que baja de Height al suelo empezando en StartX y avanzando en la dirección Dir (+1 o -1) de X. */
	void AddSandSteps(FBuffers& B, FHulls& Hulls, double StartX, double Dir, double Height, double HalfWidth)
	{
		const int32 Count = FMath::Max(2, FMath::CeilToInt32(Height / MaxStepRise));
		const double Rise = Height / Count;
		for (int32 k = 0; k + 1 < Count; ++k)
		{
			const double Top = Height - (k + 1) * Rise;
			const double X0 = StartX + Dir * k * StepRun;
			AddSandBlock(B, Hulls, FVector(X0 + Dir * StepRun * 0.5, 0.0, Top * 0.5), FVector(StepRun * 0.5, HalfWidth, Top * 0.5));
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Poste de cubos
	// ─────────────────────────────────────────────────────────────────────────

	void BuildBucketPost(FBuffers& B, FHulls& Hulls, double Height, const FLinearColor& ColA, const FLinearColor& ColB)
	{
		const int32 Count = FMath::Max(1, FMath::RoundToInt32(Height / 66.0));
		const double Each = Height / Count;
		const FLinearColor HandleCol = TNPlaygroundKit::Rgb(0xFFF6E0, 0.2f);
		for (int32 k = 0; k < Count; ++k)
		{
			const double Z0 = k * Each;
			const double Z1 = Z0 + Each;
			const double SkirtZ = Z0 + 7.0;
			const FLinearColor Plastic = (k % 2 == 0) ? ColA : ColB;
			const FLinearColor RimTone = TNPlaygroundKit::Shade(Plastic, 1.12);
			const FLinearColor RibTone = TNPlaygroundKit::Shade(Plastic, 0.9);
			// Cubo liso boca abajo: la boca (ancha) abajo y el culo (estrecho) arriba.
			TNPlaygroundKit::AddFrustum(B, FVector(0.0, 0.0, SkirtZ), FVector(0.0, 0.0, Z1), BucketRimR - 3.0, BucketBaseR, 28, Plastic,
				TNPlaygroundKit::Shade(Plastic, 1.06), false, k == Count - 1);
			// Reborde enrollado de la boca (con su cara de abajo en los que asoman sobre el cubo anterior).
			TNPlaygroundKit::AddFrustum(B, FVector(0.0, 0.0, Z0), FVector(0.0, 0.0, SkirtZ + 1.0), BucketRimR + 1.5, BucketRimR + 1.5, 28, RimTone, RimTone, k > 0, false);
			TNPlaygroundKit::AddAnnulus(B, FVector(0.0, 0.0, SkirtZ + 1.0), FVector::UpVector, BucketRimR - 3.0, BucketRimR + 1.5, 28, RimTone);
			// Dos nervios.
			for (const double Frac : { 0.38, 0.68 })
			{
				const double Zr = FMath::Lerp(SkirtZ, Z1, Frac);
				const double Rr = FMath::Lerp(BucketRimR - 3.0, BucketBaseR, Frac) + 1.2;
				TNPlaygroundKit::AddFrustum(B, FVector(0.0, 0.0, Zr - 1.8), FVector(0.0, 0.0, Zr + 1.8), Rr, Rr, 28, RibTone, RibTone, false, false);
			}
			// Asa: arco pegado al costado que cuelga de sus dos ejes; cada cubo la tiene girada.
			const double AxleZ = FMath::Lerp(SkirtZ, Z1, 0.7);
			const double AxleR = FMath::Lerp(BucketRimR - 3.0, BucketBaseR, 0.7);
			const double Turn = TNPlaygroundKit::KitPi * (0.25 + 0.5 * k);
			TArray<FVector> Path;
			TArray<double> Radii;
			TArray<FLinearColor> Colors;
			constexpr int32 HandleSteps = 12;
			for (int32 i = 0; i <= HandleSteps; ++i)
			{
				const double T = static_cast<double>(i) / HandleSteps;
				const double A = Turn + TNPlaygroundKit::KitPi * T;
				const double Z = AxleZ - Each * 0.42 * FMath::Sin(TNPlaygroundKit::KitPi * T);
				const double Along = FMath::Clamp((Z - SkirtZ) / FMath::Max(1.0, Z1 - SkirtZ), 0.0, 1.0);
				const double R = FMath::Lerp(BucketRimR - 3.0, BucketBaseR, Along) + 4.0;
				Path.Add(FVector(R * FMath::Cos(A), R * FMath::Sin(A), Z));
				Radii.Add(2.2);
				Colors.Add(HandleCol);
			}
			TNPlaygroundKit::AddTube(B, Path, Radii, 6, Colors, FVector::UpVector, false);
			for (const double Ang : { Turn, Turn + TNPlaygroundKit::KitPi })
			{
				const FVector Dir(FMath::Cos(Ang), FMath::Sin(Ang), 0.0);
				TNPlaygroundKit::AddFrustum(B, Dir * (AxleR - 1.0) + FVector(0.0, 0.0, AxleZ), Dir * (AxleR + 5.0) + FVector(0.0, 0.0, AxleZ), 3.6, 3.6, 8,
					HandleCol, HandleCol, false, true);
			}
			Hulls.Add(TNPlaygroundKit::HullCylinder(FVector(0.0, 0.0, Z0), Each, BucketRimR - 1.0, BucketBaseR, 14));
		}
		// En la cima (el culo del último cubo), una estrella de mar.
		TNPlaygroundKit::AddStarfish(B, FVector(0.0, 0.0, Height + 0.3), FVector::UpVector, FVector::ForwardVector, 26.0, 3.0, TNPlaygroundKit::Rgb(0xFFB077));
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Escalón de palitos de helado
	// ─────────────────────────────────────────────────────────────────────────

	/** Palito de madera clara o, uno de cada cinco más o menos, teñido de un color pastel. */
	FLinearColor StickColor(int32 Index, uint32 Seed)
	{
		if (TNPlaygroundKit::Hash01(Index, 5, Seed) < 0.22)
		{
			static const uint32 Dyed[4] = { 0xFF9EC2, 0x84E3C4, 0x92D2FF, 0xFFE680 };
			return TNPlaygroundKit::Rgb(Dyed[Index % 4], 0.08f);
		}
		return TNPlaygroundKit::Shade(TNPlaygroundKit::Rgb(0xEBC98F), 0.9 + 0.18 * TNPlaygroundKit::Hash01(Index, 6, Seed));
	}

	/** Filas de palitos tumbados (a lo largo de AlongDir) que forran una cara vertical de normal OutDir entre Z0 y Z1. */
	void AddStickRows(FBuffers& B, const FVector& FaceCenter, const FVector& AlongDir, const FVector& OutDir, double Length, double Z0, double Z1, int32& StickIndex,
		uint32 Seed)
	{
		constexpr double StickT = 2.6;
		const double Tall = Z1 - Z0;
		if (Tall < 4.0)
		{
			return;
		}
		const int32 Rows = FMath::Max(1, FMath::RoundToInt32(Tall / 13.0));
		const double RowH = Tall / Rows;
		for (int32 r = 0; r < Rows; ++r)
		{
			const FVector Center = FVector(FaceCenter.X, FaceCenter.Y, Z0 + (r + 0.5) * RowH) + OutDir * (StickT * 0.5 + 0.2);
			TNPlaygroundKit::AddStick(B, Center, AlongDir, FVector::UpVector, Length, RowH - 1.0, StickT, StickColor(StickIndex++, Seed));
		}
	}

	void BuildPopsicleSteps(FBuffers& B, FHulls& Hulls, int32 Steps, double Rise, double Depth, double Width, uint32 Seed)
	{
		constexpr double StickT = 2.6;
		const FLinearColor CoreTone = TNPlaygroundKit::Rgb(0xB88B55);
		const double HalfW = Width * 0.5;
		int32 StickIndex = 0;
		for (int32 k = 0; k < Steps; ++k)
		{
			const double X0 = k * Depth;
			const double X1 = X0 + Depth;
			const double Top = (k + 1) * Rise;
			const FVector Center((X0 + X1) * 0.5, 0.0, Top * 0.5);
			Hulls.Add(TNPlaygroundKit::HullAxisBox(Center, FVector(Depth * 0.5, HalfW, Top * 0.5)));
			// Núcleo de madera oscura algo metido: tapa las rendijas entre palitos.
			TNPlaygroundKit::AddAxisBox(B, Center, FVector(Depth * 0.5 - StickT, HalfW - StickT, Top * 0.5 - 0.5), CoreTone);
			// Tapa: palitos de lado a lado, uno junto a otro a lo largo del escalón.
			const int32 TopSticks = FMath::Max(2, FMath::RoundToInt32(Depth / 13.0));
			const double Pitch = Depth / TopSticks;
			for (int32 s = 0; s < TopSticks; ++s)
			{
				TNPlaygroundKit::AddStick(B, FVector(X0 + (s + 0.5) * Pitch, 0.0, Top - StickT * 0.5), FVector::RightVector, FVector::ForwardVector, Width + 6.0,
					Pitch - 1.0, StickT, StickColor(StickIndex++, Seed));
			}
			// Contrahuella: la parte de la cara de delante (-X) que asoma sobre el escalón anterior.
			AddStickRows(B, FVector(X0, 0.0, 0.0), FVector::RightVector, -FVector::ForwardVector, Width + 4.0, k * Rise, Top, StickIndex, Seed);
			// Costados.
			for (const double SideY : { -1.0, 1.0 })
			{
				AddStickRows(B, FVector((X0 + X1) * 0.5, SideY * HalfW, 0.0), FVector::ForwardVector, FVector(0.0, SideY, 0.0), Depth + 2.0, 0.0, Top, StickIndex, Seed);
			}
			// Fondo del último escalón.
			if (k == Steps - 1)
			{
				AddStickRows(B, FVector(X1, 0.0, 0.0), FVector::RightVector, FVector::ForwardVector, Width + 4.0, 0.0, Top, StickIndex, Seed);
			}
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Rampa de concha
	// ─────────────────────────────────────────────────────────────────────────

	/** Resultado del tobogán que necesita el actor: la zona de empuje y la dirección cuesta abajo. */
	struct FSlideZone
	{
		FVector Center = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity;
		FVector Half = FVector(50.0);
		FVector Down = FVector::ForwardVector;
	};

	void BuildShellSlide(FBuffers& B, FHulls& Hulls, double Height, double AngleDeg, double Width, bool bSteps, const FLinearColor& ShellTone, FSlideZone& OutZone)
	{
		const double Angle = FMath::DegreesToRadians(FMath::Clamp(AngleDeg, 20.0, 60.0));
		const double Run = (Height - SlideLip) / FMath::Tan(Angle);
		const double LipHalf = FMath::Max(SlideHingeHalf + 10.0, Width * 0.5);
		const auto HalfAt = [LipHalf](double A) { return FMath::Lerp(SlideHingeHalf, LipHalf, A); };
		// Cara de dentro (nácar), de la charnela (A = 0) al labio (A = 1) y de lado a lado (Across = -1..1): canal suave,
		// nueve costillas apenas marcadas y el labio festoneado.
		const auto InnerAt = [&HalfAt, Run, Height](double A, double Across)
		{
			const double Rib = FMath::Cos(Across * 9.0 * TNPlaygroundKit::KitPi) * 0.9 * A;
			const double Lobe = FMath::Pow(A, 6.0) * 8.0 * FMath::Square(FMath::Cos(Across * 4.5 * TNPlaygroundKit::KitPi));
			return FVector(A * Run + Lobe, Across * HalfAt(A), SlideLip + (Height - SlideLip) * (1.0 - A) + 9.0 * Across * Across + Rib);
		};
		const auto UnderAt = [&InnerAt](double A, double Across)
		{
			return InnerAt(A, Across) - FVector(0.0, 0.0, 6.0 + 3.0 * (1.0 - Across * Across));
		};
		const auto InnerNormal = [&InnerAt](double A, double Across)
		{
			constexpr double Da = 1e-3;
			constexpr double Db = 1e-3;
			FVector N = FVector::CrossProduct(InnerAt(A + Da, Across) - InnerAt(A - Da, Across), InnerAt(A, Across + Db) - InnerAt(A, Across - Db));
			if (!N.Normalize())
			{
				N = FVector::UpVector;
			}
			return N.Z < 0.0 ? -N : N;
		};
		const FLinearColor NacreA = TNPlaygroundKit::Rgb(0xFFE4EE, 0.5f);
		const FLinearColor NacreB = TNPlaygroundKit::Rgb(0xFFF4E6, 0.5f);
		const FLinearColor Iris = TNPlaygroundKit::Rgb(0xEEE0FF, 0.5f);
		constexpr int32 NA = 18;
		constexpr int32 NB = 18;
		for (int32 i = 0; i < NA; ++i)
		{
			for (int32 j = 0; j < NB; ++j)
			{
				const double A0 = static_cast<double>(i) / NA;
				const double A1 = static_cast<double>(i + 1) / NA;
				const double B0 = -1.0 + 2.0 * j / NB;
				const double B1 = -1.0 + 2.0 * (j + 1) / NB;
				const bool bStripe = (j / 2) % 2 == 1;
				const FVector P[4] = { InnerAt(A0, B0), InnerAt(A0, B1), InnerAt(A1, B1), InnerAt(A1, B0) };
				const FVector N[4] = { InnerNormal(A0, B0), InnerNormal(A0, B1), InnerNormal(A1, B1), InnerNormal(A1, B0) };
				const FLinearColor Col0 = TNPlaygroundKit::Mix(TNPlaygroundKit::Mix(NacreA, NacreB, A0), Iris, bStripe ? 0.35 : 0.0);
				const FLinearColor Col1 = TNPlaygroundKit::Mix(TNPlaygroundKit::Mix(NacreA, NacreB, A1), Iris, bStripe ? 0.35 : 0.0);
				const FLinearColor C[4] = { Col0, Col0, Col1, Col1 };
				TNPlaygroundKit::SmoothQuad(B, P, N, C);
				// Cara de fuera (costillas de color por debajo), hacia abajo.
				const FVector Q[4] = { UnderAt(A0, B0), UnderAt(A0, B1), UnderAt(A1, B1), UnderAt(A1, B0) };
				const FVector Down[4] = { -N[0], -N[1], -N[2], -N[3] };
				TNPlaygroundKit::SmoothQuad1(B, Q, Down, bStripe ? TNPlaygroundKit::Shade(ShellTone, 1.18) : ShellTone);
			}
		}
		// Canto de la concha: une la cara de dentro con la de fuera por los costados, el labio y la charnela.
		const auto EdgeBand = [&B, &InnerAt, &UnderAt, &ShellTone](double A0, double B0, double A1, double B1, const FVector& OutHint)
		{
			B.AddQuad(InnerAt(A0, B0), InnerAt(A1, B1), UnderAt(A1, B1), UnderAt(A0, B0), OutHint, TNPlaygroundKit::Shade(ShellTone, 1.08));
		};
		for (int32 i = 0; i < NA; ++i)
		{
			const double A0 = static_cast<double>(i) / NA;
			const double A1 = static_cast<double>(i + 1) / NA;
			EdgeBand(A0, -1.0, A1, -1.0, FVector(0.0, -1.0, 0.0));
			EdgeBand(A0, 1.0, A1, 1.0, FVector(0.0, 1.0, 0.0));
		}
		for (int32 j = 0; j < NB; ++j)
		{
			const double B0 = -1.0 + 2.0 * j / NB;
			const double B1 = -1.0 + 2.0 * (j + 1) / NB;
			EdgeBand(1.0, B0, 1.0, B1, FVector(1.0, 0.0, 0.0));
			EdgeBand(0.0, B0, 0.0, B1, FVector(-1.0, 0.0, 0.0));
		}

		// Montón de arena bajo la concha (algo metido: la concha vuela un poco por los lados).
		{
			const FLinearColor MoundSide = TNPlaygroundKit::Rgb(0xE3C284);
			constexpr int32 Slices = 8;
			for (int32 i = 0; i < Slices; ++i)
			{
				const double A0 = 0.96 * i / Slices;
				const double A1 = 0.96 * (i + 1) / Slices;
				const double Z0 = UnderAt(A0, 0.0).Z - 6.0;
				const double Z1 = UnderAt(A1, 0.0).Z - 6.0;
				for (const double SideY : { -1.0, 1.0 })
				{
					const double Y0 = SideY * (HalfAt(A0) - 14.0);
					const double Y1 = SideY * (HalfAt(A1) - 14.0);
					B.AddQuad(FVector(A0 * Run, Y0, 0.0), FVector(A1 * Run, Y1, 0.0), FVector(A1 * Run, Y1, Z1), FVector(A0 * Run, Y0, Z0), FVector(0.0, SideY, 0.0), MoundSide);
				}
			}
			const double AEnd = 0.96;
			const double ZEnd = UnderAt(AEnd, 0.0).Z - 6.0;
			const double YEnd = HalfAt(AEnd) - 14.0;
			B.AddQuad(FVector(AEnd * Run, -YEnd, 0.0), FVector(AEnd * Run, YEnd, 0.0), FVector(AEnd * Run, YEnd, ZEnd), FVector(AEnd * Run, -YEnd, ZEnd),
				FVector(1.0, 0.0, 0.0), MoundSide);
		}

		// Plataforma de arena de la charnela, almenas en las esquinas de atrás, banderín y escalera.
		const double PlatHalfY = SlideHingeHalf + 26.0;
		AddSandBlock(B, Hulls, FVector(-SlidePlatformDepth * 0.5, 0.0, Height * 0.5), FVector(SlidePlatformDepth * 0.5, PlatHalfY, Height * 0.5));
		for (const double SideY : { -1.0, 1.0 })
		{
			const FVector Merlon(-SlidePlatformDepth + 14.0, SideY * (PlatHalfY - 12.0), Height + 13.0);
			const FVector MerlonHalf(12.0, 11.0, 13.0);
			TNPlaygroundKit::AddAxisBox(B, Merlon, MerlonHalf, TNPlaygroundKit::Rgb(0xEFD29A));
			Hulls.Add(TNPlaygroundKit::HullAxisBox(Merlon, MerlonHalf));
		}
		const FVector PoleFoot(-SlidePlatformDepth + 14.0, PlatHalfY - 12.0, Height + 26.0);
		TNPlaygroundKit::AddRod(B, PoleFoot, PoleFoot + FVector(0.0, 0.0, 95.0), 2.0, 6, TNPlaygroundKit::Rgb(0x7A4E2B), FVector::ForwardVector);
		TNPlaygroundKit::AddPennant(B, PoleFoot + FVector(0.0, 0.0, 93.0), FVector(1.0, 0.0, 0.0), 44.0, 26.0, ShellTone);
		if (bSteps)
		{
			AddSandSteps(B, Hulls, -SlidePlatformDepth, -1.0, Height, PlatHalfY - 10.0);
		}

		// Colisión de la rampa: cuña convexa a la altura del centro del canal (el borde levantado es solo de adorno).
		TArray<FVector> Wedge;
		Wedge.Add(FVector(0.0, -SlideHingeHalf, 0.0));
		Wedge.Add(FVector(0.0, SlideHingeHalf, 0.0));
		Wedge.Add(FVector(0.0, -SlideHingeHalf, Height));
		Wedge.Add(FVector(0.0, SlideHingeHalf, Height));
		Wedge.Add(FVector(Run, -LipHalf, 0.0));
		Wedge.Add(FVector(Run, LipHalf, 0.0));
		Wedge.Add(FVector(Run, -LipHalf, SlideLip));
		Wedge.Add(FVector(Run, LipHalf, SlideLip));
		Hulls.Add(Wedge);

		// Zona de empuje: caja tumbada sobre la pendiente, de la charnela al labio.
		const double SlopeLen = FMath::Sqrt(Run * Run + FMath::Square(Height - SlideLip));
		const FVector SlopeUp(FMath::Sin(Angle), 0.0, FMath::Cos(Angle));
		OutZone.Down = FVector(FMath::Cos(Angle), 0.0, -FMath::Sin(Angle));
		OutZone.Rotation = FRotator(-FMath::RadiansToDegrees(Angle), 0.0, 0.0).Quaternion();
		OutZone.Center = FVector(Run * 0.5, 0.0, (Height + SlideLip) * 0.5) + SlopeUp * 38.0;
		OutZone.Half = FVector(SlopeLen * 0.5 + 10.0, LipHalf, 40.0);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Plataforma de galleta
	// ─────────────────────────────────────────────────────────────────────────

	/** Galleta: disco de canto irregular y algo redondeado, cara de arriba con cúpula Dome y cara de abajo plana. */
	void AddCookieDisc(FBuffers& B, double BaseZ, double Thick, double Radius, const FLinearColor& TopCol, const FLinearColor& SideCol, const FLinearColor& BottomCol,
		double Dome, uint32 Seed)
	{
		constexpr int32 Rings = 5;
		const double TopZ = BaseZ + Thick;
		const auto EdgeR = [Radius, Seed](int32 K) { return Radius * (1.0 + 0.035 * TNProcMesh::TNProcHashNoise(K % CookieSeg, 3, Seed)); };
		const auto TopPoint = [&EdgeR, TopZ, Dome](double F, int32 K)
		{
			const double A = TNPlaygroundKit::KitTwoPi * K / CookieSeg;
			const double Rr = EdgeR(K) * F;
			return FVector(Rr * FMath::Cos(A), Rr * FMath::Sin(A), TopZ + Dome * (1.0 - F * F));
		};
		const auto TopNormal = [&EdgeR, Dome](double F, int32 K)
		{
			const double A = TNPlaygroundKit::KitTwoPi * K / CookieSeg;
			const double Slope = 2.0 * Dome * F / FMath::Max(1.0, EdgeR(K));
			return FVector(Slope * FMath::Cos(A), Slope * FMath::Sin(A), 1.0).GetSafeNormal();
		};
		for (int32 r = 0; r < Rings; ++r)
		{
			const double F0 = static_cast<double>(r) / Rings;
			const double F1 = static_cast<double>(r + 1) / Rings;
			for (int32 k = 0; k < CookieSeg; ++k)
			{
				const FVector P[4] = { TopPoint(F0, k), TopPoint(F0, k + 1), TopPoint(F1, k + 1), TopPoint(F1, k) };
				const FVector N[4] = { TopNormal(F0, k), TopNormal(F0, k + 1), TopNormal(F1, k + 1), TopNormal(F1, k) };
				TNPlaygroundKit::SmoothQuad1(B, P, N, TopCol);
			}
		}
		// Canto redondeado: tres anillos (arriba un poco metido, en medio el más ancho, abajo otra vez metido).
		const double Heights[4] = { TopZ, BaseZ + Thick * 0.62, BaseZ + Thick * 0.28, BaseZ };
		const double Insets[4] = { 0.0, 3.0, 3.0, 0.0 };
		const double Shrink[4] = { 1.0, 1.0, 1.0, 0.96 };
		for (int32 b = 0; b < 3; ++b)
		{
			for (int32 k = 0; k < CookieSeg; ++k)
			{
				const double A0 = TNPlaygroundKit::KitTwoPi * k / CookieSeg;
				const double A1 = TNPlaygroundKit::KitTwoPi * (k + 1) / CookieSeg;
				const FVector E0(FMath::Cos(A0), FMath::Sin(A0), 0.0);
				const FVector E1(FMath::Cos(A1), FMath::Sin(A1), 0.0);
				const double Ra0 = EdgeR(k) * Shrink[b] + Insets[b];
				const double Ra1 = EdgeR(k + 1) * Shrink[b] + Insets[b];
				const double Rb0 = EdgeR(k) * Shrink[b + 1] + Insets[b + 1];
				const double Rb1 = EdgeR(k + 1) * Shrink[b + 1] + Insets[b + 1];
				const FVector P[4] = { E0 * Ra0 + FVector(0.0, 0.0, Heights[b]), E1 * Ra1 + FVector(0.0, 0.0, Heights[b]),
					E1 * Rb1 + FVector(0.0, 0.0, Heights[b + 1]), E0 * Rb0 + FVector(0.0, 0.0, Heights[b + 1]) };
				const FVector N[4] = { E0, E1, E1, E0 };
				TNPlaygroundKit::SmoothQuad1(B, P, N, SideCol);
			}
		}
		TNPlaygroundKit::AddDisc(B, FVector(0.0, 0.0, BaseZ), -FVector::UpVector, Radius * 0.96, CookieSeg, BottomCol);
	}

	void BuildCookie(FBuffers& B, FHulls& Hulls, double Height, double Radius, uint32 Seed)
	{
		const double ColumnTop = FMath::Max(10.0, Height - CookieThick);
		// Columna: galletas de chocolate rellenas de crema, apiladas.
		const FLinearColor Choco = TNPlaygroundKit::Rgb(0x4B3024, 0.06f);
		const FLinearColor ChocoSide = TNPlaygroundKit::Rgb(0x3E271D, 0.06f);
		const FLinearColor Cream = TNPlaygroundKit::Rgb(0xFFF3E2, 0.1f);
		const int32 Layers = FMath::Max(1, FMath::RoundToInt32(ColumnTop / 24.0));
		const double Each = ColumnTop / Layers;
		for (int32 k = 0; k < Layers; ++k)
		{
			const double Z0 = k * Each;
			const double Filling = Each * 0.28;
			const double Wafer = (Each - Filling) * 0.5;
			AddCookieDisc(B, Z0, Wafer, CookieColumnR, Choco, ChocoSide, ChocoSide, 0.0, Seed + 11u * k);
			TNPlaygroundKit::AddFrustum(B, FVector(0.0, 0.0, Z0 + Wafer), FVector(0.0, 0.0, Z0 + Wafer + Filling), CookieColumnR - 5.0, CookieColumnR - 5.0, 24,
				Cream, Cream, false, false);
			AddCookieDisc(B, Z0 + Wafer + Filling, Wafer, CookieColumnR, Choco, ChocoSide, ChocoSide, 0.0, Seed + 11u * k + 5u);
		}
		Hulls.Add(TNPlaygroundKit::HullCylinder(FVector::ZeroVector, ColumnTop, CookieColumnR, CookieColumnR, 14));

		// Galleta grande de arriba con pepitas de chocolate y azúcar.
		const FLinearColor Dough = TNPlaygroundKit::Rgb(0xE3A962);
		const FLinearColor DoughSide = TNPlaygroundKit::Rgb(0xC98A45);
		const FLinearColor DoughBottom = TNPlaygroundKit::Rgb(0xA86A33);
		constexpr double Dome = 2.0;
		AddCookieDisc(B, ColumnTop, CookieThick, Radius, Dough, DoughSide, DoughBottom, Dome, Seed + 101u);
		Hulls.Add(TNPlaygroundKit::HullCylinder(FVector(0.0, 0.0, ColumnTop), CookieThick, Radius - 3.0, Radius - 3.0, 20));
		const FLinearColor Chip = TNPlaygroundKit::Rgb(0x4A2818, 0.25f);
		for (int32 c = 0; c < 14; ++c)
		{
			const double F = 0.82 * FMath::Sqrt(TNPlaygroundKit::Hash01(c, 1, Seed));
			const double A = TNPlaygroundKit::KitTwoPi * TNPlaygroundKit::Hash01(c, 2, Seed);
			const double Spin = TNPlaygroundKit::KitTwoPi * TNPlaygroundKit::Hash01(c, 3, Seed);
			const FVector At(F * Radius * FMath::Cos(A), F * Radius * FMath::Sin(A), Height + Dome * (1.0 - F * F) - 1.2);
			const FVector Ax(FMath::Cos(Spin), FMath::Sin(Spin), 0.0);
			const FVector Ay(-Ax.Y, Ax.X, 0.0);
			const double Grow = 0.8 + 0.5 * TNPlaygroundKit::Hash01(c, 4, Seed);
			TNPlaygroundKit::AddEllipsoid(B, At, Ax, Ay, FVector::UpVector, FVector(6.0, 5.0, 3.2) * Grow, 8, 4, Chip);
		}
		const FLinearColor Sugar = TNPlaygroundKit::Rgb(0xFFFFFF, 0.3f);
		for (int32 g = 0; g < 24; ++g)
		{
			const double F = 0.9 * FMath::Sqrt(TNPlaygroundKit::Hash01(g, 7, Seed));
			const double A = TNPlaygroundKit::KitTwoPi * TNPlaygroundKit::Hash01(g, 8, Seed);
			const double Spin = TNPlaygroundKit::KitTwoPi * TNPlaygroundKit::Hash01(g, 9, Seed);
			const FVector At(F * Radius * FMath::Cos(A), F * Radius * FMath::Sin(A), Height + Dome * (1.0 - F * F) + 0.3);
			const FVector Ax = FVector(FMath::Cos(Spin), FMath::Sin(Spin), 0.0) * 1.2;
			const FVector Ay = FVector(-FMath::Sin(Spin), FMath::Cos(Spin), 0.0) * 1.2;
			B.AddQuad(At - Ax - Ay, At + Ax - Ay, At + Ax + Ay, At - Ax + Ay, FVector::UpVector, Sugar);
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Túnel de castillo
	// ─────────────────────────────────────────────────────────────────────────

	/** Peso de la boca (0 dentro, 1 en la fachada): el techo sube en embudo en los últimos TunnelFunnel cm. */
	double TunnelMouthWeight(double X, double HalfL)
	{
		return TNPlaygroundKit::Smooth01(HalfL - TunnelFunnel, HalfL, FMath::Abs(X));
	}

	/** Altura de la bóveda (dibujo) en (X, Y). */
	double TunnelVaultZ(double X, double Y, double HalfL)
	{
		const double Mouth = TunnelMouthWeight(X, HalfL);
		const double SideZ = FMath::Lerp(TunnelVaultSide, TunnelMouthSide, Mouth);
		const double CrownZ = FMath::Lerp(TunnelVaultTop, TunnelMouthTop, Mouth);
		const double Q = FMath::Clamp(Y / TunnelInnerHW, -1.0, 1.0);
		return SideZ + (CrownZ - SideZ) * FMath::Sqrt(FMath::Max(0.0, 1.0 - Q * Q));
	}

	/** Normal de la bóveda vista desde dentro (hacia abajo y hacia el eje). */
	FVector TunnelVaultNormal(double X, double Y, double HalfL)
	{
		constexpr double H = 0.5;
		const double Dx = (TunnelVaultZ(X + H, Y, HalfL) - TunnelVaultZ(X - H, Y, HalfL)) / (2.0 * H);
		const double Dy = (TunnelVaultZ(X, Y + H, HalfL) - TunnelVaultZ(X, Y - H, HalfL)) / (2.0 * H);
		return FVector(Dx, Dy, -1.0).GetSafeNormal();
	}

	void BuildCastleTunnel(FBuffers& B, FHulls& Hulls, double Length, const FLinearColor& FlagTone, uint32 Seed, FVector& OutZoneCenter, FVector& OutZoneHalf)
	{
		const double HalfL = Length * 0.5;
		const double OuterHW = TunnelInnerHW + TunnelWallT;
		const FLinearColor Wall = TNPlaygroundKit::Rgb(0xE8C889);
		const FLinearColor Crown = TNPlaygroundKit::Rgb(0xF6E0AE);
		const FLinearColor Mark = TNPlaygroundKit::Rgb(0xD2AC6A);
		const FLinearColor Deep = TNPlaygroundKit::Rgb(0xC9A56A);
		const FLinearColor Inside = TNPlaygroundKit::Rgb(0xD9B474);

		// Paredes de fuera, tapa de arriba y marcas de cubo (una tira por pared: por dentro no se ven).
		for (const double SideY : { -1.0, 1.0 })
		{
			const double Y = SideY * OuterHW;
			B.AddQuad(FVector(-HalfL, Y, 0.0), FVector(HalfL, Y, 0.0), FVector(HalfL, Y, TunnelTopZ), FVector(-HalfL, Y, TunnelTopZ), FVector(0.0, SideY, 0.0), Wall);
			for (const double BandZ : { TunnelTopZ * 0.28, TunnelTopZ * 0.65 })
			{
				TNPlaygroundKit::AddAxisBox(B, FVector(0.0, SideY * (OuterHW + 1.5), BandZ), FVector(HalfL + 2.0, 3.0, 5.0), Mark);
			}
			TNPlaygroundKit::AddStarfish(B, FVector(-HalfL * 0.45, SideY * (OuterHW + 0.5), TunnelTopZ * 0.48), FVector(0.0, SideY, 0.0), FVector::UpVector, 22.0, 3.0,
				TNPlaygroundKit::Rgb(0xFF8A70));
			TNPlaygroundKit::AddShellFan(B, FVector(HalfL * 0.4, SideY * (OuterHW + 0.5), TunnelTopZ * 0.41), FVector(0.0, SideY, 0.0), FVector::UpVector, 21.0,
				TNPlaygroundKit::Rgb(0xFFE0C2, 0.2f));
		}
		B.AddQuad(FVector(-HalfL, -OuterHW, TunnelTopZ), FVector(HalfL, -OuterHW, TunnelTopZ), FVector(HalfL, OuterHW, TunnelTopZ), FVector(-HalfL, OuterHW, TunnelTopZ),
			FVector::UpVector, Crown);

		// Por dentro: paredes hasta el arranque de la bóveda y bóveda lisa, en rebanadas a lo largo de X.
		const int32 NX = FMath::Max(8, FMath::RoundToInt32(Length / 16.0));
		constexpr int32 NY = 16;
		for (int32 i = 0; i < NX; ++i)
		{
			const double Xa = -HalfL + Length * i / NX;
			const double Xb = -HalfL + Length * (i + 1) / NX;
			for (const double SideY : { -1.0, 1.0 })
			{
				const double Y = SideY * TunnelInnerHW;
				B.AddQuad(FVector(Xa, Y, 0.0), FVector(Xb, Y, 0.0), FVector(Xb, Y, TunnelVaultZ(Xb, Y, HalfL)), FVector(Xa, Y, TunnelVaultZ(Xa, Y, HalfL)),
					FVector(0.0, -SideY, 0.0), Inside);
			}
			for (int32 j = 0; j < NY; ++j)
			{
				const double Ya = -TunnelInnerHW + 2.0 * TunnelInnerHW * j / NY;
				const double Yb = -TunnelInnerHW + 2.0 * TunnelInnerHW * (j + 1) / NY;
				const FVector P[4] = { FVector(Xa, Ya, TunnelVaultZ(Xa, Ya, HalfL)), FVector(Xb, Ya, TunnelVaultZ(Xb, Ya, HalfL)),
					FVector(Xb, Yb, TunnelVaultZ(Xb, Yb, HalfL)), FVector(Xa, Yb, TunnelVaultZ(Xa, Yb, HalfL)) };
				const FVector N[4] = { TunnelVaultNormal(Xa, Ya, HalfL), TunnelVaultNormal(Xb, Ya, HalfL), TunnelVaultNormal(Xb, Yb, HalfL), TunnelVaultNormal(Xa, Yb, HalfL) };
				TNPlaygroundKit::SmoothQuad1(B, P, N, TNPlaygroundKit::Shade(Inside, 0.92));
			}
		}

		// Fachadas: pilares a los lados de la boca, dintel sobre el arco, reborde oscuro del arco y concha en la clave.
		constexpr int32 NT = 18;
		const double ArchB = TunnelMouthTop - TunnelMouthSide;
		for (const double End : { -1.0, 1.0 })
		{
			const double X = End * HalfL;
			const FVector Out(End, 0.0, 0.0);
			B.AddQuad(FVector(X, -OuterHW, 0.0), FVector(X, -TunnelInnerHW, 0.0), FVector(X, -TunnelInnerHW, TunnelTopZ), FVector(X, -OuterHW, TunnelTopZ), Out, Wall);
			B.AddQuad(FVector(X, TunnelInnerHW, 0.0), FVector(X, OuterHW, 0.0), FVector(X, OuterHW, TunnelTopZ), FVector(X, TunnelInnerHW, TunnelTopZ), Out, Wall);
			const double Xr = X + End * 2.5;
			for (int32 t = 0; t < NT; ++t)
			{
				const double T0 = TNPlaygroundKit::KitPi * t / NT;
				const double T1 = TNPlaygroundKit::KitPi * (t + 1) / NT;
				const double Y0 = TunnelInnerHW * FMath::Cos(T0);
				const double Y1 = TunnelInnerHW * FMath::Cos(T1);
				const double Z0 = TunnelMouthSide + ArchB * FMath::Sin(T0);
				const double Z1 = TunnelMouthSide + ArchB * FMath::Sin(T1);
				B.AddQuad(FVector(X, Y0, Z0), FVector(X, Y1, Z1), FVector(X, Y1, TunnelTopZ), FVector(X, Y0, TunnelTopZ), Out, Wall);
				// Reborde: tira de 11 cm por fuera del arco, en relieve.
				const FVector2D N0 = FVector2D(ArchB * FMath::Cos(T0), TunnelInnerHW * FMath::Sin(T0)).GetSafeNormal();
				const FVector2D N1 = FVector2D(ArchB * FMath::Cos(T1), TunnelInnerHW * FMath::Sin(T1)).GetSafeNormal();
				B.AddQuad(FVector(Xr, Y0, Z0), FVector(Xr, Y1, Z1), FVector(Xr, Y1 + N1.X * 11.0, Z1 + N1.Y * 11.0), FVector(Xr, Y0 + N0.X * 11.0, Z0 + N0.Y * 11.0), Out, Deep);
			}
			for (const double SideY : { -1.0, 1.0 })
			{
				const double Yin = SideY * TunnelInnerHW;
				const double Yout = SideY * (TunnelInnerHW + 11.0);
				B.AddQuad(FVector(Xr, Yin, 0.0), FVector(Xr, Yout, 0.0), FVector(Xr, Yout, TunnelMouthSide), FVector(Xr, Yin, TunnelMouthSide), Out, Deep);
			}
			TNPlaygroundKit::AddShellFan(B, FVector(X + End * 3.5, 0.0, TunnelMouthTop + 14.0), Out, FVector::UpVector, 19.0, TNPlaygroundKit::Rgb(0xFFB4A2, 0.2f));
		}

		// Almenas a lo largo de los dos bordes de arriba.
		const int32 Merlons = FMath::Max(2, FMath::RoundToInt32(Length / 75.0));
		for (const double SideY : { -1.0, 1.0 })
		{
			for (int32 m = 0; m < Merlons; ++m)
			{
				const FVector Merlon(-HalfL + (m + 0.5) * Length / Merlons, SideY * (OuterHW - 13.0), TunnelTopZ + 14.0);
				const FVector MerlonHalf(16.0, 12.0, 14.0);
				TNPlaygroundKit::AddAxisBox(B, Merlon, MerlonHalf, TNPlaygroundKit::Rgb(0xEFD29A));
				Hulls.Add(TNPlaygroundKit::HullAxisBox(Merlon, MerlonHalf));
			}
		}
		// Banderín en lo alto (sin colisión: se puede andar por encima del túnel).
		const FVector PoleFoot(0.0, 0.0, TunnelTopZ);
		TNPlaygroundKit::AddRod(B, PoleFoot, PoleFoot + FVector(0.0, 0.0, 110.0), 2.2, 6, TNPlaygroundKit::Rgb(0x7A4E2B), FVector::ForwardVector);
		TNPlaygroundKit::AddPennant(B, PoleFoot + FVector(0.0, 0.0, 108.0), FVector(1.0, 0.4, 0.0), 48.0, 28.0, FlagTone);

		// Camino de piedrecitas por dentro.
		const FLinearColor Pebbles[3] = { TNPlaygroundKit::Rgb(0xD8D2C4, 0.1f), TNPlaygroundKit::Rgb(0xF2EEE6, 0.1f), TNPlaygroundKit::Rgb(0xBDB6A8, 0.1f) };
		constexpr int32 NumPebbles = 11;
		for (int32 p = 0; p < NumPebbles; ++p)
		{
			const double X = FMath::Lerp(-HalfL - 40.0, HalfL + 40.0, static_cast<double>(p) / (NumPebbles - 1));
			const double Y = 24.0 * (TNPlaygroundKit::Hash01(p, 1, Seed) - 0.5);
			const double Rad = 7.0 + 3.5 * TNPlaygroundKit::Hash01(p, 2, Seed);
			TNPlaygroundKit::AddDisc(B, FVector(X, Y, 0.6), FVector::UpVector, Rad, 8, Pebbles[p % 3]);
		}

		// Colisión: paredes y techo en embudo (convexo: el techo es el epígrafo de una función convexa de X).
		const double WallCenterY = 0.5 * (TunnelInnerHW + OuterHW);
		for (const double SideY : { -1.0, 1.0 })
		{
			Hulls.Add(TNPlaygroundKit::HullAxisBox(FVector(0.0, SideY * WallCenterY, TunnelTopZ * 0.5), FVector(HalfL, TunnelWallT * 0.5, TunnelTopZ * 0.5)));
		}
		TArray<FVector> Roof;
		const double RoofX[4] = { -HalfL, -HalfL + TunnelFunnel, HalfL - TunnelFunnel, HalfL };
		const double RoofZ[4] = { TunnelMouthTop, TunnelRoofBottom, TunnelRoofBottom, TunnelMouthTop };
		for (int32 r = 0; r < 4; ++r)
		{
			for (const double SideY : { -1.0, 1.0 })
			{
				Roof.Add(FVector(RoofX[r], SideY * OuterHW, RoofZ[r]));
				Roof.Add(FVector(RoofX[r], SideY * OuterHW, TunnelTopZ));
			}
		}
		Hulls.Add(Roof);

		OutZoneCenter = FVector(0.0, 0.0, 90.0);
		OutZoneHalf = FVector(HalfL + 25.0, TunnelInnerHW - 8.0, 90.0);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Barra giratoria de pala
	// ─────────────────────────────────────────────────────────────────────────

	void BuildSpinnerHub(FBuffers& B, FHulls& Hulls, const FLinearColor& HubTone)
	{
		const FLinearColor RimTone = TNPlaygroundKit::Shade(HubTone, 1.15);
		TNPlaygroundKit::AddFrustum(B, FVector(0.0, 0.0, 6.0), FVector(0.0, 0.0, HubHeight), HubRadius - 2.0, HubTopRadius, 24, HubTone, TNPlaygroundKit::Shade(HubTone, 1.08),
			false, true);
		TNPlaygroundKit::AddFrustum(B, FVector::ZeroVector, FVector(0.0, 0.0, 7.0), HubRadius + 1.5, HubRadius + 1.5, 24, RimTone, RimTone, false, false);
		TNPlaygroundKit::AddAnnulus(B, FVector(0.0, 0.0, 7.0), FVector::UpVector, HubRadius - 2.0, HubRadius + 1.5, 24, RimTone);
		Hulls.Add(TNPlaygroundKit::HullCylinder(FVector::ZeroVector, HubHeight, HubRadius, HubTopRadius, 12));
	}

	/** Palas que giran (espacio del pivote): collar alrededor del cubo, tapa con remolino y, por brazo, mango, puño, cuello y hoja. */
	void BuildSpinnerArms(FBuffers& B, double ArmLen, double ArmTop, int32 Arms, const FLinearColor& HandleTone, const FLinearColor& BladeTone)
	{
		const double Zc = FMath::Max(8.0, ArmTop - 14.0);
		const FLinearColor CollarTone = TNPlaygroundKit::Rgb(0xFFCB3D, 0.2f);
		TNPlaygroundKit::AddFrustum(B, FVector(0.0, 0.0, Zc - 7.0), FVector(0.0, 0.0, Zc + 7.0), HubRadius + 4.0, HubRadius + 4.0, 24, CollarTone, CollarTone, false, false);
		TNPlaygroundKit::AddAnnulus(B, FVector(0.0, 0.0, Zc + 7.0), FVector::UpVector, HubRadius - 3.0, HubRadius + 4.0, 24, CollarTone);
		TNPlaygroundKit::AddAnnulus(B, FVector(0.0, 0.0, Zc - 7.0), -FVector::UpVector, HubRadius - 3.0, HubRadius + 4.0, 24, CollarTone);
		// Tapa con remolino encima del cubo: se ve girar aunque las palas queden detrás.
		constexpr int32 Wedges = 8;
		for (int32 w = 0; w < Wedges; ++w)
		{
			const double A0 = TNPlaygroundKit::KitTwoPi * w / Wedges;
			const double A1 = TNPlaygroundKit::KitTwoPi * (w + 1) / Wedges;
			const FVector C(0.0, 0.0, HubHeight + 0.6);
			const FVector E0 = C + FVector(FMath::Cos(A0), FMath::Sin(A0), 0.0) * (HubTopRadius - 2.0);
			const FVector E1 = C + FVector(FMath::Cos(A1 + 0.35), FMath::Sin(A1 + 0.35), 0.0) * (HubTopRadius - 2.0);
			B.AddTri(C, E0, E1, FVector::UpVector, (w % 2 == 0) ? HandleTone : TNPlaygroundKit::Rgb(0xFFF6E0, 0.1f));
		}
		const FLinearColor GripTone = TNPlaygroundKit::Shade(HandleTone, 0.78);
		for (int32 j = 0; j < Arms; ++j)
		{
			const double A = TNPlaygroundKit::KitTwoPi * j / FMath::Max(1, Arms);
			const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0.0);
			const FVector Side(-Dir.Y, Dir.X, 0.0);
			const FVector Lift(0.0, 0.0, Zc);
			TNPlaygroundKit::AddRod(B, Dir * (HubRadius + 3.0) + Lift, Dir * (ArmLen - 50.0) + Lift, 4.5, 8, HandleTone, FVector::UpVector);
			TNPlaygroundKit::AddFrustum(B, Dir * (HubRadius + 3.0) + Lift, Dir * (HubRadius + 24.0) + Lift, 6.0, 6.0, 10, GripTone, GripTone, false, true);
			TNPlaygroundKit::AddFrustum(B, Dir * (ArmLen - 52.0) + Lift, Dir * (ArmLen - 44.0) + Lift, 4.5, 8.0, 10, HandleTone, HandleTone, false, false);
			// Hoja vertical de esquinas de arriba redondeadas (u hacia fuera, v hacia arriba); el grosor, de lado.
			const double BladeW = 46.0;
			const double BladeH = FMath::Max(24.0, ArmTop - 3.0);
			constexpr double CornerR = 10.0;
			TArray<FVector2D> Outline;
			Outline.Add(FVector2D(0.0, 0.0));
			Outline.Add(FVector2D(BladeW, 0.0));
			for (int32 c = 0; c <= 4; ++c)
			{
				const double Ca = 0.5 * TNPlaygroundKit::KitPi * c / 4.0;
				Outline.Add(FVector2D(BladeW - CornerR + CornerR * FMath::Cos(Ca), BladeH - CornerR + CornerR * FMath::Sin(Ca)));
			}
			for (int32 c = 0; c <= 4; ++c)
			{
				const double Ca = 0.5 * TNPlaygroundKit::KitPi + 0.5 * TNPlaygroundKit::KitPi * c / 4.0;
				Outline.Add(FVector2D(CornerR + CornerR * FMath::Cos(Ca), BladeH - CornerR + CornerR * FMath::Sin(Ca)));
			}
			TNPlaygroundKit::AddSlab(B, Dir * (ArmLen - BladeW) + FVector(0.0, 0.0, 3.0), Dir, FVector::UpVector, Side, Outline, 3.2, BladeTone);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// ATN_PlaygroundPiece
// ─────────────────────────────────────────────────────────────────────────────

ATN_PlaygroundPiece::ATN_PlaygroundPiece()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	SetNetUpdateFrequency(2.f);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(SceneRoot);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetCanEverAffectNavigation(false);

	// Colisión convexa (bloquea a todos salvo a la cámara, que atraviesa las piezas pequeñas sin dar tirones).
	BodyCollision = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BodyCollision"));
	BodyCollision->SetupAttachment(SceneRoot);
	BodyCollision->bUseComplexAsSimpleCollision = false;
	BodyCollision->bUseAsyncCooking = false;
	BodyCollision->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	BodyCollision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	SpinPivot = CreateDefaultSubobject<USceneComponent>(TEXT("SpinPivot"));
	SpinPivot->SetupAttachment(SceneRoot);

	SpinMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SpinMesh"));
	SpinMesh->SetupAttachment(SpinPivot);
	SpinMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SpinMesh->SetCanEverAffectNavigation(false);

	SpinBlockers.Reserve(3);
	for (int32 j = 0; j < 3; ++j)
	{
		UBoxComponent* Blocker = CreateDefaultSubobject<UBoxComponent>(*FString::Printf(TEXT("SpinBlocker%d"), j));
		Blocker->SetupAttachment(SpinPivot);
		Blocker->InitBoxExtent(FVector(90.0, 6.0, 20.0));
		Blocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Blocker->SetCollisionObjectType(ECC_WorldDynamic);
		Blocker->SetCollisionResponseToAllChannels(ECR_Ignore);
		Blocker->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
		Blocker->SetGenerateOverlapEvents(false);
		Blocker->SetCanEverAffectNavigation(false);
		Blocker->SetHiddenInGame(true);
		Blocker->SetVisibility(false);
		SpinBlockers.Add(Blocker);
	}

	AssistZone = CreateDefaultSubobject<UBoxComponent>(TEXT("AssistZone"));
	AssistZone->SetupAttachment(SceneRoot);
	AssistZone->InitBoxExtent(FVector(50.0));
	AssistZone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AssistZone->SetCollisionObjectType(ECC_WorldDynamic);
	AssistZone->SetCollisionResponseToAllChannels(ECR_Ignore);
	AssistZone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	AssistZone->SetGenerateOverlapEvents(true);
	AssistZone->SetCanEverAffectNavigation(false);
	AssistZone->SetHiddenInGame(true);
	AssistZone->SetVisibility(false);
}

void ATN_PlaygroundPiece::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_PlaygroundPiece, PieceType);
	DOREPLIFETIME(ATN_PlaygroundPiece, Tint);
	DOREPLIFETIME(ATN_PlaygroundPiece, SecondTint);
	DOREPLIFETIME(ATN_PlaygroundPiece, DecorSeed);
	DOREPLIFETIME(ATN_PlaygroundPiece, PostHeight);
	DOREPLIFETIME(ATN_PlaygroundPiece, StepCount);
	DOREPLIFETIME(ATN_PlaygroundPiece, StepHeight);
	DOREPLIFETIME(ATN_PlaygroundPiece, StepDepth);
	DOREPLIFETIME(ATN_PlaygroundPiece, StepWidth);
	DOREPLIFETIME(ATN_PlaygroundPiece, SlideHeight);
	DOREPLIFETIME(ATN_PlaygroundPiece, SlideAngle);
	DOREPLIFETIME(ATN_PlaygroundPiece, SlideWidth);
	DOREPLIFETIME(ATN_PlaygroundPiece, bSlideSteps);
	DOREPLIFETIME(ATN_PlaygroundPiece, SlideBoost);
	DOREPLIFETIME(ATN_PlaygroundPiece, PlatformHeight);
	DOREPLIFETIME(ATN_PlaygroundPiece, CookieRadius);
	DOREPLIFETIME(ATN_PlaygroundPiece, TunnelLength);
	DOREPLIFETIME(ATN_PlaygroundPiece, TunnelAssistSpeed);
	DOREPLIFETIME(ATN_PlaygroundPiece, ArmLength);
	DOREPLIFETIME(ATN_PlaygroundPiece, ArmHeight);
	DOREPLIFETIME(ATN_PlaygroundPiece, NumArms);
	DOREPLIFETIME(ATN_PlaygroundPiece, SpinSpeed);
	DOREPLIFETIME(ATN_PlaygroundPiece, StartAngle);
	DOREPLIFETIME(ATN_PlaygroundPiece, KnockSpeed);
	DOREPLIFETIME(ATN_PlaygroundPiece, KnockUp);
}

void ATN_PlaygroundPiece::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	BuildAll(false);
}

void ATN_PlaygroundPiece::BeginPlay()
{
	Super::BeginPlay();
	BuildAll(false);
	SetActorTickEnabled(NeedsTick());
	if (GetNetMode() != NM_DedicatedServer && PieceType == ETNPlaygroundPieceType::SpadeSpinner)
	{
		Voice = UTN_PlaygroundSynthComponent::AttachTo(this, GetActorLocation() + FVector(0.0, 0.0, 40.0), 350.f, 2200.f);
	}
	UE_LOG(LogTortunabo, Verbose, TEXT("[Parque] Pieza %s lista (%s)."), *GetName(), *UEnum::GetValueAsString(PieceType));
}

void ATN_PlaygroundPiece::OnRep_Config()
{
	BuildAll(false);
	if (HasActorBegunPlay())
	{
		SetActorTickEnabled(NeedsTick());
		if (!Voice && GetNetMode() != NM_DedicatedServer && PieceType == ETNPlaygroundPieceType::SpadeSpinner)
		{
			Voice = UTN_PlaygroundSynthComponent::AttachTo(this, GetActorLocation() + FVector(0.0, 0.0, 40.0), 350.f, 2200.f);
		}
	}
}

void ATN_PlaygroundPiece::RebuildPiece()
{
	BuildAll(true);
	if (HasActorBegunPlay())
	{
		SetActorTickEnabled(NeedsTick());
	}
}

bool ATN_PlaygroundPiece::NeedsTick() const
{
	return PieceType == ETNPlaygroundPieceType::SpadeSpinner || PieceType == ETNPlaygroundPieceType::CastleTunnel || PieceType == ETNPlaygroundPieceType::ShellSlide;
}

float ATN_PlaygroundPiece::GetStandHeight() const
{
	switch (PieceType)
	{
	case ETNPlaygroundPieceType::BucketPost: return PostHeight;
	case ETNPlaygroundPieceType::PopsicleSteps: return StepCount * StepHeight;
	case ETNPlaygroundPieceType::ShellSlide: return SlideHeight;
	case ETNPlaygroundPieceType::CookiePlatform: return PlatformHeight;
	case ETNPlaygroundPieceType::CastleTunnel: return static_cast<float>(TNPlaygroundPieceDetail::TunnelTopZ);
	default: return static_cast<float>(TNPlaygroundPieceDetail::HubHeight);
	}
}

uint32 ATN_PlaygroundPiece::ConfigHash() const
{
	uint32 Acc = GetTypeHash(static_cast<uint8>(PieceType));
	Acc = HashCombine(Acc, GetTypeHash(static_cast<uint8>(Tint)));
	Acc = HashCombine(Acc, GetTypeHash(static_cast<uint8>(SecondTint)));
	Acc = HashCombine(Acc, GetTypeHash(DecorSeed));
	Acc = HashCombine(Acc, GetTypeHash(PostHeight));
	Acc = HashCombine(Acc, GetTypeHash(StepCount));
	Acc = HashCombine(Acc, GetTypeHash(StepHeight));
	Acc = HashCombine(Acc, GetTypeHash(StepDepth));
	Acc = HashCombine(Acc, GetTypeHash(StepWidth));
	Acc = HashCombine(Acc, GetTypeHash(SlideHeight));
	Acc = HashCombine(Acc, GetTypeHash(SlideAngle));
	Acc = HashCombine(Acc, GetTypeHash(SlideWidth));
	Acc = HashCombine(Acc, GetTypeHash(bSlideSteps));
	Acc = HashCombine(Acc, GetTypeHash(PlatformHeight));
	Acc = HashCombine(Acc, GetTypeHash(CookieRadius));
	Acc = HashCombine(Acc, GetTypeHash(TunnelLength));
	Acc = HashCombine(Acc, GetTypeHash(ArmLength));
	Acc = HashCombine(Acc, GetTypeHash(ArmHeight));
	Acc = HashCombine(Acc, GetTypeHash(NumArms));
	Acc = HashCombine(Acc, GetTypeHash(StartAngle));
	return Acc;
}

void ATN_PlaygroundPiece::BuildAll(bool bForce)
{
	using namespace TNPlaygroundPieceDetail;
	const uint32 NewHash = ConfigHash();
	if (!bForce && NewHash == BuiltHash && BodyMesh->GetStaticMesh())
	{
		return;
	}
	BuiltHash = NewHash;

	UMaterialInterface* Mat = TNPlaygroundKit::VertexColorMaterial();
	const FLinearColor MainTone = TintColor(Tint);
	const FLinearColor SecondTone = TintColor(SecondTint);
	const uint32 Seed = static_cast<uint32>(DecorSeed) * 2654435761u + 91u;
	FBuffers Body;
	FBuffers Spin;
	FHulls Hulls;
	bool bZone = false;
	FVector ZoneCenter = FVector::ZeroVector;
	FVector ZoneHalf(50.0);
	FQuat ZoneRot = FQuat::Identity;

	switch (PieceType)
	{
	case ETNPlaygroundPieceType::PopsicleSteps:
		BuildPopsicleSteps(Body, Hulls, FMath::Clamp(StepCount, 1, 5), StepHeight, StepDepth, StepWidth, Seed);
		break;
	case ETNPlaygroundPieceType::ShellSlide:
	{
		FSlideZone Slide;
		BuildShellSlide(Body, Hulls, SlideHeight, SlideAngle, SlideWidth, bSlideSteps, MainTone, Slide);
		bZone = true;
		ZoneCenter = Slide.Center;
		ZoneHalf = Slide.Half;
		ZoneRot = Slide.Rotation;
		SlideDownDir = Slide.Down;
		break;
	}
	case ETNPlaygroundPieceType::CookiePlatform:
		BuildCookie(Body, Hulls, PlatformHeight, CookieRadius, Seed);
		break;
	case ETNPlaygroundPieceType::CastleTunnel:
		BuildCastleTunnel(Body, Hulls, TunnelLength, MainTone, Seed, ZoneCenter, ZoneHalf);
		bZone = true;
		break;
	case ETNPlaygroundPieceType::SpadeSpinner:
		BuildSpinnerHub(Body, Hulls, SecondTone);
		BuildSpinnerArms(Spin, ArmLength, ArmHeight + 12.0, FMath::Clamp(NumArms, 1, 3), MainTone, SecondTone);
		break;
	default:
		BuildBucketPost(Body, Hulls, PostHeight, MainTone, SecondTone);
		break;
	}

	// Cuerpo: pieza de arte (la colisión convexa de abajo no cambia; la de la malla generada se queda invisible).
	TNArt::SetMesh(BodyMesh, TNPlaygroundKit::BuildMesh(this, Body, Mat), BodyArtSlot(PieceType, bSlideSteps));
	TNPlaygroundKit::ScaleArt(BodyMesh, BodyArtScale(*this));
	BodyCollision->SetCollisionConvexMeshes(Hulls);

	// Palas: malla que gira y cajas que apartan a los caparazones con física.
	const bool bSpinner = PieceType == ETNPlaygroundPieceType::SpadeSpinner;
	if (bSpinner)
	{
		TNArt::SetMesh(SpinMesh, TNPlaygroundKit::BuildMesh(this, Spin, Mat), SpinArtSlot(FMath::Clamp(NumArms, 1, 3)));
		TNPlaygroundKit::ScaleArt(SpinMesh, SpinArtScale(*this));
	}
	else if (SpinMesh->GetStaticMesh())
	{
		// Ya no es una barra: se quita su malla y, con ella, la de arte.
		TNArt::SetMesh(SpinMesh, nullptr, SpinArtSlot(2));
	}
	SpinPivot->SetRelativeRotation(FRotator(0.0, StartAngle, 0.0));
	const int32 Arms = FMath::Clamp(NumArms, 1, 3);
	const double ArmTop = ArmHeight + 12.0;
	for (int32 j = 0; j < SpinBlockers.Num(); ++j)
	{
		UBoxComponent* Blocker = SpinBlockers[j].Get();
		if (!Blocker)
		{
			continue;
		}
		const bool bUsed = bSpinner && j < Arms;
		Blocker->SetCollisionEnabled(bUsed ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		if (bUsed)
		{
			const double A = 360.0 * j / Arms;
			const FVector Dir(FMath::Cos(FMath::DegreesToRadians(A)), FMath::Sin(FMath::DegreesToRadians(A)), 0.0);
			Blocker->SetBoxExtent(FVector((ArmLength - HubRadius) * 0.5, 6.0, ArmTop * 0.5), false);
			Blocker->SetRelativeLocationAndRotation(Dir * ((ArmLength + HubRadius) * 0.5) + FVector(0.0, 0.0, ArmTop * 0.5), FRotator(0.0, A, 0.0));
		}
	}

	// Zona de ayuda (túnel o tobogán).
	AssistZone->SetCollisionEnabled(bZone ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	if (bZone)
	{
		AssistZone->SetBoxExtent(ZoneHalf, false);
		AssistZone->SetRelativeLocationAndRotation(ZoneCenter, ZoneRot);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick
// ─────────────────────────────────────────────────────────────────────────────

double ATN_PlaygroundPiece::AdvanceClock(float DeltaSeconds)
{
	const UWorld* World = GetWorld();
	double Target = World ? World->GetTimeSeconds() : 0.0;
	if (World)
	{
		if (const AGameStateBase* GameState = World->GetGameState())
		{
			Target = GameState->GetServerWorldTimeSeconds();
		}
	}
	if (!bClockValid || FMath::Abs(Target - Clock) > 1.0)
	{
		Clock = Target;
		bClockValid = true;
	}
	else
	{
		Clock += DeltaSeconds;
		Clock += (Target - Clock) * FMath::Min(1.0, static_cast<double>(DeltaSeconds) * 1.5);
	}
	return Clock;
}

double ATN_PlaygroundPiece::SpinAngleDeg(double AtTime) const
{
	return FMath::Fmod(static_cast<double>(StartAngle) + static_cast<double>(SpinSpeed) * AtTime, 360.0);
}

void ATN_PlaygroundPiece::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	switch (PieceType)
	{
	case ETNPlaygroundPieceType::SpadeSpinner: TickSpinner(DeltaSeconds); break;
	case ETNPlaygroundPieceType::CastleTunnel: TickTunnel(); break;
	case ETNPlaygroundPieceType::ShellSlide: TickSlide(DeltaSeconds); break;
	default: break;
	}
}

void ATN_PlaygroundPiece::TickSpinner(float DeltaSeconds)
{
	using namespace TNPlaygroundPieceDetail;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = AdvanceClock(DeltaSeconds);
	const double AngleNow = SpinAngleDeg(Now);
	SpinPivot->SetRelativeRotation(FRotator(0.0, AngleNow, 0.0));

	const FTransform ActorXf = GetActorTransform();
	const int32 Arms = FMath::Clamp(NumArms, 1, 3);
	const double ArmTop = ArmHeight + 12.0;
	const bool bServer = GetNetMode() != NM_Client;
	const double WorldNow = World->GetTimeSeconds();
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		ACharacter* Victim = *It;
		if (!IsValid(Victim) || !TNPlaygroundPieceDetail::SimulatesMovement(Victim))
		{
			continue;
		}
		const FVector Local = ActorXf.InverseTransformPosition(Victim->GetActorLocation());
		const double Radial = FVector2D(Local.X, Local.Y).Size();
		const UCapsuleComponent* Capsule = Victim->GetCapsuleComponent();
		const double Half = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 70.0;
		const double CapR = Capsule ? Capsule->GetScaledCapsuleRadius() : 40.0;
		// Fuera del alcance, saltando por encima de la pala o sobre el cubo del centro.
		if (Radial > ArmLength + CapR * 0.6 || Radial < HubRadius - CapR * 0.5 || Local.Z - Half > ArmTop + 2.0 || Local.Z + Half < -10.0)
		{
			continue;
		}
		// El servidor mira a cada cliente en el instante que ese cliente veía: su reloj va medio ping por detrás y sus
		// movimientos llegan medio ping tarde.
		double Eval = Now;
		if (bServer && !Victim->IsLocallyControlled())
		{
			const APlayerState* State = Victim->GetPlayerState();
			Eval -= State ? FMath::Clamp(static_cast<double>(State->GetPingInMilliseconds()) * 0.001, 0.0, 0.35) : 0.0;
		}
		const double Theta = FMath::Atan2(Local.Y, Local.X);
		const double HalfArc = FMath::Asin(FMath::Clamp((CapR + 8.0) / FMath::Max(Radial, 1.0), 0.0, 1.0));
		const double NowRad = FMath::DegreesToRadians(SpinAngleDeg(Eval));
		const double PrevRad = FMath::DegreesToRadians(SpinAngleDeg(Eval - DeltaSeconds));
		bool bHit = false;
		for (int32 j = 0; j < Arms && !bHit; ++j)
		{
			const double Offset = TNPlaygroundKit::KitTwoPi * j / Arms;
			const double DNow = FMath::UnwindRadians(Theta - (NowRad + Offset));
			const double DPrev = FMath::UnwindRadians(Theta - (PrevRad + Offset));
			// Encima de la pala ahora, o la pala ha pasado por su ángulo en este fotograma.
			bHit = FMath::Abs(DNow) < HalfArc || (DNow * DPrev < 0.0 && FMath::Abs(DNow) < 1.2 && FMath::Abs(DPrev) < 1.2);
		}
		if (bHit)
		{
			Knock(Victim, Theta, WorldNow);
		}
	}

	// Aviso sonoro: barrido cuando una pala se acerca al jugador local.
	WhooshCooldown -= DeltaSeconds;
	// Con la pantalla partida (#311), la tortuga local más cercana.
	const APawn* Mine = TNLocalViews::ClosestLocalPawn(World, ActorXf.GetLocation());
	if (Voice && Mine && WhooshCooldown <= 0.f)
	{
		const FVector Local = ActorXf.InverseTransformPosition(Mine->GetActorLocation());
		if (FVector2D(Local.X, Local.Y).Size() < ArmLength + 180.0 && FMath::Abs(Local.Z) < 250.0)
		{
			const double Theta = FMath::Atan2(Local.Y, Local.X);
			const double Turning = SpinSpeed >= 0.f ? 1.0 : -1.0;
			const double NowRad = FMath::DegreesToRadians(AngleNow);
			for (int32 j = 0; j < Arms; ++j)
			{
				const double Gap = FMath::UnwindRadians(Theta - (NowRad + TNPlaygroundKit::KitTwoPi * j / Arms));
				if (Gap * Turning > 0.0 && FMath::Abs(Gap) < 0.5)
				{
					const double Tip = NowRad + TNPlaygroundKit::KitTwoPi * j / Arms;
					Voice->SetWorldLocation(ActorXf.TransformPosition(FVector(FMath::Cos(Tip), FMath::Sin(Tip), 0.0) * (ArmLength * 0.8) + FVector(0.0, 0.0, 30.0)));
					Voice->TriggerSound(ETNPlaygroundSound::Whoosh, 1.f, 0.55f * EffectsVolume);
					WhooshCooldown = 0.45f;
					break;
				}
			}
		}
	}
}

void ATN_PlaygroundPiece::Knock(ACharacter* Victim, double Theta, double WorldNow)
{
	const TWeakObjectPtr<ACharacter> Key(Victim);
	if (const double* Last = LastKnockTime.Find(Key))
	{
		if (WorldNow - *Last < 0.9)
		{
			return;
		}
	}
	LastKnockTime.Add(Key, WorldNow);
	// Empujón en el sentido del giro (tangente) y algo hacia fuera, con salto: lo mismo en el servidor y en el dueño.
	const double Turning = SpinSpeed >= 0.f ? 1.0 : -1.0;
	const FVector Radial(FMath::Cos(Theta), FMath::Sin(Theta), 0.0);
	const FVector Tangent(-Radial.Y * Turning, Radial.X * Turning, 0.0);
	const FVector Push = GetActorTransform().TransformVectorNoScale((Tangent + Radial * 0.45).GetSafeNormal());
	Victim->LaunchCharacter(Push * KnockSpeed + FVector(0.0, 0.0, KnockUp), true, true);

	if (GetNetMode() != NM_Client)
	{
		if (GetIsReplicated())
		{
			MulticastKnockFX(Victim);
			ForceNetUpdate();
		}
		else
		{
			PlayKnockFX(Victim->GetActorLocation());
		}
	}
	else if (Victim->IsLocallyControlled())
	{
		PlayKnockFX(Victim->GetActorLocation());
	}
}

void ATN_PlaygroundPiece::MulticastKnockFX_Implementation(APawn* Victim)
{
	if (GetNetMode() == NM_Client && Victim && Victim->IsLocallyControlled())
	{
		return;
	}
	PlayKnockFX(Victim ? Victim->GetActorLocation() : GetActorLocation());
}

void ATN_PlaygroundPiece::PlayKnockFX(const FVector& WorldAt)
{
	if (Voice)
	{
		Voice->SetWorldLocation(WorldAt);
		Voice->TriggerSound(ETNPlaygroundSound::Bonk, FMath::FRandRange(0.92f, 1.1f), EffectsVolume);
	}
}

void ATN_PlaygroundPiece::TickTunnel()
{
	using namespace TNPlaygroundPieceDetail;
	if (TunnelAssistSpeed <= 0.f)
	{
		return;
	}
	TArray<AActor*> Inside;
	AssistZone->GetOverlappingActors(Inside, ACharacter::StaticClass());
	const FVector Axis = GetActorForwardVector();
	const double HalfL = TunnelLength * 0.5;
	for (AActor* Other : Inside)
	{
		ACharacter* Slider = Cast<ACharacter>(Other);
		if (!Slider || !TNPlaygroundPieceDetail::SimulatesMovement(Slider))
		{
			continue;
		}
		UCharacterMovementComponent* Move = Slider->GetCharacterMovement();
		const UCapsuleComponent* Capsule = Slider->GetCapsuleComponent();
		if (!Move || !Capsule)
		{
			continue;
		}
		const double LocalX = FVector::DotProduct(Slider->GetActorLocation() - GetActorLocation(), Axis);
		// De pie en la boca (no cabe): no se le empuja contra el techo; tumbada o ya dentro, sí.
		const bool bLow = Capsule->GetScaledCapsuleHalfHeight() < 60.0;
		if (!bLow && FMath::Abs(LocalX) > HalfL - 10.0)
		{
			continue;
		}
		const double Along = FVector::DotProduct(Move->Velocity, Axis);
		double Dir = Along >= 0.0 ? 1.0 : -1.0;
		if (FMath::Abs(Along) < 40.0)
		{
			// Parada dentro: hacia la salida más cercana.
			Dir = LocalX >= 0.0 ? 1.0 : -1.0;
		}
		if (FMath::Abs(Along) < TunnelAssistSpeed)
		{
			// Mismo empujón en el servidor y en el dueño: el panzazo no se para a medio túnel.
			Move->AddImpulse(Axis * (Dir * TunnelAssistSpeed - Along), true);
		}
	}
}

void ATN_PlaygroundPiece::TickSlide(float DeltaSeconds)
{
	using namespace TNPlaygroundPieceDetail;
	if (SlideBoost <= 0.f)
	{
		return;
	}
	TArray<AActor*> OnSlide;
	AssistZone->GetOverlappingActors(OnSlide, ACharacter::StaticClass());
	const FVector Down = GetActorTransform().TransformVectorNoScale(SlideDownDir);
	for (AActor* Other : OnSlide)
	{
		ACharacter* Slider = Cast<ACharacter>(Other);
		if (!Slider || !TNPlaygroundPieceDetail::SimulatesMovement(Slider))
		{
			continue;
		}
		UCharacterMovementComponent* Move = Slider->GetCharacterMovement();
		if (!Move)
		{
			continue;
		}
		// Cuesta abajo hasta una velocidad tope: ritmo de tobogán sin salir disparado.
		if (FVector::DotProduct(Move->Velocity, Down) < 1100.0)
		{
			Move->AddImpulse(Down * (SlideBoost * DeltaSeconds), true);
		}
	}
}
