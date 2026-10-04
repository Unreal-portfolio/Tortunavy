#include "UI/Race/TN_RacePodiumStage.h"
#include "Art/TN_TurtleArt.h"
#include "Core/TN_CosmeticLook.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "World/Beach/TN_BeachTypes.h"
#include "../../World/ProcMap/TN_ProcMapMeshKit.h"
#include "../../World/ProcMap/TN_ProcMapRuntimeMesh.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto
// del bloque.
namespace TNRacePodiumDetail
{
	TAutoConsoleVariable<float> CVarPodiumFOV(TEXT("TN.Race.PodiumFOV"), 32.f,
		TEXT("Podio del campeón: campo de visión horizontal de la cámara (grados)."));
	TAutoConsoleVariable<float> CVarPodiumDistance(TEXT("TN.Race.PodiumDistance"), 2150.f,
		TEXT("Podio del campeón: distancia de la cámara al podio (cm)."));

	/** Donde vive el podio: muy alto, lejos del recorrido y del escaparate de la tienda (60 000). */
	const FVector StageLocation(0.0, 0.0, 150000.0);

	/** Centímetros del juego para Cm centímetros reales (todo a TNBeach::Scale). */
	constexpr double Real(double Cm) { return Cm * TNBeach::Scale; }

	constexpr float TurtleScale = 2.5f;
	constexpr int32 CaptureWidth = 1600;
	constexpr int32 CaptureHeight = 900;
	/** Dónde queda el centro del podio en la imagen (fracción del ancho): a la derecha, con los botones a la izquierda. */
	constexpr float PodiumScreenX = 0.62f;
	constexpr double CameraHeight = 280.0;
	constexpr double LookHeight = 170.0;

	// ── Podio (medidas reales × 28) ──
	/** 1.º: vaso de plástico boca abajo de 6 cm (boca de 6,5 cm abajo, base de 4,6 cm arriba). */
	constexpr double CupHeight = Real(6.0);
	constexpr double CupRim = Real(3.25);
	constexpr double CupTop = Real(2.3);
	/** 2.º: caja de zumo de 10,5 × 6,3 cm tumbada y aplastada (3,4 cm de alto). */
	constexpr double BoxLength = Real(10.5);
	constexpr double BoxWidth = Real(6.3);
	constexpr double BoxHeight = Real(3.4);
	/** 3.º: chancla de 26 × 9,5 cm con 1,8 cm de suela. */
	constexpr double SoleLength = Real(26.0);
	constexpr double SoleWidth = Real(9.5);
	constexpr double SoleThick = Real(1.8);
	/** Tapón de botella (3 × 1,2 cm) y lata de refresco (6,6 × 12,2 cm). */
	constexpr double CapRadius = Real(1.5);
	constexpr double CapHeight = Real(1.2);
	constexpr double CanRadius = Real(3.3);
	constexpr double CanLength = Real(12.2);

	/** Sitio de cada tortuga (la cámara mira hacia -X: +Y sale a la izquierda de la imagen) y hacia dónde mira. */
	const FVector PlaceSpots[3] = { FVector(0.0, 0.0, CupHeight), FVector(0.0, 215.0, BoxHeight), FVector(0.0, -250.0, SoleThick) };
	constexpr float PlaceYaw[3] = { -90.f, -100.f, -74.f };
	/** La chancla: el talón queda delante de la tortuga sentada (hacia la cámara). */
	constexpr double SoleHeelX = 170.0;
	/** Orilla: la arena acaba aquí y empieza el mar. */
	constexpr double ShoreX = -1550.0;
	constexpr double SeaZ = -26.0;

	/**
	 * Color sRGB 0xRRGGBB para las mallas en ejecución (MakeStaticMesh decodifica una vez más: así llega lineal) y, en el
	 * alfa, el brillo metálico de M_CosmeticVertexColor.
	 */
	FLinearColor Pal(uint32 Hex, float Metal = 0.f)
	{
		return FLinearColor(TNProcRuntimeMesh::SRGBToLinear(((Hex >> 16) & 255) / 255.f), TNProcRuntimeMesh::SRGBToLinear(((Hex >> 8) & 255) / 255.f),
			TNProcRuntimeMesh::SRGBToLinear((Hex & 255) / 255.f), Metal);
	}

	FLinearColor WithMetal(const FLinearColor& Color, float Metal)
	{
		FLinearColor Out = Color;
		Out.A = Metal;
		return Out;
	}

	/** Solo el canal de luz 2: ni el sol del nivel (canal 0) ni el escaparate de la tienda (canal 1) lo tocan. */
	FLightingChannels PodiumChannel()
	{
		FLightingChannels Channels;
		Channels.bChannel0 = false;
		Channels.bChannel1 = false;
		Channels.bChannel2 = true;
		return Channels;
	}

	void SetupPrimitive(UPrimitiveComponent* Prim, bool bShadow)
	{
		Prim->SetMobility(EComponentMobility::Movable);
		Prim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Prim->SetVisibleInSceneCaptureOnly(true);
		Prim->SetCastShadow(bShadow);
		Prim->LightingChannels = PodiumChannel();
	}

	void SetupPointLight(UPointLightComponent* Light, float Candelas, const FLinearColor& Color, float Radius, bool bShadows)
	{
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetIntensity(Candelas);
		Light->SetLightColor(Color);
		Light->SetAttenuationRadius(Radius);
		Light->SetSourceRadius(40.f);
		Light->SetCastShadows(bShadows);
		Light->LightingChannels = PodiumChannel();
	}

	/** Cuerpo de revolución limpio (sin ruido, como algo fabricado): anillos Z/R sobre Base, con tapa arriba si se pide. */
	void AddRevolve(TNProcMesh::FTNProcMeshBuffers& B, const FVector& Base, const TArray<double>& Z, const TArray<double>& R, int32 Seg,
		const FLinearColor& Color, bool bCapTop)
	{
		auto Ring = [&](int32 r, int32 k)
		{
			const double A = TNProcMap::TwoPi * static_cast<double>(k % Seg) / Seg;
			return Base + FVector(FMath::Cos(A) * R[r], FMath::Sin(A) * R[r], Z[r]);
		};
		for (int32 r = 0; r + 1 < Z.Num(); ++r)
		{
			for (int32 k = 0; k < Seg; ++k)
			{
				const double Am = TNProcMap::TwoPi * (k + 0.5) / Seg;
				const FVector Out(FMath::Cos(Am), FMath::Sin(Am), 0.0);
				B.AddQuad(Ring(r, k), Ring(r, k + 1), Ring(r + 1, k + 1), Ring(r + 1, k), Out, Color);
			}
		}
		if (bCapTop && Z.Num() > 0)
		{
			const int32 Top = Z.Num() - 1;
			const FVector Center = Base + FVector(0.0, 0.0, Z[Top]);
			for (int32 k = 0; k < Seg; ++k) { B.AddTri(Center, Ring(Top, k), Ring(Top, k + 1), FVector::UpVector, Color); }
		}
	}

	/** Polígono plano (tapa, dibujo) en el plano de Normal, con abanico desde su centro. */
	void AddFlat(TNProcMesh::FTNProcMeshBuffers& B, const TArray<FVector>& Poly, const FVector& Normal, const FLinearColor& Color)
	{
		if (Poly.Num() < 3) { return; }
		FVector Center = FVector::ZeroVector;
		for (const FVector& P : Poly) { Center += P; }
		Center /= static_cast<double>(Poly.Num());
		for (int32 i = 0; i < Poly.Num(); ++i) { B.AddTri(Center, Poly[i], Poly[(i + 1) % Poly.Num()], Normal, Color); }
	}

	/** Círculo de puntos en el plano de dos ejes (U, V) alrededor de Center. */
	TArray<FVector> CirclePoints(const FVector& Center, const FVector& U, const FVector& V, double Radius, int32 Seg)
	{
		TArray<FVector> Out;
		for (int32 k = 0; k < Seg; ++k)
		{
			const double A = TNProcMap::TwoPi * k / Seg;
			Out.Add(Center + U * (FMath::Cos(A) * Radius) + V * (FMath::Sin(A) * Radius));
		}
		return Out;
	}

	/** Arena con dunas muy suaves que baja hacia la orilla (mojada) y se mete bajo el mar. */
	void AddSand(TNProcMesh::FTNProcMeshBuffers& B)
	{
		constexpr int32 NX = 30;
		constexpr int32 NY = 22;
		const double X0 = ShoreX - 260.0;
		const double X1 = 2800.0;
		const double Y0 = -3400.0;
		const double Y1 = 3400.0;
		auto Height = [](double X, double Y)
		{
			const double Dunes = 5.0 * FMath::Sin(X * 0.0035 + Y * 0.0021) + 3.0 * FMath::Sin(Y * 0.009 + 1.3);
			const double Slope = FMath::Clamp((ShoreX + 320.0 - X) / 520.0, 0.0, 1.0);
			return Dunes * (1.0 - Slope) - 42.0 * Slope;
		};
		auto At = [&](int32 i, int32 j)
		{
			const double X = FMath::Lerp(X0, X1, static_cast<double>(i) / NX);
			const double Y = FMath::Lerp(Y0, Y1, static_cast<double>(j) / NY);
			return FVector(X, Y, Height(X, Y));
		};
		for (int32 i = 0; i < NX; ++i)
		{
			for (int32 j = 0; j < NY; ++j)
			{
				const FVector A = At(i, j);
				const FVector C = At(i + 1, j + 1);
				const double Cx = 0.5 * (A.X + C.X);
				const float Tone = 0.5f + 0.5f * static_cast<float>(TNProcMesh::TNProcHashNoise(i, j, 71u));
				FLinearColor Color = TNProcMesh::TNProcLerpColor(Pal(0xF2D49B), Pal(0xE6C184), Tone * 0.6f);
				if (Cx < ShoreX + 260.0)
				{
					Color = TNProcMesh::TNProcLerpColor(Color, Pal(0xC49C66), static_cast<float>(FMath::Clamp((ShoreX + 260.0 - Cx) / 260.0, 0.0, 1.0)));
				}
				B.AddQuad(A, At(i + 1, j), C, At(i, j + 1), FVector::UpVector, Color);
			}
		}
	}

	/** 1.º: vaso de plástico rojo boca abajo, con el borde blanco enrollado abajo y tres aros en relieve. */
	void AddCup(TNProcMesh::FTNProcMeshBuffers& B, const FVector& Base)
	{
		auto RadiusAt = [](double Z) { return FMath::Lerp(CupRim, CupTop, Z / CupHeight); };
		const FLinearColor White = Pal(0xF7F4EC, 0.1f);
		const FLinearColor Red = Pal(0xE0342E, 0.18f);
		AddRevolve(B, Base, { 0.0, 4.0, 11.0, 16.0 }, { CupRim + 2.0, CupRim + 6.0, CupRim + 6.0, RadiusAt(16.0) }, 36, White, false);
		TArray<double> Z = { 16.0 };
		TArray<double> R = { RadiusAt(16.0) };
		for (const double Ridge : { 58.0, 92.0, 126.0 })
		{
			Z.Add(Ridge - 4.0); R.Add(RadiusAt(Ridge - 4.0));
			Z.Add(Ridge); R.Add(RadiusAt(Ridge) + 3.5);
			Z.Add(Ridge + 4.0); R.Add(RadiusAt(Ridge + 4.0));
		}
		Z.Add(CupHeight - 5.0); R.Add(RadiusAt(CupHeight - 5.0));
		Z.Add(CupHeight); R.Add(CupTop - 3.0);
		AddRevolve(B, Base, Z, R, 36, Red, true);
	}

	/** 2.º: caja de zumo tumbada y aplastada (abollada por arriba y con una esquina chafada), con su dibujo y la pajita. */
	void AddJuiceBox(TNProcMesh::FTNProcMeshBuffers& B, const FVector& Base)
	{
		constexpr int32 NX = 6;
		constexpr int32 NY = 4;
		const double HalfL = BoxLength * 0.5;
		const double HalfW = BoxWidth * 0.5;
		auto TopZ = [HalfL, HalfW](double X, double Y)
		{
			const double Dent = 22.0 * FMath::Exp(-(FMath::Square((X + 80.0) / 70.0) + FMath::Square((Y - 45.0) / 50.0)));
			const double Corner = 32.0 * FMath::Clamp((X - (HalfL - 75.0)) / 75.0, 0.0, 1.0) * FMath::Clamp((-Y - (HalfW - 60.0)) / 60.0, 0.0, 1.0);
			return BoxHeight - Dent - Corner;
		};
		auto Top = [&](int32 i, int32 j)
		{
			const double X = -HalfL + BoxLength * i / NX;
			const double Y = -HalfW + BoxWidth * j / NY;
			return Base + FVector(X, Y, TopZ(X, Y));
		};
		const FLinearColor Orange = Pal(0xFF9F1C, 0.12f);
		const FLinearColor Band = Pal(0xFFF6E6, 0.12f);
		for (int32 i = 0; i < NX; ++i)
		{
			for (int32 j = 0; j < NY; ++j)
			{
				const bool bEdge = i == 0 || j == 0 || i == NX - 1 || j == NY - 1;
				B.AddQuad(Top(i, j), Top(i + 1, j), Top(i + 1, j + 1), Top(i, j + 1), FVector::UpVector, bEdge ? Pal(0xFFB347, 0.12f) : Band);
			}
		}
		// Paredes: naranja con una franja blanca, siguiendo el borde abollado de arriba.
		TArray<FIntPoint> Ring;
		for (int32 i = 0; i < NX; ++i) { Ring.Add(FIntPoint(i, 0)); }
		for (int32 j = 0; j < NY; ++j) { Ring.Add(FIntPoint(NX, j)); }
		for (int32 i = NX; i > 0; --i) { Ring.Add(FIntPoint(i, NY)); }
		for (int32 j = NY; j > 0; --j) { Ring.Add(FIntPoint(0, j)); }
		const double Levels[] = { 0.0, BoxHeight * 0.34, BoxHeight * 0.54, 1e9 };
		const FVector Center = Base + FVector(0.0, 0.0, BoxHeight * 0.5);
		for (int32 e = 0; e < Ring.Num(); ++e)
		{
			const FVector Ta = Top(Ring[e].X, Ring[e].Y);
			const FVector Tb = Top(Ring[(e + 1) % Ring.Num()].X, Ring[(e + 1) % Ring.Num()].Y);
			const FVector Out = ((Ta + Tb) * 0.5 - Center) * FVector(1.0, 1.0, 0.0);
			for (int32 l = 0; l < 3; ++l)
			{
				const double Za = FMath::Min(Levels[l + 1], Ta.Z - Base.Z);
				const double Zb = FMath::Min(Levels[l + 1], Tb.Z - Base.Z);
				if (Za <= Levels[l] && Zb <= Levels[l]) { continue; }
				const FVector A0(Ta.X, Ta.Y, Base.Z + Levels[l]);
				const FVector B0(Tb.X, Tb.Y, Base.Z + Levels[l]);
				const FVector B1(Tb.X, Tb.Y, Base.Z + FMath::Max(Levels[l], Zb));
				const FVector A1(Ta.X, Ta.Y, Base.Z + FMath::Max(Levels[l], Za));
				B.AddQuad(A0, B0, B1, A1, Out, l == 1 ? Band : Orange);
			}
		}
		// Dibujo en la cara que mira a la cámara (+X): una naranja con su brillo y una hoja.
		const double FaceX = Base.X + HalfL + 0.8;
		const FVector FruitC(FaceX, Base.Y - 22.0, Base.Z + 50.0);
		AddFlat(B, CirclePoints(FruitC, FVector(0.0, 1.0, 0.0), FVector(0.0, 0.0, 1.0), 31.0, 18), FVector(1.0, 0.0, 0.0), Pal(0xFF7A00));
		AddFlat(B, CirclePoints(FruitC + FVector(0.6, -9.0, 9.0), FVector(0.0, 1.0, 0.0), FVector(0.0, 0.0, 1.0), 8.0, 10), FVector(1.0, 0.0, 0.0), Pal(0xFFC266));
		AddFlat(B, { FVector(FaceX + 0.6, Base.Y - 18.0, Base.Z + 80.0), FVector(FaceX + 0.6, Base.Y + 6.0, Base.Z + 92.0),
			FVector(FaceX + 0.6, Base.Y + 22.0, Base.Z + 84.0), FVector(FaceX + 0.6, Base.Y + 2.0, Base.Z + 76.0) }, FVector(1.0, 0.0, 0.0), Pal(0x3FAE49));
		// Agujero de la pajita y la pajita doblada, a rayas blancas y rosas.
		const FVector Hole = Base + FVector(-95.0, 52.0, TopZ(-95.0, 52.0) + 0.6);
		AddFlat(B, CirclePoints(Hole, FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), 12.0, 10), FVector::UpVector, Pal(0x6B4A2A));
		const FVector Bend = Hole + FVector(0.0, 0.0, 88.0);
		const FVector Tip = Bend + FVector(-30.0, 26.0, 22.0);
		int32 Stripe = 0;
		for (const TPair<FVector, FVector>& Leg : { TPair<FVector, FVector>(Hole, Bend), TPair<FVector, FVector>(Bend, Tip) })
		{
			const int32 Pieces = FMath::Max(1, FMath::RoundToInt32(FVector::Distance(Leg.Key, Leg.Value) / 14.0));
			for (int32 p = 0; p < Pieces; ++p)
			{
				const FVector A = FMath::Lerp(Leg.Key, Leg.Value, static_cast<double>(p) / Pieces);
				const FVector C = FMath::Lerp(Leg.Key, Leg.Value, static_cast<double>(p + 1) / Pieces);
				TNProcMesh::TNProcAddCylinder(B, A, C, 8.0, 8.0, 10, (Stripe++ % 2) ? Pal(0xFF6F9C) : Pal(0xFAFAFA), p == Pieces - 1);
			}
		}
	}

	/** Media anchura de la chancla (fracción) a lo largo de la suela, del talón (0) a la punta (1). */
	double SoleHalfWidth(double U)
	{
		static const double Us[] = { 0.0, 0.12, 0.36, 0.56, 0.72, 0.88, 1.0 };
		static const double Ws[] = { 0.72, 0.8, 0.7, 0.86, 1.0, 0.95, 0.7 };
		for (int32 i = 0; i < 6; ++i)
		{
			if (U <= Us[i + 1]) { return FMath::Lerp(Ws[i], Ws[i + 1], (U - Us[i]) / (Us[i + 1] - Us[i])); }
		}
		return Ws[6];
	}

	/** 3.º: chancla (suela turquesa, plantilla clara y tira rosa en Y); el talón hacia la cámara. */
	void AddFlipFlop(TNProcMesh::FTNProcMeshBuffers& B, const FVector& Base)
	{
		auto PointAt = [&Base](double U, double Side, double Inset)
		{
			return FVector2D(Base.X + SoleHeelX - U * SoleLength, Base.Y + Side * (SoleHalfWidth(U) * SoleWidth * 0.5 - Inset));
		};
		auto Outline = [&](double Inset)
		{
			TArray<FVector2D> Poly;
			constexpr int32 Steps = 16;
			for (int32 s = 0; s <= Steps; ++s) { Poly.Add(PointAt(0.03 + 0.94 * s / Steps, -1.0, Inset)); }
			// Punta redondeada.
			const FVector2D TipC = PointAt(0.97, 0.0, 0.0);
			const double TipR = SoleHalfWidth(0.97) * SoleWidth * 0.5 - Inset;
			for (int32 a = 1; a < 6; ++a)
			{
				const double Ang = -HALF_PI + PI * a / 6.0;
				Poly.Add(TipC + FVector2D(-FMath::Cos(Ang) * TipR * 0.45, FMath::Sin(Ang) * TipR));
			}
			for (int32 s = Steps; s >= 0; --s) { Poly.Add(PointAt(0.03 + 0.94 * s / Steps, 1.0, Inset)); }
			// Talón redondeado.
			const FVector2D HeelC = PointAt(0.03, 0.0, 0.0);
			const double HeelR = SoleHalfWidth(0.03) * SoleWidth * 0.5 - Inset;
			for (int32 a = 1; a < 6; ++a)
			{
				const double Ang = HALF_PI + PI * a / 6.0;
				Poly.Add(HeelC + FVector2D(-FMath::Cos(Ang) * HeelR * 0.45, FMath::Sin(Ang) * HeelR));
			}
			return Poly;
		};
		B.AddPrism(Outline(0.0), Base.Z + SoleThick * 0.72, Base.Z, Pal(0x1FB5A8, 0.05f));
		B.AddPrism(Outline(9.0), Base.Z + SoleThick, Base.Z + SoleThick * 0.72 - 1.0, Pal(0x9FEADF, 0.05f));
		// Tira en Y: del palito de entre los dedos a los dos lados, en arco por encima.
		const FVector2D Toe2 = PointAt(0.8, 0.0, 0.0);
		const FVector Toe(Toe2.X, Toe2.Y, Base.Z + SoleThick);
		const FLinearColor Strap = Pal(0xFF5E8A, 0.1f);
		TNProcMesh::TNProcAddCylinder(B, Toe, Toe + FVector(0.0, 0.0, 20.0), 7.0, 7.0, 8, Strap);
		for (const double Side : { -1.0, 1.0 })
		{
			const FVector2D Post2 = PointAt(0.5, Side, 26.0);
			const FVector Post(Post2.X, Post2.Y, Base.Z + SoleThick);
			const FVector Mid = FMath::Lerp(Post, Toe, 0.45) + FVector(0.0, 0.0, 62.0);
			const FVector End = Toe + FVector(0.0, 0.0, 16.0);
			FVector Prev = Post;
			constexpr int32 Pieces = 9;
			for (int32 p = 1; p <= Pieces; ++p)
			{
				const double T = static_cast<double>(p) / Pieces;
				const FVector Next = Post * FMath::Square(1.0 - T) + Mid * (2.0 * (1.0 - T) * T) + End * FMath::Square(T);
				TNProcMesh::TNProcAddCylinder(B, Prev, Next, 9.0, 9.0, 8, Strap, false);
				Prev = Next;
			}
		}
	}

	/** Tapón de botella azul (con estrías) y lata de refresco roja tumbada y medio enterrada. */
	void AddLitter(TNProcMesh::FTNProcMeshBuffers& B)
	{
		const FVector CapBase(300.0, -40.0, -4.0);
		AddRevolve(B, CapBase, { 0.0, CapHeight - 4.0, CapHeight }, { CapRadius, CapRadius, CapRadius - 4.0 }, 24, Pal(0x2F7BE8, 0.1f), true);
		for (int32 k = 0; k < 18; ++k)
		{
			const double A = TNProcMap::TwoPi * k / 18.0;
			const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0.0);
			B.AddBox(CapBase + Dir * CapRadius + FVector(0.0, 0.0, CapHeight * 0.45), FVector(-Dir.Y, Dir.X, 0.0), FVector(3.0, 2.2, CapHeight * 0.4), Pal(0x1E5CB8, 0.1f));
		}
		AddFlat(B, CirclePoints(CapBase + FVector(0.0, 0.0, CapHeight + 0.5), FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), CapRadius - 12.0, 20),
			FVector::UpVector, Pal(0x5B9BF0, 0.1f));

		const double CanX = -430.0;
		const double CanY = 470.0;
		const double CanZ = CanRadius - 30.0;
		const FVector CanStart(CanX, CanY, CanZ);
		const FVector CanEnd(CanX, CanY + CanLength, CanZ);
		const FVector Neck(0.0, 16.0, 0.0);
		const FLinearColor Silver = Pal(0xCFD4DC, 0.9f);
		TNProcMesh::TNProcAddCylinder(B, CanStart, CanStart + Neck, CanRadius - 12.0, CanRadius, 24, Silver);
		TNProcMesh::TNProcAddCylinder(B, CanStart + Neck, CanEnd - Neck, CanRadius, CanRadius, 24, Pal(0xE63946, 0.55f), false);
		TNProcMesh::TNProcAddCylinder(B, CanEnd - Neck, CanEnd, CanRadius, CanRadius - 12.0, 24, Silver);
		const FVector BandMid = FMath::Lerp(CanStart, CanEnd, 0.55);
		TNProcMesh::TNProcAddCylinder(B, BandMid - FVector(0.0, 34.0, 0.0), BandMid + FVector(0.0, 34.0, 0.0), CanRadius + 0.8, CanRadius + 0.8, 24, Pal(0xF5F5F5, 0.4f), false);
	}

	/** Conchas, una estrella de mar y piedrecitas en la arena, delante del podio. */
	void AddSandDecor(TNProcMesh::FTNProcMeshBuffers& B)
	{
		struct FShellSpot { FVector2D At; double Size; float Turn; uint32 Hex; };
		const FShellSpot Shells[] = { { FVector2D(430.0, 130.0), 34.0, 0.4f, 0xFF9A80 }, { FVector2D(560.0, -190.0), 28.0, -0.9f, 0xFFD4BA },
			{ FVector2D(250.0, 420.0), 24.0, 2.1f, 0xFFE3A8 } };
		for (const FShellSpot& Spot : Shells)
		{
			TArray<FVector2D> Fan;
			Fan.Add(Spot.At);
			for (int32 j = 0; j <= 8; ++j)
			{
				const double A = Spot.Turn + PI * (0.1 + 0.8 * j / 8.0);
				const double Scallop = 1.0 + 0.06 * FMath::Abs(FMath::Sin(j * PI * 0.5));
				Fan.Add(Spot.At + FVector2D(FMath::Cos(A), FMath::Sin(A)) * (Spot.Size * Scallop));
			}
			B.AddPrism(Fan, 6.0, -2.0, Pal(Spot.Hex));
		}
		TArray<FVector2D> Star;
		const FVector2D StarC(380.0, 330.0);
		for (int32 i = 0; i < 10; ++i)
		{
			const double A = PI * i / 5.0 + 0.3;
			const double R = (i % 2) ? 12.0 : 34.0;
			Star.Add(StarC + FVector2D(FMath::Cos(A) * R, FMath::Sin(A) * R));
		}
		B.AddPrism(Star, 7.0, -2.0, Pal(0xFF8C42));
		TNProcMesh::TNProcAddBoulder(B, FVector(520.0, 260.0, 2.0), 22.0, 18.0, 17u, Pal(0xA39A8C));
		TNProcMesh::TNProcAddBoulder(B, FVector(480.0, -420.0, 2.0), 16.0, 12.0, 29u, Pal(0x8F877A));
		TNProcMesh::TNProcAddBoulder(B, FVector(640.0, 40.0, 2.0), 12.0, 9.0, 43u, Pal(0xB5AC9C));
	}

	/** Mar hasta el horizonte: turquesa en la orilla y azul cada vez más hondo. */
	void AddSea(TNProcMesh::FTNProcMeshBuffers& B)
	{
		const double Xs[] = { ShoreX - 200.0, ShoreX - 700.0, ShoreX - 2200.0, -12000.0, -45000.0, -140000.0, -500000.0 };
		const double Ys[] = { 4000.0, 4000.0, 6000.0, 12000.0, 40000.0, 120000.0, 600000.0 };
		const uint32 Hexes[] = { 0x5ED6D0, 0x3CC3CB, 0x2AA6C6, 0x1F8BBE, 0x1B72AE, 0x175D9C, 0x154E8A };
		for (int32 r = 0; r + 1 < 7; ++r)
		{
			const FLinearColor Near = Pal(Hexes[r], 0.18f);
			const FLinearColor Far = Pal(Hexes[r + 1], 0.18f);
			// Dos triángulos por franja con el color de cada borde (el color de vértice hace el degradado).
			const int32 Base = B.Verts.Num();
			const FVector P[4] = { FVector(Xs[r], -Ys[r], SeaZ), FVector(Xs[r], Ys[r], SeaZ), FVector(Xs[r + 1], Ys[r + 1], SeaZ), FVector(Xs[r + 1], -Ys[r + 1], SeaZ) };
			const FLinearColor Cols[4] = { Near, Near, Far, Far };
			B.AddQuad(P[0], P[1], P[2], P[3], FVector::UpVector, Near);
			for (int32 v = Base; v < B.Verts.Num(); ++v)
			{
				B.Colors[v] = FMath::Abs(B.Verts[v].X - Xs[r]) < 1.0 ? Cols[0] : Cols[2];
			}
		}
	}

	/** Espuma de la orilla: dos cintas onduladas blancas (el escenario las mueve adelante y atrás). */
	void AddFoam(TNProcMesh::FTNProcMeshBuffers& B)
	{
		for (int32 Band = 0; Band < 2; ++Band)
		{
			const double X = ShoreX - 40.0 - 170.0 * Band;
			const double Width = Band == 0 ? 34.0 : 18.0;
			constexpr int32 Steps = 60;
			for (int32 s = 0; s < Steps; ++s)
			{
				const double Y0 = -3600.0 + 7200.0 * s / Steps;
				const double Y1 = -3600.0 + 7200.0 * (s + 1) / Steps;
				auto Wave = [Band](double Y) { return 26.0 * FMath::Sin(Y * 0.006 + Band * 1.7) + 10.0 * FMath::Sin(Y * 0.021); };
				const double Z = SeaZ + 1.5 + Band;
				B.AddQuad(FVector(X + Wave(Y0), Y0, Z), FVector(X + Wave(Y1), Y1, Z), FVector(X + Wave(Y1) - Width, Y1, Z), FVector(X + Wave(Y0) - Width, Y0, Z),
					FVector::UpVector, Pal(Band == 0 ? 0xFFFFFF : 0xDDF6F4));
			}
		}
	}

	/** Palmera: tronco curvo con anillos y penacho de hojas que caen (dobles caras). */
	void AddPalm(TNProcMesh::FTNProcMeshBuffers& B, const FVector& Base, double Height, const FVector& Lean, uint32 Seed)
	{
		constexpr int32 Segments = 9;
		auto TrunkAt = [&](double T) { return Base + Lean * (Height * 0.22 * T * T) + FVector(0.0, 0.0, Height * T); };
		for (int32 s = 0; s < Segments; ++s)
		{
			const double T0 = static_cast<double>(s) / Segments;
			const double T1 = static_cast<double>(s + 1) / Segments;
			TNProcMesh::TNProcAddCylinder(B, TrunkAt(T0), TrunkAt(T1), Height * FMath::Lerp(0.03, 0.018, T0), Height * FMath::Lerp(0.03, 0.018, T1), 8,
				(s % 2) ? Pal(0x8D6B4A) : Pal(0x7A5A3C), false);
		}
		const FVector Crown = TrunkAt(1.0);
		for (int32 k = 0; k < 3; ++k)
		{
			const double A = TNProcMap::TwoPi * k / 3.0 + Seed;
			AddRevolve(B, Crown + FVector(FMath::Cos(A), FMath::Sin(A), 0.0) * (Height * 0.025) - FVector(0.0, 0.0, Height * 0.03),
				{ 0.0, Height * 0.02, Height * 0.035 }, { Height * 0.012, Height * 0.02, Height * 0.008 }, 7, Pal(0x6B4A2A), true);
		}
		constexpr int32 Fronds = 9;
		for (int32 f = 0; f < Fronds; ++f)
		{
			const double A = TNProcMap::TwoPi * (f + 0.3 * TNProcMesh::TNProcHashNoise(f, 1, Seed)) / Fronds;
			const FVector Out(FMath::Cos(A), FMath::Sin(A), 0.0);
			const FVector Side(-Out.Y, Out.X, 0.0);
			const double Len = Height * (0.42 + 0.08 * TNProcMesh::TNProcHashNoise(f, 2, Seed));
			const FVector Mid = Crown + Out * (Len * 0.5) + FVector(0.0, 0.0, Len * 0.12);
			const FVector Tip = Crown + Out * Len - FVector(0.0, 0.0, Len * 0.32);
			const double HalfW = Len * 0.09;
			const FLinearColor Leaf = (f % 2) ? Pal(0x4CAF50) : Pal(0x3E9443);
			const FVector Pts[4] = { Crown, Mid + Side * HalfW, Tip, Mid - Side * HalfW };
			for (const double Face : { 1.0, -1.0 })
			{
				B.AddTri(Pts[0], Pts[1], Pts[3], FVector(0.0, 0.0, Face), Leaf);
				B.AddTri(Pts[1], Pts[2], Pts[3], FVector(0.0, 0.0, Face), Leaf * 0.9f);
			}
		}
	}

	/** Islita de arena con selva y palmeras de 280 m (una palmera de 10 m a escala). */
	void AddIsland(TNProcMesh::FTNProcMeshBuffers& B, const FVector& Center, double Radius, int32 Palms, uint32 Seed)
	{
		AddRevolve(B, Center, { 0.0, 600.0, 1300.0, 1700.0 }, { Radius, Radius * 0.78, Radius * 0.42, Radius * 0.1 }, 18, Pal(0xE9CD95), true);
		for (int32 j = 0; j < 5; ++j)
		{
			const double A = TNProcMap::TwoPi * (j + 0.5 * TNProcMesh::TNProcHashNoise(j, 3, Seed)) / 5.0;
			const double R = Radius * 0.35 * (0.5 + 0.5 * TNProcMesh::TNProcHashNoise(j, 4, Seed));
			const double Bush = Radius * (0.16 + 0.05 * TNProcMesh::TNProcHashNoise(j, 5, Seed));
			AddRevolve(B, Center + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 900.0), { 0.0, Bush * 0.6, Bush * 1.0, Bush * 1.15 },
				{ Bush, Bush * 0.85, Bush * 0.45, 0.0 }, 10, (j % 2) ? Pal(0x3F8F3A) : Pal(0x2E7331), false);
		}
		for (int32 p = 0; p < Palms; ++p)
		{
			const double A = TNProcMap::TwoPi * (p + 0.4 * TNProcMesh::TNProcHashNoise(p, 6, Seed)) / Palms;
			const double R = Radius * 0.3 * (0.4 + 0.6 * (0.5 + 0.5 * TNProcMesh::TNProcHashNoise(p, 7, Seed)));
			const FVector Foot = Center + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 1000.0);
			const FVector Lean = FVector(FMath::Cos(A), FMath::Sin(A), 0.0);
			AddPalm(B, Foot, Real(10.0) * (0.8 + 0.25 * (0.5 + 0.5 * TNProcMesh::TNProcHashNoise(p, 8, Seed))), Lean, Seed + p);
		}
	}

	/** Concha trofeo (la reina: rosa y violeta con el borde dorado), de pie en el plano YZ con la charnela abajo. */
	void AddTrophyShell(TNProcMesh::FTNProcMeshBuffers& M)
	{
		constexpr int32 Fan = 18;
		constexpr int32 Rings = 5;
		constexpr int32 Ribs = 9;
		const double Spread = FMath::DegreesToRadians(80.0);
		auto Rib = [&](double A) { return FMath::Abs(FMath::Sin((A / Spread + 1.0) * 0.5 * Ribs * PI)); };
		auto Point = [&](int32 i, int32 j, double Side)
		{
			const double A = FMath::Lerp(-Spread, Spread, static_cast<double>(i) / Fan);
			const double T = static_cast<double>(j) / Rings;
			const double R = 36.0 * (1.0 + 0.07 * Rib(A)) * T;
			const double Bulge = (8.0 * (1.0 - T * T) + 1.7 * Rib(A) * T) * Side;
			return FVector(Bulge, FMath::Sin(A) * R, 4.0 + FMath::Cos(A) * R);
		};
		const FLinearColor Hinge = Pal(0x7A1F66, 0.3f);
		const FLinearColor Middle = Pal(0xD9559F, 0.3f);
		const FLinearColor Edge = Pal(0xFFC2E3, 0.3f);
		for (const double Side : { 1.0, -1.0 })
		{
			for (int32 j = 0; j < Rings; ++j)
			{
				for (int32 i = 0; i < Fan; ++i)
				{
					FLinearColor C = j + 1 == Rings ? Edge : TNProcMesh::TNProcLerpColor(Hinge, Middle, static_cast<float>(j + 1) / Rings);
					if (((i + 1) / 2) % 2 == 0) { C = WithMetal(C * 0.84f, C.A); }
					M.AddQuad(Point(i, j, Side), Point(i + 1, j, Side), Point(i + 1, j + 1, Side), Point(i, j + 1, Side), FVector(Side, 0.0, 0.0), C);
				}
			}
		}
		const FLinearColor Rim = Pal(0xF2C14E, 0.85f);
		for (int32 i = 0; i < Fan; ++i)
		{
			const double Am = FMath::Lerp(-Spread, Spread, (i + 0.5) / Fan);
			M.AddQuad(Point(i, Rings, 1.0), Point(i + 1, Rings, 1.0), Point(i + 1, Rings, -1.0), Point(i, Rings, -1.0),
				FVector(0.0, FMath::Sin(Am), FMath::Cos(Am)), Rim);
		}
		for (const double Sy : { -1.0, 1.0 })
		{
			for (const double Side : { 1.0, -1.0 })
			{
				const FVector Off(2.5 * Side, 0.0, 0.0);
				M.AddQuad(FVector(0.0, Sy * 3.0, 6.0) + Off, FVector(0.0, Sy * 17.0, 10.0) + Off, FVector(0.0, Sy * 14.0, -3.0) + Off,
					FVector(0.0, Sy * 3.0, -2.0) + Off, FVector(Side, 0.0, 0.0), Pal(0x9A2B7F, 0.3f));
			}
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Montaje
// ─────────────────────────────────────────────────────────────────────────────

ATN_RacePodiumStage::ATN_RacePodiumStage()
{
	using namespace TNRacePodiumDetail;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	// Después de la animación: la concha va entre las manos de la pose de este fotograma.
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	bReplicates = false;
	SetCanBeDamaged(false);

	StageRoot = CreateDefaultSubobject<USceneComponent>(TEXT("StageRoot"));
	StageRoot->SetMobility(EComponentMobility::Movable);
	SetRootComponent(StageRoot);

	auto MakeMesh = [this](const TCHAR* Name, bool bShadow)
	{
		UStaticMeshComponent* Comp = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Comp->SetupAttachment(StageRoot);
		SetupPrimitive(Comp, bShadow);
		return Comp;
	};
	SetMesh = MakeMesh(TEXT("SetMesh"), true);
	SeaMesh = MakeMesh(TEXT("SeaMesh"), false);
	FoamMesh = MakeMesh(TEXT("FoamMesh"), false);
	IslandMesh = MakeMesh(TEXT("IslandMesh"), false);
	TrophyMesh = MakeMesh(TEXT("TrophyMesh"), true);

	// La malla de las tortugas es la del personaje (TNTurtleArt::ApplyBody en BeginPlay), no una ruta fija.
	for (int32 i = 0; i < 3; ++i)
	{
		USkeletalMeshComponent* Turtle = CreateDefaultSubobject<USkeletalMeshComponent>(*FString::Printf(TEXT("Turtle%d"), i));
		Turtle->SetupAttachment(StageRoot);
		// Como en BP_TortugaCharacter: la malla mira a su +Y; girada -90 mira al +X del escenario (hacia la cámara).
		Turtle->SetRelativeLocation(PlaceSpots[i]);
		Turtle->SetRelativeRotation(FRotator(0.f, PlaceYaw[i], 0.f));
		Turtle->SetRelativeScale3D(FVector(TurtleScale));
		Turtle->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		SetupPrimitive(Turtle, true);
		Turtles.Add(Turtle);

		UStaticMeshComponent* Helmet = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Helmet%d"), i));
		Helmet->SetupAttachment(Turtle);
		SetupPrimitive(Helmet, true);
		Helmets.Add(Helmet);
	}
	bPlaceUsed.Init(false, 3);
	PlacePose = { ETNTurtleCelebration::Trophy, ETNTurtleCelebration::Disappointed, ETNTurtleCelebration::Tantrum };
	NextBlink = { 1.2f, 2.3f, 3.1f };
	BlinkAge = { 1.f, 1.f, 1.f };

	// Luces del podio (canal 2): sol cálido desde delante, arriba y a la derecha (donde la pantalla pinta el sol),
	// principal con sombras, relleno frío desde la izquierda y contraluz.
	SunLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("SunLight"));
	SunLight->SetupAttachment(StageRoot);
	SunLight->SetMobility(EComponentMobility::Movable);
	SunLight->SetRelativeRotation(FRotator(-48.f, 155.f, 0.f));
	SunLight->SetIntensity(3.4f);
	SunLight->SetLightColor(FLinearColor(1.f, 0.95f, 0.86f));
	SunLight->SetCastShadows(false);
	SunLight->SetAtmosphereSunLight(false);
	SunLight->LightingChannels = PodiumChannel();

	KeyLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("KeyLight"));
	KeyLight->SetupAttachment(StageRoot);
	KeyLight->SetRelativeLocation(FVector(520.f, -260.f, 720.f));
	SetupPointLight(KeyLight, 130.f, FLinearColor(1.f, 0.93f, 0.82f), 3000.f, true);

	FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
	FillLight->SetupAttachment(StageRoot);
	FillLight->SetRelativeLocation(FVector(650.f, 560.f, 260.f));
	SetupPointLight(FillLight, 45.f, FLinearColor(0.78f, 0.9f, 1.f), 3000.f, false);

	RimLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("RimLight"));
	RimLight->SetupAttachment(StageRoot);
	RimLight->SetRelativeLocation(FVector(-620.f, 120.f, 820.f));
	SetupPointLight(RimLight, 90.f, FLinearColor(0.85f, 0.97f, 1.f), 3000.f, false);

	TrophyLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("TrophyLight"));
	TrophyLight->SetupAttachment(StageRoot);
	SetupPointLight(TrophyLight, 30.f, FLinearColor(1.f, 0.72f, 0.88f), 260.f, false);

	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(StageRoot);
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetVolumetricFog(false);
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetMotionBlur(false);
}

ATN_RacePodiumStage* ATN_RacePodiumStage::Get(UWorld* World)
{
	if (!World) { return nullptr; }
	for (TActorIterator<ATN_RacePodiumStage> It(World); It; ++It)
	{
		return *It;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	return World->SpawnActor<ATN_RacePodiumStage>(TNRacePodiumDetail::StageLocation, FRotator::ZeroRotator, Params);
}

void ATN_RacePodiumStage::BeginPlay()
{
	using namespace TNRacePodiumDetail;
	Super::BeginPlay();
	for (int32 i = 0; i < Turtles.Num(); ++i)
	{
		// La tortuga del personaje (malla, materiales y escala) en su sitio del podio, con la animación de la del jugador,
		// sin personaje: la pose de celebración la pide el podio.
		USkeletalMeshComponent* Turtle = Turtles[i];
		if (TNTurtleArt::ApplyBody(Turtle, FTransform(FRotator(0.f, PlaceYaw[i], 0.f), PlaceSpots[i], FVector(TurtleScale)))) { DefaultMaterials.Reset(); }
		Turtle->SetAnimInstanceClass(UTN_TurtleAnimInstance::StaticClass());
		Turtle->SetVisibility(false, true);
	}
	BuildSet();
	Target = UKismetRenderingLibrary::CreateRenderTarget2D(this, CaptureWidth, CaptureHeight, RTF_RGBA16f, FLinearColor(0.f, 0.f, 0.f, 0.f));
	Capture->TextureTarget = Target;
	Capture->ClearShowOnlyComponents();
	TInlineComponentArray<UPrimitiveComponent*> Prims(this);
	for (UPrimitiveComponent* Prim : Prims) { Capture->ShowOnlyComponent(Prim); }
	PlaceCamera();
}

void ATN_RacePodiumStage::BuildSet()
{
	using namespace TNRacePodiumDetail;
	if (bBuilt) { return; }
	bBuilt = true;
	UMaterialInterface* VertexColorMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Cosmetics/Materials/M_CosmeticVertexColor.M_CosmeticVertexColor"));

	TNProcMesh::FTNProcMeshBuffers Set;
	AddSand(Set);
	AddCup(Set, FVector(PlaceSpots[0].X, PlaceSpots[0].Y, 0.0));
	AddJuiceBox(Set, FVector(PlaceSpots[1].X, PlaceSpots[1].Y, 0.0));
	AddFlipFlop(Set, FVector(PlaceSpots[2].X, PlaceSpots[2].Y, 0.0));
	AddLitter(Set);
	AddSandDecor(Set);
	// El alfa del color de vértice es el brillo metálico del material (FixedAlpha -2: el de los propios buffers).
	SetMesh->SetStaticMesh(TNProcRuntimeMesh::MakeStaticMesh(this, Set, VertexColorMat, false, 0.f, 1.f, -2.f));

	TNProcMesh::FTNProcMeshBuffers Sea;
	AddSea(Sea);
	SeaMesh->SetStaticMesh(TNProcRuntimeMesh::MakeStaticMesh(this, Sea, VertexColorMat, false, 0.f, 1.f, -2.f));

	TNProcMesh::FTNProcMeshBuffers Foam;
	AddFoam(Foam);
	FoamMesh->SetStaticMesh(TNProcRuntimeMesh::MakeStaticMesh(this, Foam, VertexColorMat, false, 0.f, 1.f, 0.f));

	// Islas en el horizonte vistas desde la cámara: una a la derecha del todo (a unos 13° del centro de la imagen, a
	// 2,5 km) y otra a la izquierda del podio (a unos 4°, a 3 km), para que sus palmeras enmarquen el cielo.
	TNProcMesh::FTNProcMeshBuffers Islands;
	AddIsland(Islands, FVector(-244700.0, -39400.0, SeaZ), 13000.0, 4, 5u);
	AddIsland(Islands, FVector(-295000.0, 41400.0, SeaZ), 16000.0, 5, 11u);
	IslandMesh->SetStaticMesh(TNProcRuntimeMesh::MakeStaticMesh(this, Islands, VertexColorMat, false, 0.f, 1.f, 0.f));

	TNProcMesh::FTNProcMeshBuffers Shell;
	AddTrophyShell(Shell);
	TrophyMesh->SetStaticMesh(TNProcRuntimeMesh::MakeStaticMesh(this, Shell, VertexColorMat, false, 0.f, 1.f, -2.f));
	TrophyMesh->SetVisibility(false);
	TrophyLight->SetVisibility(false);
}

void ATN_RacePodiumStage::PlaceCamera()
{
	using namespace TNRacePodiumDetail;
	const float Fov = FMath::Clamp(CVarPodiumFOV.GetValueOnGameThread(), 15.f, 80.f);
	const double Distance = FMath::Clamp(static_cast<double>(CVarPodiumDistance.GetValueOnGameThread()), 800.0, 6000.0);
	const FVector CamPos(Distance, 0.0, CameraHeight);
	FRotator Rot = (FVector(0.0, 0.0, LookHeight) - CamPos).Rotation();
	// Gira la cámara hacia la izquierda de la imagen (+Y) para que el podio quede a la derecha (PodiumScreenX).
	Rot.Yaw -= FMath::RadiansToDegrees(FMath::Atan((2.f * PodiumScreenX - 1.f) * FMath::Tan(FMath::DegreesToRadians(Fov * 0.5f))));
	Capture->SetRelativeLocationAndRotation(CamPos, Rot);
	Capture->FOVAngle = Fov;
}

void ATN_RacePodiumStage::SetPodium(const TArray<FTN_TurtleLook>& Looks)
{
	for (int32 i = 0; i < Turtles.Num(); ++i)
	{
		USkeletalMeshComponent* Turtle = Turtles[i];
		const bool bUse = Looks.IsValidIndex(i);
		bPlaceUsed[i] = bUse;
		Turtle->SetVisibility(bUse, true);
		if (!bUse) { continue; }
		UTN_CosmeticLook::ApplyLook(this, Turtle, Helmets[i], Looks[i], DefaultMaterials);
		// Las piezas de Arte de la tortuga (TNTurtleArt) solo se ven en la captura, como ella.
		TArray<UPrimitiveComponent*> Pieces;
		TNTurtleArt::GetPieceComponents(Turtle, Pieces);
		for (UPrimitiveComponent* Piece : Pieces) { Capture->ShowOnlyComponent(Piece); }
		if (UTN_TurtleAnimInstance* Anim = Cast<UTN_TurtleAnimInstance>(Turtle->GetAnimInstance())) { Anim->SetCelebration(PlacePose[i]); }
	}
	const bool bTrophy = bPlaceUsed[0];
	TrophyMesh->SetVisibility(bTrophy);
	TrophyLight->SetVisibility(bTrophy);
}

void ATN_RacePodiumStage::SetLive(bool bInLive)
{
	bLive = bInLive;
	SetActorTickEnabled(bInLive);
	for (USkeletalMeshComponent* Turtle : Turtles) { Turtle->SetComponentTickEnabled(bInLive); }
	Capture->bCaptureEveryFrame = bInLive;
	if (bInLive)
	{
		PlaceCamera();
		Capture->CaptureScene();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Animación
// ─────────────────────────────────────────────────────────────────────────────

void ATN_RacePodiumStage::Tick(float DeltaSeconds)
{
	using namespace TNRacePodiumDetail;
	Super::Tick(DeltaSeconds);
	Time += DeltaSeconds;
	PlaceCamera();
	// La espuma va y viene con las olas.
	FoamMesh->SetRelativeLocation(FVector(55.0 * FMath::Sin(Time * 0.8f) + 20.0 * FMath::Sin(Time * 1.9f + 1.f), 0.0, 0.0));
	TickFaces(DeltaSeconds);
	TickTrophy();
}

void ATN_RacePodiumStage::TickFaces(float DeltaSeconds)
{
	for (int32 i = 0; i < Turtles.Num(); ++i)
	{
		if (!bPlaceUsed[i]) { continue; }
		USkeletalMeshComponent* Turtle = Turtles[i];
		UTN_TurtleAnimInstance* Anim = Cast<UTN_TurtleAnimInstance>(Turtle->GetAnimInstance());
		if (Anim && Anim->GetCelebration() != PlacePose[i]) { Anim->SetCelebration(PlacePose[i]); }
		const float T = Anim ? Anim->GetCelebrationTime() : Time;

		// Cara de cada pose, acompasada con su bucle.
		float Open = 0.3f;
		float Smile = 1.f;
		float Blush = 0.f;
		float Tired = 0.f;
		float Squeeze = 0.f;
		switch (PlacePose[i])
		{
		case ETNTurtleCelebration::Trophy:
		{
			const float Phase = FMath::Frac(T / 1.6f);
			Open = 0.75f + 0.25f * FMath::Abs(FMath::Sin(Phase * 2.f * PI));
			Blush = 0.8f;
			Squeeze = (Phase > 0.55f && Phase < 0.72f) ? 1.f : 0.f;
			break;
		}
		case ETNTurtleCelebration::Disappointed:
		{
			const float U = FMath::Fmod(T, 3.2f);
			Tired = 0.85f;
			Smile = 0.f;
			Open = (U > 0.9f && U < 1.5f) ? 0.12f + 0.45f * FMath::Sin((U - 0.9f) / 0.6f * PI) : 0.1f;
			break;
		}
		case ETNTurtleCelebration::Tantrum:
			Squeeze = 1.f;
			Smile = 0.f;
			Blush = 1.f;
			Open = 0.6f + 0.4f * FMath::Abs(FMath::Sin(T * 2.f * PI / 1.2f * 2.f));
			break;
		default:
			break;
		}
		if (UMaterialInstanceDynamic* MID = UTN_CosmeticLook::GetBodyMaterial(Turtle))
		{
			MID->SetScalarParameterValue(TEXT("MouthOpen"), Open);
			MID->SetScalarParameterValue(TEXT("MouthSmile"), Smile);
			MID->SetScalarParameterValue(TEXT("FaceBlush"), Blush);
			MID->SetScalarParameterValue(TEXT("EyeTired"), Tired);
			MID->SetScalarParameterValue(TEXT("EyeSqueeze"), Squeeze);
		}

		// Parpadeo de vez en cuando (la de la pataleta tiene los ojos apretados).
		BlinkAge[i] += DeltaSeconds;
		if (Time >= NextBlink[i])
		{
			BlinkAge[i] = 0.f;
			NextBlink[i] = Time + FMath::FRandRange(2.2f, 4.6f);
		}
		const float Blink = BlinkAge[i] < 0.16f && Squeeze < 0.5f ? FMath::Sin(BlinkAge[i] / 0.16f * PI) : 0.f;
		UTN_CosmeticLook::SetEyeState(Turtle, Blink, 0.f);
	}
}

void ATN_RacePodiumStage::TickTrophy()
{
	if (!bPlaceUsed[0]) { return; }
	USkeletalMeshComponent* First = Turtles[0];
	static const FName LeftHand(TEXT("LeftHand"));
	static const FName RightHand(TEXT("RightHand"));
	FVector Hold = First->GetComponentLocation() + FVector(0.0, 0.0, 190.0);
	if (First->GetBoneIndex(LeftHand) != INDEX_NONE && First->GetBoneIndex(RightHand) != INDEX_NONE)
	{
		Hold = (First->GetBoneLocation(LeftHand) + First->GetBoneLocation(RightHand)) * 0.5 + FVector(0.0, 0.0, 4.0);
	}
	// La concha mira a la cámara (su cara es ±X) y se mece un poco; su luz rosa late.
	const FVector ToCamera = (Capture->GetComponentLocation() - Hold).GetSafeNormal2D();
	FRotator Rot = ToCamera.Rotation();
	Rot.Yaw += 14.f * FMath::Sin(Time * 2.6f);
	Rot.Roll = 7.f * FMath::Sin(Time * 3.3f);
	TrophyMesh->SetWorldLocationAndRotation(Hold, Rot);
	TrophyLight->SetWorldLocation(Hold + FVector(0.0, 0.0, 30.0) + ToCamera * 45.0);
	TrophyLight->SetIntensity(30.f + 12.f * FMath::Sin(Time * 5.f));
}

bool ATN_RacePodiumStage::GetNameTagUV(int32 Place, FVector2D& OutUV) const
{
	if (!Turtles.IsValidIndex(Place) || !bPlaceUsed[Place] || !Capture) { return false; }
	const USkeletalMeshComponent* Turtle = Turtles[Place];
	static const FName HeadBone(TEXT("Head"));
	FVector Above = Turtle->GetComponentLocation() + FVector(0.0, 0.0, 190.0);
	if (Turtle->GetBoneIndex(HeadBone) != INDEX_NONE) { Above = Turtle->GetBoneLocation(HeadBone) + FVector(0.0, 0.0, 80.0); }
	// La primera lleva la concha en alto: la etiqueta va por encima de la concha.
	if (Place == 0 && TrophyMesh && TrophyMesh->IsVisible()) { Above = TrophyMesh->GetComponentLocation() + FVector(0.0, 0.0, 95.0); }
	const FVector Local = Capture->GetComponentTransform().InverseTransformPositionNoScale(Above);
	if (Local.X <= 1.0) { return false; }
	const double TanH = FMath::Tan(FMath::DegreesToRadians(static_cast<double>(Capture->FOVAngle) * 0.5));
	const double TanV = TanH / CaptureAspect;
	OutUV = FVector2D(0.5 + 0.5 * (Local.Y / Local.X) / TanH, 0.5 - 0.5 * (Local.Z / Local.X) / TanV);
	return true;
}
