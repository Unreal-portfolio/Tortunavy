#include "World/Beach/TN_BeachMovingPlatform.h"
#include "Multiplayer/TN_LocalViews.h"
#include "World/Beach/TN_BeachTrapSynthComponent.h"
#include "World/ProcMap/TN_ProcWaterActors.h"
#include "Core/TN_Log.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "ProceduralMeshComponent.h"
#include "UObject/Package.h"
#include "TN_BeachRideKit.h"
#include "TN_BeachTrapKit.h"

/**
 * Geometría de la plataforma móvil (espacio del marco: X hacia el mar, origen en la arena). La balsa y la bandeja se
 * construyen con su cara de arriba en Z = 0 del componente que se mueve (RideRoot), así que colocarla es poner su cara de
 * arriba donde toca. El charco es un cráter de revolución (el de la plataforma que se rompe, más ancho y lleno de agua);
 * la torre, un bloque de arena de molde con almenas en los lados ±Y.
 */
namespace TNBeachPlatformDetail
{
	using FBuffers = TNBeachTrapKit::FBuffers;
	using FHulls = TNBeachTrapKit::FHulls;

	/** Charco: cresta de la orilla, agua 42 cm por debajo y taludes de 30° (se sube andando y se sale nadando). */
	constexpr double RimH = 170.0;
	constexpr double WaterBelowRim = 42.0;
	constexpr double RimWidth = 90.0;
	constexpr double Tan30 = 0.57735;
	constexpr int32 RingSectors = 20;

	/**
	 * Hasta dónde se oyen las salidas y llegadas (cm desde la cámara local). La balsa y el ascensor no paran nunca: cada
	 * pocos segundos, un roce y un chof (o un crujido y un golpe). Oídos a 40 m, con seis u ocho repartidos por la playa, la
	 * carrera entera sonaba a una fuente que no cesa; ahora solo se oyen si hay alguien lo bastante cerca para usarlas.
	 */
	constexpr double SoundReach = 1800.0;

	/** Distancia de la cámara local más cercana a Where (con la pantalla partida, cualquiera); enorme si no hay jugador local. */
	inline double CameraDistance(const UWorld* World, const FVector& Where)
	{
		return TNLocalViews::ClosestCameraDistance(World, Where);
	}

	enum class ERide : uint8
	{
		FlipFlop,
		Surfboard,
		Frisbee,
		Lid,
	};

	/** Contorno en planta de cada balsa (centrado; X a lo largo). */
	TArray<FVector2D> RideOutline(ERide Kind, double HalfX, double HalfY)
	{
		TArray<FVector2D> Out;
		constexpr int32 Steps = 28;
		for (int32 i = 0; i < Steps; ++i)
		{
			const double A = TNPlaygroundKit::KitTwoPi * i / Steps;
			const double C = FMath::Cos(A);
			const double S = FMath::Sin(A);
			double X = HalfX * C;
			double Y = HalfY * S;
			switch (Kind)
			{
			case ERide::FlipFlop:
				// Más ancha en los dedos (+X) que en el talón, con un poco de arco.
				Y *= 0.82 + 0.18 * C;
				break;
			case ERide::Surfboard:
				// Punta afilada hacia +X.
				Y *= C > 0.0 ? (1.0 - 0.55 * C * C * C) : 1.0;
				break;
			case ERide::Lid:
			{
				// Rectángulo de esquinas redondas (superelipse).
				const double Px = FMath::Pow(FMath::Abs(C), 0.35);
				const double Py = FMath::Pow(FMath::Abs(S), 0.35);
				X = HalfX * (C < 0.0 ? -Px : Px);
				Y = HalfY * (S < 0.0 ? -Py : Py);
				break;
			}
			default:
				break;
			}
			Out.Add(FVector2D(X, Y));
		}
		return Out;
	}

	/** La balsa con sus detalles, cara de arriba en Z = 0. */
	void BuildRide(FBuffers& B, ERide Kind, double HalfX, double HalfY, double Thick, uint32 Seed)
	{
		const TArray<FVector2D> Outline = RideOutline(Kind, HalfX, HalfY);
		const FVector Mid(0.0, 0.0, -0.5 * Thick);
		switch (Kind)
		{
		case ERide::FlipFlop:
		{
			const FLinearColor Sole = TNPlaygroundKit::ToyColor(static_cast<int32>(Seed % 7u), 0.1f);
			TNPlaygroundKit::AddSlab(B, Mid, FVector::ForwardVector, FVector::RightVector, FVector::UpVector, Outline, Thick, TNPlaygroundKit::Shade(Sole, 0.8));
			// Plantilla de otro color y la tira en V (baja, solo dibujo).
			TArray<FVector2D> Insole;
			for (const FVector2D& P : Outline)
			{
				Insole.Add(P * 0.9);
			}
			TNPlaygroundKit::AddSlab(B, FVector(0.0, 0.0, 1.0), FVector::ForwardVector, FVector::RightVector, FVector::UpVector, Insole, 2.0, Sole);
			const FLinearColor Strap = TNPlaygroundKit::ToyColor(static_cast<int32>(Seed % 7u) + 3, 0.2f);
			const FVector Toe(0.55 * HalfX, 0.0, 4.0);
			for (const double Side : { -1.0, 1.0 })
			{
				TNPlaygroundKit::AddRod(B, Toe, FVector(-0.05 * HalfX, Side * 0.78 * HalfY, 4.0), 10.0, 6, Strap, FVector::UpVector);
			}
			TNPlaygroundKit::AddBall(B, Toe + FVector(0.0, 0.0, 4.0), 16.0, 8, TNPlaygroundKit::Shade(Strap, 0.8));
			break;
		}
		case ERide::Surfboard:
		{
			const FLinearColor Deck = TNPlaygroundKit::ToyColor(static_cast<int32>(Seed % 7u) + 1, 0.3f);
			TNPlaygroundKit::AddSlab(B, Mid, FVector::ForwardVector, FVector::RightVector, FVector::UpVector, Outline, Thick, TNPlaygroundKit::Rgb(0xF7F3EA, 0.3f));
			// Franja central y una aleta.
			TNPlaygroundKit::AddAxisBox(B, FVector(-0.1 * HalfX, 0.0, 1.0), FVector(0.72 * HalfX, 0.18 * HalfY, 1.0), Deck);
			TNPlaygroundKit::AddAxisBox(B, FVector(-0.1 * HalfX, 0.0, 1.5), FVector(0.72 * HalfX, 0.05 * HalfY, 1.0), TNPlaygroundKit::Shade(Deck, 0.75));
			TNPlaygroundKit::AddXfBox(B, FTransform(FVector(-0.75 * HalfX, 0.0, -Thick - 20.0)), FVector::ZeroVector, FVector(22.0, 3.0, 20.0), Deck);
			break;
		}
		case ERide::Frisbee:
		{
			const FLinearColor Disc = TNPlaygroundKit::ToyColor(static_cast<int32>(Seed % 7u) + 4, 0.35f);
			TNPlaygroundKit::AddFrustum(B, FVector(0.0, 0.0, -Thick), FVector(0.0, 0.0, 0.0), HalfX * 0.94, HalfX, 28, TNPlaygroundKit::Shade(Disc, 0.85), Disc, true, true);
			// Aros en relieve y un dibujo de estrella.
			TNPlaygroundKit::AddAnnulus(B, FVector(0.0, 0.0, 1.0), FVector::UpVector, HalfX * 0.62, HalfX * 0.68, 28, TNPlaygroundKit::Shade(Disc, 1.15));
			TNPlaygroundKit::AddAnnulus(B, FVector(0.0, 0.0, 1.0), FVector::UpVector, HalfX * 0.9, HalfX * 0.96, 28, TNPlaygroundKit::Shade(Disc, 0.8));
			TNPlaygroundKit::AddStarfish(B, FVector(0.0, 0.0, 1.0), FVector::UpVector, FVector::ForwardVector, HalfX * 0.35, 2.0, TNPlaygroundKit::Rgb(0xFFF1A8, 0.3f));
			break;
		}
		default:
		{
			const FLinearColor Lid = TNPlaygroundKit::ToyColor(static_cast<int32>(Seed % 7u) + 2, 0.25f);
			TNPlaygroundKit::AddSlab(B, Mid, FVector::ForwardVector, FVector::RightVector, FVector::UpVector, Outline, Thick, TNPlaygroundKit::Rgb(0xF2F2EE, 0.35f));
			// Reborde de color y la pestaña para abrirla.
			TArray<FVector2D> Inner;
			for (const FVector2D& P : Outline)
			{
				Inner.Add(P * 0.86);
			}
			TNPlaygroundKit::AddSlab(B, FVector(0.0, 0.0, 1.0), FVector::ForwardVector, FVector::RightVector, FVector::UpVector, Inner, 2.0, Lid);
			TNPlaygroundKit::AddAxisBox(B, FVector(HalfX + 18.0, 0.0, -0.5 * Thick), FVector(20.0, 0.3 * HalfY, 0.3 * Thick), Lid);
			break;
		}
		}
	}

	/** Casco de la balsa (la envolvente del contorno, de su cara de abajo a la de arriba). */
	TArray<FVector> RideHull(ERide Kind, double HalfX, double HalfY, double Thick)
	{
		TArray<FVector> Pts;
		for (const FVector2D& P : RideOutline(Kind, HalfX, HalfY))
		{
			Pts.Add(FVector(P.X, P.Y, 0.0));
			Pts.Add(FVector(P.X, P.Y, -Thick));
		}
		return Pts;
	}

	/** Bandeja o disco del ascensor (cara de arriba en Z = 0). */
	void BuildLift(FBuffers& B, bool bRound, double Half, uint32 Seed)
	{
		const FLinearColor Col = TNPlaygroundKit::ToyColor(static_cast<int32>(Seed % 7u) + 5, 0.3f);
		if (bRound)
		{
			TNPlaygroundKit::AddFrustum(B, FVector(0.0, 0.0, -34.0), FVector(0.0, 0.0, 0.0), Half * 0.93, Half, 26, TNPlaygroundKit::Shade(Col, 0.85), Col, true, true);
			TNPlaygroundKit::AddAnnulus(B, FVector(0.0, 0.0, 1.0), FVector::UpVector, Half * 0.86, Half * 0.93, 26, TNPlaygroundKit::Shade(Col, 0.78));
		}
		else
		{
			TNPlaygroundKit::AddAxisBox(B, FVector(0.0, 0.0, -17.0), FVector(Half, Half, 17.0), Col);
			// Borde levantado de bandeja (bajo: se pasa andando).
			for (const double Side : { -1.0, 1.0 })
			{
				TNPlaygroundKit::AddAxisBox(B, FVector(0.0, Side * (Half - 6.0), 5.0), FVector(Half, 6.0, 5.0), TNPlaygroundKit::Shade(Col, 0.85));
			}
		}
		// Argollas de las cuerdas.
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector At(((i & 1) ? 0.7 : -0.7) * Half, ((i & 2) ? 0.7 : -0.7) * Half, 6.0);
			TNPlaygroundKit::AddBall(B, At, 9.0, 6, TNPlaygroundKit::Rgb(0xD9D2C4, 0.4f));
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// ATN_BeachMovingPlatform
// ─────────────────────────────────────────────────────────────────────────────

ATN_BeachMovingPlatform::ATN_BeachMovingPlatform()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(1.f);

	Frame = CreateDefaultSubobject<USceneComponent>(TEXT("Frame"));
	Frame->SetupAttachment(GetRootComponent());

	BaseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BaseMesh"));
	BaseMesh->SetupAttachment(Frame);
	TNBeachTrapKit::ConfigureVisual(BaseMesh);

	BaseCollision = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BaseCollision"));
	BaseCollision->SetupAttachment(Frame);
	TNBeachTrapKit::ConfigureSolid(BaseCollision, true);

	RideRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RideRoot"));
	RideRoot->SetupAttachment(Frame);

	RideMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RideMesh"));
	RideMesh->SetupAttachment(RideRoot);
	TNBeachTrapKit::ConfigureVisual(RideMesh);

	// La plataforma: base móvil (subobjeto por defecto: se nombra igual en todas las máquinas).
	RideCollision = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("RideCollision"));
	RideCollision->SetupAttachment(RideRoot);
	TNBeachTrapKit::ConfigureSolid(RideCollision, false);
}

void ATN_BeachMovingPlatform::ApplySpec()
{
	const double Fit = TNBeachTrapKit::FitRadius(Spec.Element, Spec.SizeScale);
	const uint32 Seed = TNBeachTrapKit::SeedOf(Spec.Seed, 97u);
	bElevator = (static_cast<uint32>(Spec.Seed) & 1u) != 0u;
	if (bElevator)
	{
		BuildElevator(Fit, Seed);
	}
	else
	{
		BuildFerry(Fit, Seed);
	}
	LastAlpha = -1.0;
	LiftPlacedAt = -1.0;
	bLiftCatchingUp = false;
	PlaceRide(TNBeachTrapKit::ServerNow(GetWorld()));
}

void ATN_BeachMovingPlatform::BuildFerry(double Fit, uint32 Seed)
{
	using namespace TNBeachPlatformDetail;
	const ERide Kind = static_cast<ERide>((Seed >> 1) % 4u);
	const double Sc = FMath::Clamp(Fit / 1200.0, 0.85, 1.15);
	switch (Kind)
	{
	case ERide::FlipFlop:
		RideHalfX = 340.0 * Sc;
		RideHalfY = 125.0 * Sc;
		RideThick = 42.0;
		break;
	case ERide::Surfboard:
		RideHalfX = 300.0 * Sc;
		RideHalfY = 100.0 * Sc;
		RideThick = 36.0;
		break;
	case ERide::Frisbee:
		RideHalfX = 280.0 * Sc;
		RideHalfY = RideHalfX;
		RideThick = 38.0;
		break;
	default:
		RideHalfX = 270.0 * Sc;
		RideHalfY = 185.0 * Sc;
		RideThick = 40.0;
		break;
	}
	bRideRound = Kind == ERide::Frisbee;

	// Charco: la balsa llega de orilla a orilla (su punta se mete 60 cm en el talud) y todo cabe en la huella.
	const double OuterExtra = WaterBelowRim / Tan30 + RimWidth + RimH / Tan30;
	const double MaxEdge = FMath::Max(360.0, 0.97 * Fit - OuterExtra);
	const double MaxTravel = FMath::Max(300.0, 2.0 * (MaxEdge + 60.0) - 2.0 * RideHalfX);
	Travel = FMath::Clamp(Spec.Extent > 0.f ? static_cast<double>(Spec.Extent) : 800.0, 300.0, MaxTravel);
	WaterEdgeR = FMath::Max(RideHalfY + 220.0, 0.5 * (Travel + 2.0 * RideHalfX) - 60.0);
	WaterZ = RimH - WaterBelowRim;
	RideTopZ = WaterZ + 34.0;
	const double Rf = FMath::Max(80.0, WaterEdgeR - WaterZ / Tan30);
	const double Rp = WaterEdgeR + WaterBelowRim / Tan30;
	const double Ro = Rp + RimWidth + RimH / Tan30;

	TNBeachTrapKit::FBuffers Base;
	TNBeachTrapKit::FHulls Hulls;
	const TArray<FVector2D> Profile = { FVector2D(Rf - 10.0, -3.0), FVector2D(Rf, 0.0), FVector2D(WaterEdgeR, WaterZ), FVector2D(Rp, RimH),
		FVector2D(Rp + 0.5 * RimWidth, RimH + 8.0), FVector2D(Rp + RimWidth, RimH), FVector2D(Ro, 0.0), FVector2D(Ro + 25.0, -25.0) };
	const FLinearColor WetIn = TNPlaygroundKit::Mix(TNBeachTrapKit::SandWet(), TNBeachTrapKit::SandSide(), 0.4);
	const TArray<FLinearColor> Cols = { TNBeachTrapKit::SandDeep(), TNBeachTrapKit::SandWet(), WetIn, TNBeachTrapKit::SandTop(), TNBeachTrapKit::SandTop(),
		TNBeachTrapKit::SandSide(), TNBeachTrapKit::SandSide() };
	TNBeachTrapKit::AddLatheProfile(Base, FVector::ZeroVector, Profile, Cols, 36, 0.02, Seed);
	const TArray<FVector2D> Section = { FVector2D(Rf, 0.0), FVector2D(Rp, RimH), FVector2D(Rp + RimWidth, RimH), FVector2D(Ro, 0.0), FVector2D(Ro, -30.0),
		FVector2D(Rf, -30.0) };
	for (int32 s = 0; s < RingSectors; ++s)
	{
		const double A0 = TNPlaygroundKit::KitTwoPi * s / RingSectors;
		const double A1 = TNPlaygroundKit::KitTwoPi * (s + 1) / RingSectors;
		Hulls.Add(TNBeachTrapKit::HullRingSector(FVector::ZeroVector, A0, A1, Section));
	}

	// Agua: más oscura en el centro, espuma en la orilla y algo flotando.
	const FLinearColor Shallow = TNPlaygroundKit::Rgb(0x55C7E0, 0.6f);
	const FLinearColor Deep = TNPlaygroundKit::Rgb(0x2A8FB8, 0.6f);
	TNPlaygroundKit::AddDisc(Base, FVector(0.0, 0.0, WaterZ), FVector::UpVector, WaterEdgeR + 25.0, 36, Shallow);
	TNPlaygroundKit::AddDisc(Base, FVector(0.0, 0.0, WaterZ + 0.6), FVector::UpVector, 0.6 * WaterEdgeR, 28, Deep);
	TNPlaygroundKit::AddAnnulus(Base, FVector(0.0, 0.0, WaterZ + 1.0), FVector::UpVector, WaterEdgeR - 22.0, WaterEdgeR + 2.0, 36, TNPlaygroundKit::Rgb(0xF4FBFF, 0.3f));
	for (int32 i = 0; i < 3; ++i)
	{
		const double Ang = TNPlaygroundKit::KitTwoPi * (0.2 + 0.33 * i + 0.1 * TNBeachTrapKit::Hash01(i, 8, Seed));
		const FVector At((0.55 + 0.2 * TNBeachTrapKit::Hash01(i, 9, Seed)) * WaterEdgeR * FMath::Cos(Ang), 0.8 * WaterEdgeR * FMath::Sin(Ang), WaterZ + 2.0);
		if (i == 0)
		{
			TNPlaygroundKit::AddShellFan(Base, At, FVector::UpVector, FVector::ForwardVector, 30.0, TNBeachTrapKit::ShellTone(i));
		}
		else
		{
			// Hoja y chapa flotando.
			TNPlaygroundKit::AddEllipsoid(Base, At, FVector::ForwardVector, FVector::RightVector, FVector::UpVector, FVector(38.0, 16.0, 2.0), 8, 3,
				i == 1 ? TNPlaygroundKit::Rgb(0x6FA84A) : TNPlaygroundKit::Rgb(0xE63946, 0.4f));
		}
	}
	// Embarcaderos: dos palos de polo clavados en el talud a cada lado de donde atraca la balsa (solo dibujo).
	for (const double End : { -1.0, 1.0 })
	{
		const double Dx = End * (WaterEdgeR + 70.0);
		for (const double Side : { -1.0, 1.0 })
		{
			const double Dy = Side * (RideHalfY + 60.0);
			const double SlopeZ = FMath::Min(RimH, WaterZ + (FVector2D(Dx, Dy).Size() - WaterEdgeR) * Tan30);
			TNPlaygroundKit::AddStick(Base, FVector(Dx, Dy, SlopeZ + 60.0), FVector::UpVector, FVector::ForwardVector, 170.0, 40.0, 10.0,
				TNPlaygroundKit::Rgb(0xE9CC98, 0.05f));
			TNPlaygroundKit::AddBall(Base, FVector(Dx, Dy, SlopeZ + 150.0), 16.0, 8, TNPlaygroundKit::ToyColor(static_cast<int32>(Seed % 7u) + (End > 0.0 ? 1 : 4), 0.3f));
		}
	}
	TNBeachTrapKit::SetMesh(BaseMesh, this, Base, TN_ART("Beach.MovingPlatform.FerryBase"));
	BaseCollision->SetCollisionConvexMeshes(Hulls);

	TNBeachTrapKit::FBuffers Ride;
	BuildRide(Ride, Kind, RideHalfX, RideHalfY, RideThick, Seed);
	TNBeachTrapKit::SetMesh(RideMesh, this, Ride, TN_ART("Beach.MovingPlatform.Raft"));
	TArray<TArray<FVector>> RideHulls;
	RideHulls.Add(RideHull(Kind, RideHalfX, RideHalfY, RideThick));
	RideCollision->SetCollisionConvexMeshes(RideHulls);

	Phase = TNBeachTrapKit::Hash01(6, 1, Seed) * 20.0;
	UE_LOG(LogTortunabo, Verbose, TEXT("[Playa] Plataforma móvil %s: balsa %d, recorrido %.0f cm, charco de %.0f cm."), *GetName(), static_cast<int32>(Kind), Travel,
		2.0 * WaterEdgeR);
}

void ATN_BeachMovingPlatform::BuildElevator(double Fit, uint32 Seed)
{
	using namespace TNBeachPlatformDetail;
	TowerH = FMath::Clamp(Spec.Extent > 0.f ? static_cast<double>(Spec.Extent) : 450.0, 250.0, 480.0);
	TowerHalf = FMath::Clamp(0.54 * Fit, 560.0, 680.0);
	bRideRound = ((Seed >> 1) & 1u) != 0u;
	const double LiftHalf = 195.0;
	RideHalfX = LiftHalf;
	RideHalfY = LiftHalf;
	RideThick = 34.0;
	LiftX = -(TowerHalf + LiftHalf + 12.0);
	BottomTopZ = 22.0;
	Travel = TowerH + 2.0 - BottomTopZ;

	TNBeachTrapKit::FBuffers Base;
	TNBeachTrapKit::FHulls Hulls;
	// Torre de molde con marcas de cubo, almenas en los lados ±Y y conchas pegadas.
	TNBeachTrapKit::AddSandBox(Base, &Hulls, FVector(-TowerHalf, -TowerHalf, -30.0), FVector(TowerHalf, TowerHalf, TowerH), true);
	const int32 Merlons = FMath::Max(3, FMath::RoundToInt32(2.0 * TowerHalf / 180.0));
	for (const double Side : { -1.0, 1.0 })
	{
		for (int32 i = 0; i < Merlons; ++i)
		{
			if (i % 2 == 1)
			{
				continue;
			}
			const double X = -TowerHalf + (i + 0.5) * (2.0 * TowerHalf / Merlons);
			const double HalfW = 0.5 * (2.0 * TowerHalf / Merlons);
			TNBeachTrapKit::AddSandBox(Base, &Hulls, FVector(X - HalfW, Side * TowerHalf - (Side > 0.0 ? 70.0 : 0.0), TowerH),
				FVector(X + HalfW, Side * TowerHalf + (Side > 0.0 ? 0.0 : 70.0), TowerH + 70.0), false);
		}
	}
	for (int32 i = 0; i < 6; ++i)
	{
		const double Side = (i % 2) ? 1.0 : -1.0;
		const FVector At((TNBeachTrapKit::Hash01(i, 1, Seed) - 0.5) * 1.6 * TowerHalf, Side * (TowerHalf + 2.0), 60.0 + (TowerH - 120.0) * TNBeachTrapKit::Hash01(i, 2, Seed));
		TNPlaygroundKit::AddShellFan(Base, At, FVector(0.0, Side, 0.0), FVector::UpVector, 34.0, TNBeachTrapKit::ShellTone(i));
	}
	// Bandera en una esquina.
	const FVector PoleFoot(TowerHalf - 60.0, TowerHalf - 60.0, TowerH);
	TNPlaygroundKit::AddRod(Base, PoleFoot, PoleFoot + FVector(0.0, 0.0, 260.0), 7.0, 6, TNPlaygroundKit::Rgb(0xE8D2A6), FVector::ForwardVector);
	TNPlaygroundKit::AddPennant(Base, PoleFoot + FVector(0.0, 0.0, 260.0), FVector(-1.0, 0.0, 0.0), 120.0, 70.0, TNPlaygroundKit::ToyColor(static_cast<int32>(Seed % 7u), 0.2f));

	// Grúa: mástil de palo de polo a un lado de donde llega la plataforma (no estorba al bajarse) y brazo en diagonal hasta
	// encima de ella, con una chapa por polea.
	const FLinearColor Wood = TNPlaygroundKit::Rgb(0xE2C08C, 0.05f);
	const FVector MastFoot(-TowerHalf + 50.0, LiftHalf + 45.0, TowerH);
	const double ArmZ = TowerH + 235.0;
	TNPlaygroundKit::AddStick(Base, FVector(MastFoot.X, MastFoot.Y, TowerH + 0.5 * (ArmZ - TowerH) + 10.0), FVector::UpVector, FVector::RightVector,
		ArmZ - TowerH + 40.0, 48.0, 14.0, Wood);
	const FVector ArmFrom(MastFoot.X, MastFoot.Y, ArmZ);
	const FVector ArmTo(LiftX, 0.0, ArmZ);
	const FVector ArmDir = (ArmTo - ArmFrom).GetSafeNormal();
	TNPlaygroundKit::AddStick(Base, 0.5 * (ArmFrom + ArmTo), ArmDir, FVector::CrossProduct(FVector::UpVector, ArmDir), FVector::Dist(ArmFrom, ArmTo) + 70.0, 48.0, 14.0,
		TNPlaygroundKit::Shade(Wood, 0.94));
	TNPlaygroundKit::AddFrustum(Base, FVector(LiftX, -14.0, ArmZ - 20.0), FVector(LiftX, 14.0, ArmZ - 20.0), 26.0, 26.0, 14, TNPlaygroundKit::Rgb(0xE63946, 0.4f),
		TNPlaygroundKit::Rgb(0xF4F1EA, 0.4f), true, true);
	CraneTipLocal = FVector(LiftX, 0.0, ArmZ - 44.0);
	Hulls.Add(TNPlaygroundKit::HullAxisBox(FVector(MastFoot.X, MastFoot.Y, 0.5 * (TowerH + ArmZ)), FVector(7.0, 24.0, 0.5 * (ArmZ - TowerH))));
	// Arena mojada donde se posa la plataforma.
	TNPlaygroundKit::AddDisc(Base, FVector(LiftX, 0.0, 1.5), FVector::UpVector, LiftHalf + 40.0, 18, TNBeachTrapKit::SandWet());
	TNBeachTrapKit::SetMesh(BaseMesh, this, Base, TN_ART("Beach.MovingPlatform.ElevatorBase"));
	BaseCollision->SetCollisionConvexMeshes(Hulls);

	TNBeachTrapKit::FBuffers Ride;
	BuildLift(Ride, bRideRound, LiftHalf, Seed);
	TNBeachTrapKit::SetMesh(RideMesh, this, Ride, TN_ART("Beach.MovingPlatform.Lift"));
	TArray<TArray<FVector>> RideHulls;
	RideHulls.Add(bRideRound ? TNPlaygroundKit::HullCylinder(FVector(0.0, 0.0, -34.0), 34.0, LiftHalf * 0.93, LiftHalf, 16)
		: TNPlaygroundKit::HullAxisBox(FVector(0.0, 0.0, -17.0), FVector(LiftHalf, LiftHalf, 17.0)));
	RideCollision->SetCollisionConvexMeshes(RideHulls);

	Phase = TNBeachTrapKit::Hash01(6, 2, Seed) * 20.0;
	UE_LOG(LogTortunabo, Verbose, TEXT("[Playa] Plataforma móvil %s: ascensor de %.0f cm junto a una torre de %.0f cm."), *GetName(), TowerH, 2.0 * TowerHalf);
}

void ATN_BeachMovingPlatform::BeginPlay()
{
	Super::BeginPlay();
	Voice = UTN_BeachTrapSynthComponent::AttachTo(this, Frame->GetComponentLocation() + FVector(0.0, 0.0, 200.0), 700.f, 2400.f);
	Splash.Init(this, ETNTrapBurstShape::Blob, TNPlaygroundKit::Rgb(0xE6F7FF, 0.5f), 24);
	Splash.SetMotion(-800.f, 1.2f, 26.f, 8.f, 0.35f, 0.7f);
	if (!bElevator)
	{
		SpawnWater();
	}
	else
	{
		if (HasAuthority() && bCatapultOnTop)
		{
			SpawnTopCatapult();
		}
		UWorld* World = GetWorld();
		if (World && World->IsGameWorld() && GetNetMode() != NM_DedicatedServer)
		{
			// Cuerdas: una varilla de 100 cm a lo largo de X que se estira cada fotograma.
			TNBeachTrapKit::FBuffers Rod;
			TNPlaygroundKit::AddRod(Rod, FVector::ZeroVector, FVector(100.0, 0.0, 0.0), 3.5, 6, TNPlaygroundKit::Rgb(0xF2E6C8, 0.05f), FVector::UpVector);
			UStaticMesh* RodMesh = TNPlaygroundKit::BuildMesh(this, Rod, TNPlaygroundKit::VertexColorMaterial());
			for (int32 i = 0; i < 4; ++i)
			{
				UStaticMeshComponent* Rope = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
				Rope->SetStaticMesh(RodMesh);
				TNBeachTrapKit::ConfigureVisual(Rope);
				Rope->SetCastShadow(false);
				Rope->SetupAttachment(Frame);
				Rope->RegisterComponent();
				// Pieza de arte: varilla de 100 a lo largo de +X desde su origen; se estira con el componente.
				TNArt::ApplyToComponent(Rope, TN_ART("Beach.MovingPlatform.Rope"));
				Rope->SetAbsolute(true, true, true);
				Ropes.Add(Rope);
			}
			UpdateRopes();
		}
	}
}

void ATN_BeachMovingPlatform::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ATN_ProcWaterVolume* Volume = Water.Get())
	{
		Volume->Destroy();
	}
	Water.Reset();
	if (HasAuthority())
	{
		if (ATN_BeachElement* Child = TopCatapult.Get())
		{
			Child->Destroy();
		}
	}
	TopCatapult.Reset();
	Super::EndPlay(EndPlayReason);
}

void ATN_BeachMovingPlatform::SpawnWater()
{
	using namespace TNBeachPlatformDetail;
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || Water.IsValid())
	{
		return;
	}
	// Local en cada máquina (la natación la predicen servidor y cliente), como el agua de meta. Solo lo hondo: en la
	// orilla, quien hace pie ya no nada y sale andando por el talud.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = this;
	Params.ObjectFlags |= RF_Transient;
	const FTransform FrameXf = Frame->GetComponentTransform();
	ATN_ProcWaterVolume* Volume = World->SpawnActor<ATN_ProcWaterVolume>(ATN_ProcWaterVolume::StaticClass(), FrameXf, Params);
	if (!Volume)
	{
		return;
	}
	const double R = FMath::Max(120.0, WaterEdgeR - 110.0);
	const double HalfZ = 0.5 * (WaterZ + 60.0);
	const FVector Center = FrameXf.TransformPosition(FVector(0.0, 0.0, 0.5 * (WaterZ - 60.0)));
	Volume->AddWaterBox(Center, FVector(0.92 * R, 0.38 * R, HalfZ));
	Volume->AddWaterBox(Center, FVector(0.38 * R, 0.92 * R, HalfZ));
	Volume->AddWaterBox(Center, FVector(0.7 * R, 0.7 * R, HalfZ));
	Water = Volume;
}

void ATN_BeachMovingPlatform::SpawnTopCatapult()
{
	UWorld* World = GetWorld();
	if (!World || TopCatapult.IsValid())
	{
		return;
	}
	// Del tamaño que cabe arriba (la catapulta no baja de lo que necesita su cazo); se orienta sola hacia el mar.
	FTNBeachElementSpec ChildSpec;
	ChildSpec.Element = ETNBeachElement::Catapult;
	ChildSpec.Seed = static_cast<int32>(TNBeachTrapKit::SeedOf(Spec.Seed, 101u) & 0x7FFFFFFFu);
	ChildSpec.SizeScale = static_cast<float>(0.95 * TowerHalf / TNBeach::FootprintRadius(ETNBeachElement::Catapult));
	ChildSpec.Extent = 0.f;
	const FTransform FrameXf = Frame->GetComponentTransform();
	const FTransform ChildXf(FrameXf.GetRotation(), FrameXf.TransformPosition(FVector(0.0, 0.0, TowerH)));
	TopCatapult = ATN_BeachElement::SpawnElement(World, ChildXf, ChildSpec);
	UE_LOG(LogTortunabo, Verbose, TEXT("[Playa] Plataforma móvil %s: catapulta arriba %s."), *GetName(), *GetNameSafe(TopCatapult.Get()));
}

// ─────────────────────────────────────────────────────────────────────────────
// Movimiento
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachMovingPlatform::PlaceRide(double Now)
{
	const double T = Now + Phase;
	if (bElevator)
	{
		const double Speed = FMath::Max(50.0, static_cast<double>(LiftSpeed));
		const double Alpha = TNBeachRideKit::ShuttleAlpha(T, LiftDwellBottom, Travel / Speed, LiftDwellTop);
		const double Scheduled = BottomTopZ + Travel * Alpha;
		double Z = Scheduled;
		if (LiftPlacedAt >= 0.0)
		{
			if (bLiftCatchingUp)
			{
				// Tras soltar a quien tenía debajo vuelve a su horario a la velocidad del ascensor, sin caer de golpe.
				Z = FMath::Max(Scheduled, LiftZ - Speed * FMath::Max(0.0, Now - LiftPlacedAt));
			}
			// Se colocaba sin barrido y atravesaba a quien estuviera debajo al bajar: ahora se apoya en su cabeza.
			const double Floor = LiftFloorUnderneath();
			if (Floor > Z)
			{
				Z = FMath::Min(Floor, BottomTopZ + Travel);
			}
		}
		bLiftCatchingUp = Z > Scheduled + 0.5;
		LiftZ = Z;
		LiftPlacedAt = Now;
		const double Yaw = bRideRound ? FMath::Fmod(T * 12.0, 360.0) : 0.0;
		RideRoot->SetRelativeLocationAndRotation(FVector(LiftX, 0.0, Z), FRotator(0.0, Yaw, 0.0));
		LastAlpha = Alpha;
		return;
	}
	const double Alpha = TNBeachRideKit::ShuttleAlpha(T, FerryDwell, Travel / FMath::Max(50.0, static_cast<double>(FerrySpeed)), FerryDwell);
	// Se mece un poco en el agua (pequeño: quien va encima no lo nota como un salto).
	const double Bob = 3.0 * FMath::Sin(T * 1.7);
	const double Roll = 1.2 * FMath::Sin(T * 1.3 + 0.7);
	const double Yaw = bRideRound ? FMath::Fmod(T * 14.0, 360.0) : 0.0;
	RideRoot->SetRelativeLocationAndRotation(FVector(-0.5 * Travel + Travel * Alpha, 0.0, RideTopZ + Bob), FRotator(0.0, Yaw, Roll));
	LastAlpha = Alpha;
}

double ATN_BeachMovingPlatform::LiftFloorUnderneath() const
{
	double Floor = -UE_DOUBLE_BIG_NUMBER;
	UWorld* World = GetWorld();
	if (!bElevator || !World)
	{
		return Floor;
	}
	const FTransform FrameXf = Frame->GetComponentTransform();
	const double Bottom = LiftZ - RideThick;
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		const ACharacter* Walker = *It;
		const UCapsuleComponent* Capsule = IsValid(Walker) ? Walker->GetCapsuleComponent() : nullptr;
		if (!Capsule)
		{
			continue;
		}
		const FVector Feet = TNBeachRideKit::FeetIn(FrameXf, Walker);
		const double Radius = Capsule->GetScaledCapsuleRadius();
		const double Dx = FMath::Abs(Feet.X - LiftX);
		const double Dy = FMath::Abs(Feet.Y);
		const bool bUnder = bRideRound ? FMath::Square(Dx) + FMath::Square(Dy) < FMath::Square(RideHalfX + Radius)
			: Dx < RideHalfX + Radius && Dy < RideHalfY + Radius;
		const double Head = Feet.Z + 2.0 * Capsule->GetScaledCapsuleHalfHeight();
		// Quien va encima (pies por encima de la cara de abajo) o la cruza de lado (cabeza por encima de la de arriba) no la
		// sostiene: de eso ya se encarga el movimiento del personaje.
		if (!bUnder || Feet.Z >= Bottom || Head > LiftZ)
		{
			continue;
		}
		Floor = FMath::Max(Floor, Head + RideThick + 2.0);
	}
	return Floor;
}

void ATN_BeachMovingPlatform::UpdateRopes()
{
	if (Ropes.Num() < 4)
	{
		return;
	}
	const FTransform FrameXf = Frame->GetComponentTransform();
	const FTransform RideXf = RideRoot->GetComponentTransform();
	const FVector Tip = FrameXf.TransformPosition(CraneTipLocal);
	for (int32 i = 0; i < 4; ++i)
	{
		UStaticMeshComponent* Rope = Ropes[i].Get();
		if (!Rope)
		{
			continue;
		}
		const FVector Ring = RideXf.TransformPosition(FVector(((i & 1) ? 0.7 : -0.7) * RideHalfX, ((i & 2) ? 0.7 : -0.7) * RideHalfY, 8.0));
		const FVector Span = Ring - Tip;
		const double Len = Span.Size();
		if (Len < 1.0)
		{
			continue;
		}
		Rope->SetWorldLocationAndRotation(Tip, Span.Rotation());
		Rope->SetWorldScale3D(FVector(Len / 100.0, 1.0, 1.0));
	}
}

void ATN_BeachMovingPlatform::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Before = LastAlpha;
	const double Now = Clock.Advance(GetWorld(), DeltaSeconds);
	PlaceRide(Now);
	const double Alpha = LastAlpha;
	if (bElevator)
	{
		UpdateRopes();
	}
	if (GetNetMode() != NM_DedicatedServer && Before >= 0.0 && Voice)
	{
		// Arranca y llega: crujido de cuerda en el ascensor, chapoteo en la balsa.
		const bool bLeft = (Before <= 0.0 && Alpha > 0.0) || (Before >= 1.0 && Alpha < 1.0);
		const bool bArrived = (Before > 0.0 && Alpha <= 0.0) || (Before < 1.0 && Alpha >= 1.0);
		const FVector At = RideRoot->GetComponentLocation();
		// Los sonidos solo si la cámara local está cerca (TNBeachPlatformDetail::SoundReach), más flojos cuanto más lejos y
		// con el tono algo distinto cada vez. Las partículas de la llegada, siempre.
		float Near = 0.f;
		float Jitter = 1.f;
		if (bLeft || bArrived)
		{
			const double CamDist = TNBeachPlatformDetail::CameraDistance(GetWorld(), At);
			Near = CamDist < TNBeachPlatformDetail::SoundReach ? 1.f - 0.55f * static_cast<float>(CamDist / TNBeachPlatformDetail::SoundReach) : 0.f;
			Jitter = FMath::FRandRange(0.92f, 1.08f);
		}
		if (bLeft && Near > 0.f)
		{
			Voice->TriggerSoundAt(bElevator ? ETNBeachTrapSound::Creak : ETNBeachTrapSound::Grind, At, (bElevator ? 0.8f : 1.2f) * Jitter, 0.45f * Near);
		}
		if (bArrived)
		{
			if (bElevator)
			{
				if (Near > 0.f)
				{
					Voice->TriggerSoundAt(ETNBeachTrapSound::Thud, At, 1.1f * Jitter, 0.5f * Near);
				}
			}
			else
			{
				const FVector Nose = At + RideRoot->GetForwardVector() * (Alpha >= 1.0 ? RideHalfX : -RideHalfX);
				if (Near > 0.f)
				{
					Voice->TriggerSoundAt(ETNBeachTrapSound::Squelch, Nose, 0.9f * Jitter, 0.4f * Near);
				}
				Splash.Burst(Nose, 10, FVector::UpVector, 320.f, 0.9f, 60.f);
			}
		}
	}
	Splash.Tick(DeltaSeconds);
}
