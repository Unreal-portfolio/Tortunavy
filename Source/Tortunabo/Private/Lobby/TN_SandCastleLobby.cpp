#include "Lobby/TN_SandCastleLobby.h"
#include "Core/TN_Log.h"
#include "Lobby/TN_HQGameMode.h"
#include "Lobby/TN_LobbyReadyZone.h"
#include "Lobby/TN_TreasureChest.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Scene.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "TN_CastleKit.h"

// Un sitio en la pila por cada jugador que cabe: el montículo del kit y el castillo cuentan igual.
static_assert(ATN_SandCastleLobby::NumEggs == TNCastleKit::EggMound::NumEggs, "Un huevo del kit por cada huevo de la pila del lobby.");

namespace TNCastleDetail
{
	using namespace TNCastleKit;

	TAutoConsoleVariable<int32> CVarLobbyCastle(TEXT("TN.Lobby.Castle"), 1,
		TEXT("1 = el lobby es el castillo de arena (ATN_SandCastleLobby); 0 = escondido (vuelve a cargar el lobby)."));

	// ── Medidas (cm, locales del castillo: centro del círculo en el origen, +Y es la puerta) ──
	constexpr double R = ATN_SandCastleLobby::Radius;
	constexpr double WallT = 190.0;
	constexpr double RO = R + WallT;
	constexpr double RM = R + WallT * 0.5;
	constexpr double WallH = 600.0;
	constexpr double FloorZ = 2.0;
	/** Puerta doble: origen (umbral de la puerta 1) en la muralla, a las 12; la sala sale hacia fuera (+Y). */
	constexpr double GateY = R + 70.0;
	/** Suelo de la puerta doble, por encima de la playa de fuera (adorno a 4). */
	constexpr double GateFloorZ = 6.0;
	constexpr double CutY = ATN_SandCastleLobby::CutY;
	/** Muro interior: medio grosor (adarve de 1,7 m entre almenas) y alto. */
	constexpr double CutHalfT = 130.0;
	constexpr double CutH = 600.0;
	constexpr double WalkHalf = CutHalfT - 44.0;
	/** Torre del homenaje en el centro del muro interior. */
	constexpr double KeepR = 400.0;
	constexpr double KeepRoofZ = 900.0;
	constexpr double DoorHalfW = 130.0;
	constexpr double DoorH = 300.0;
	/**
	 * Escalera de caracol: por fuera de la torre, del suelo (lado este, a 290°, pegada al muro) a la azotea (lado oeste, a
	 * 70°) pasando por encima de la puerta y de su arco sin tocarlos; rellano hasta 100° y, desde él, escalera recta que
	 * baja por encima del muro al adarve izquierdo. Por el otro lado de la azotea, otro rellano y otra escalera igual al
	 * derecho. Los StairEntrySteps primeros peldaños salen más hacia la plaza y no tienen barandilla: se pisan desde fuera
	 * (entre el primero y el muro no cabe la tortuga).
	 */
	constexpr double StairIn = KeepR;
	constexpr double StairOut = KeepR + 210.0;
	constexpr int32 StairSteps = 36;
	constexpr double StairStartDeg = 290.0;
	constexpr int32 StairEntrySteps = 3;
	constexpr double StairEntryFlare = 35.0;
	/** Grueso de los peldaños (más que su alto, 25 cm: por debajo no se ve a través). */
	constexpr double StairStepT = 30.0;
	constexpr double StairEndDeg = 430.0;
	constexpr double LandingEndDeg = 100.0;
	constexpr double DownStepsX0 = -(KeepR + 190.0);
	constexpr int32 DownSteps = 11;
	constexpr double DownStepL = 40.0;
	/** Toboganes de los adarves: X y cara del muro por la que bajan (+1 a la plaza, -1 al patio de pruebas). */
	constexpr double SlideLeftX = -1400.0;
	constexpr double SlideRightX = 1500.0;
	constexpr double SlideRun = 560.0;
	constexpr double SlideHalfW = 95.0;
	/** Rellano del este de la azotea (grados de la torre), del que baja la escalera recta al adarve derecho. */
	constexpr double EastLandingStartDeg = 255.0;
	constexpr double EastLandingEndDeg = 285.0;
	/** Pila de huevos: centro del montículo de dos alturas en la plaza. */
	const FVector2D EggsCenter(0.0, 700.0);
	/**
	 * Cofre del tesoro (ATN_TreasureChest) en la azotea de la torre del homenaje: lo más al centro posible sin tocar el
	 * torreón (que ocupa el sur de la azotea), sobre una tarima redonda y mirando a la plaza (+Y). Entre la tarima y el
	 * torreón queda paso de un rellano al otro, y entre la tarima y las almenas del norte, una franja de medio metro.
	 */
	const FVector2D TreasureSpot(0.0, CutY + 200.0);
	constexpr double TreasureDaisR = 125.0;
	constexpr double TreasureDaisH = 24.0;

	/** Ángulo (radianes, sentido de las agujas del reloj desde +Y) de una hora del reloj. */
	double ClockAngle(double Hour)
	{
		return Hour / 12.0 * TNProcMap::TwoPi;
	}

	/** Punto a la hora Clock y a Dist del centro (12 = +Y, 3 = -X). */
	FVector2D ClockPoint(double Hour, double Dist)
	{
		const double A = ClockAngle(Hour);
		return FVector2D(-Dist * FMath::Sin(A), Dist * FMath::Cos(A));
	}

	/** Huevos: posición local (x, y) y cota de su base. El de arriba es el último. */
	FVector EggSpot(int32 Index)
	{
		return EggMoundSpot(Index, FVector(EggsCenter.X, EggsCenter.Y, 0.0), FloorZ);
	}

	/** Torres de la muralla: hora del reloj, radio y alto (irregulares a propósito) y color de la bandera. */
	struct FTowerDef
	{
		double Clock;
		double Radius;
		double Height;
		uint32 Flag;
	};
	/** Las dos de la puerta las pone la puerta doble. */
	const FTowerDef Towers[] = {
		{ 1.62, 230.0, 980.0, 0xFFCB3D }, { 2.85, 200.0, 760.0, 0x9B5DE5 },
		{ 3.66, 300.0, 1180.0, 0x4CC9F0 }, { 4.62, 210.0, 820.0, 0xFF8FB1 },
		{ 6.0, 265.0, 1060.0, 0x3DDC62 }, { 7.32, 200.0, 760.0, 0xFFB077 },
		{ 8.34, 300.0, 1250.0, 0xFF6A52 }, { 9.2, 220.0, 880.0, 0x2EC4B6 },
		{ 10.35, 250.0, 960.0, 0xFFCB3D },
	};

	/** Alto de la muralla en el ángulo A (radianes): ondulado suave entre torres, para que no sea una tapia recta. */
	double WallHeightAt(double A)
	{
		return WallH + 45.0 * FMath::Sin(A * 3.0 + 0.7) + 25.0 * FMath::Sin(A * 7.0 + 2.1);
	}

	/** Cierto si el ángulo A (radianes) cae en el hueco de la puerta doble (hasta el eje de sus torres grandes). */
	bool InGate(double A)
	{
		double X = FMath::Fmod(A, TNProcMap::TwoPi);
		if (X > PI) { X -= TNProcMap::TwoPi; }
		if (X < -PI) { X += TNProcMap::TwoPi; }
		return FMath::Abs(R * FMath::Sin(X)) < Gatehouse::BigTowerX && FMath::Cos(X) > 0.0;
	}

	/** Punto de la torre del homenaje a Rad del eje, en el ángulo A (radianes, desde +Y en el sentido del reloj) y cota Z. */
	FVector KeepPoint(double Rad, double A, double Z)
	{
		return FVector(-Rad * FMath::Sin(A), CutY + Rad * FMath::Cos(A), Z);
	}

	/**
	 * Sector macizo alrededor de la torre del homenaje (peldaño o rellano): entre los radios R0 y R1, los ángulos A0 y A1
	 * (grados) y las cotas Z0 y Z1, con sus seis caras (por debajo no se ve a través).
	 */
	void AddKeepSector(FBuffers& B, double R0, double R1, double A0Deg, double A1Deg, double Z0, double Z1, const FLinearColor& Top, const FLinearColor& Side)
	{
		const double A0 = FMath::DegreesToRadians(A0Deg), A1 = FMath::DegreesToRadians(A1Deg);
		const double Am = (A0 + A1) * 0.5;
		const FVector Up(0.0, 0.0, 1.0);
		const FVector Out(-FMath::Sin(Am), FMath::Cos(Am), 0.0);
		const FVector Fwd0(-FMath::Cos(A0), -FMath::Sin(A0), 0.0), Fwd1(-FMath::Cos(A1), -FMath::Sin(A1), 0.0);
		const FVector I0t = KeepPoint(R0, A0, Z1), O0t = KeepPoint(R1, A0, Z1), O1t = KeepPoint(R1, A1, Z1), I1t = KeepPoint(R0, A1, Z1);
		const FVector I0b = KeepPoint(R0, A0, Z0), O0b = KeepPoint(R1, A0, Z0), O1b = KeepPoint(R1, A1, Z0), I1b = KeepPoint(R0, A1, Z0);
		B.AddQuad(I0t, O0t, O1t, I1t, Up, Top);
		B.AddQuad(I0b, I1b, O1b, O0b, -Up, Side);
		B.AddQuad(O0b, O1b, O1t, O0t, Out, Side);
		B.AddQuad(I0b, I0t, I1t, I1b, -Out, Side);
		B.AddQuad(I0b, O0b, O0t, I0t, -Fwd0, Side);
		B.AddQuad(I1b, I1t, O1t, O1b, Fwd1, Side);
	}

	/**
	 * Tobogán de plástico desde el adarve del muro interior (X, cara Face: +1 plaza, -1 patio): canal con perfil que
	 * empieza a ~60° (se resbala) y acaba plano en la arena, barandillas, panza por debajo y dos pilares de arena.
	 */
	void AddWallSlide(FBuffers& B, FBuffers& Decor, double X, double Face, const FLinearColor& Tone, TNArt::FPieceLog* Log)
	{
		constexpr int32 N = 16;
		const FVector Up(0.0, 0.0, 1.0);
		const double Y0 = CutY + Face * CutHalfT;
		const FLinearColor Rail = Col(0xFFF6E8);
		auto Z = [](double T) { return FloorZ + 1.0 + (CutH - FloorZ - 1.0) * FMath::Pow(1.0 - T, 1.7); };
		for (int32 i = 0; i < N; ++i)
		{
			const double T0 = static_cast<double>(i) / N, T1 = static_cast<double>(i + 1) / N;
			const double Ya = Y0 + Face * SlideRun * T0, Yb = Y0 + Face * SlideRun * T1;
			const double Za = Z(T0), Zb = Z(T1);
			const FLinearColor Surf = (i % 2) ? Tone : Tone * 0.92f;
			B.AddQuad(FVector(X - SlideHalfW, Ya, Za), FVector(X + SlideHalfW, Ya, Za), FVector(X + SlideHalfW, Yb, Zb), FVector(X - SlideHalfW, Yb, Zb), Up, Surf);
			B.AddQuad(FVector(X - SlideHalfW - 18.0, Ya, Za - 26.0), FVector(X + SlideHalfW + 18.0, Ya, Za - 26.0), FVector(X + SlideHalfW + 18.0, Yb, Zb - 26.0),
				FVector(X - SlideHalfW - 18.0, Yb, Zb - 26.0), -Up, Tone * 0.7f);
			for (const double Sx : { -1.0, 1.0 })
			{
				// Bordes del canal: cara de dentro, cara de fuera (de la panza arriba) y tapa, con la barandilla encima.
				const double XIn = X + Sx * SlideHalfW, XOut = X + Sx * (SlideHalfW + 18.0);
				B.AddQuad(FVector(XIn, Ya, Za), FVector(XIn, Yb, Zb), FVector(XIn, Yb, Zb + 48.0), FVector(XIn, Ya, Za + 48.0), FVector(-Sx, 0.0, 0.0), Tone * 0.8f);
				B.AddQuad(FVector(XOut, Ya, Za - 26.0), FVector(XOut, Yb, Zb - 26.0), FVector(XOut, Yb, Zb + 48.0), FVector(XOut, Ya, Za + 48.0), FVector(Sx, 0.0, 0.0), Tone * 0.85f);
				B.AddQuad(FVector(XIn, Ya, Za + 48.0), FVector(XIn, Yb, Zb + 48.0), FVector(XOut, Yb, Zb + 48.0), FVector(XOut, Ya, Za + 48.0), Up, Tone * 0.9f);
				B.AddBeam(FVector(X + Sx * (SlideHalfW + 9.0), Ya, Za + 52.0), FVector(X + Sx * (SlideHalfW + 9.0), Yb, Zb + 52.0), 9.0, Rail);
			}
		}
		// Pilares de arena bajo el canal, con la cabeza inclinada como la panza (pegados a ella en toda su huella, sin
		// atravesarla ni dejar hueco).
		constexpr double PillarR = 36.0;
		constexpr int32 PillarSeg = 12;
		for (const double T : { 0.28, 0.58 })
		{
			const FVector Foot(X, Y0 + Face * SlideRun * T, 0.0);
			auto HeadZ = [&](const FVector& P) { return Z(FMath::Clamp((P.Y - Y0) / (Face * SlideRun), 0.0, 1.0)) - 27.0; };
			for (int32 k = 0; k < PillarSeg; ++k)
			{
				const double A0 = TNProcMap::TwoPi * k / PillarSeg, A1 = TNProcMap::TwoPi * (k + 1) / PillarSeg;
				const FVector P0 = Foot + FVector(FMath::Cos(A0), FMath::Sin(A0), 0.0) * PillarR;
				const FVector P1 = Foot + FVector(FMath::Cos(A1), FMath::Sin(A1), 0.0) * PillarR;
				const FVector Out(FMath::Cos((A0 + A1) * 0.5), FMath::Sin((A0 + A1) * 0.5), 0.0);
				B.AddQuad(P0, P1, P1 + Up * HeadZ(P1), P0 + Up * HeadZ(P0), Out, SandDark());
				B.AddTri(Foot + Up * HeadZ(Foot), P0 + Up * HeadZ(P0), P1 + Up * HeadZ(P1), Up, SandDark());
			}
		}
		// Conchas en los pilares y una estrella al pie.
		const FVector StarAt(X + 40.0, Y0 + Face * (SlideRun + 90.0), FloorZ);
		TNArt::FPieceScope Star(Log, TN_ART("Lobby.Castle.Starfish"), StarfishPivot(StarAt, 30.0, X * 0.01), { &Decor });
		AddStarfish(Decor, StarAt, 30.0, X * 0.01, Col(0xFF8A70));
	}

	/** Guirnalda de banderines entre dos puntos (cuerda que cuelga un poco y triángulos de colores alternos). */
	void AddBunting(FBuffers& Decor, const FVector& A, const FVector& B, int32 Seed)
	{
		static const uint32 Colors[5] = { 0xFF6A52, 0xFFF6E8, 0x2EC4B6, 0xFFCB3D, 0x9B5DE5 };
		const int32 N = FMath::Max(3, FMath::RoundToInt32(FVector::Dist(A, B) / 45.0));
		const FVector Side = FVector::CrossProduct(B - A, FVector(0.0, 0.0, 1.0)).GetSafeNormal();
		FVector Prev = A;
		for (int32 i = 1; i <= N; ++i)
		{
			const double T = static_cast<double>(i) / N;
			const FVector P = FMath::Lerp(A, B, T) - FVector(0.0, 0.0, 40.0 * 4.0 * T * (1.0 - T));
			Decor.AddBeam(Prev, P, 1.2, Col(0xC9A56A));
			const FVector Mid = (Prev + P) * 0.5;
			const FLinearColor C = Col(Colors[(i + Seed) % 5]);
			Decor.AddTri(Prev, P, Mid - FVector(0.0, 0.0, 34.0), Side, C);
			Decor.AddTri(Prev, P, Mid - FVector(0.0, 0.0, 34.0), -Side, C * 0.85f);
			Prev = P;
		}
	}
}

bool ATN_SandCastleLobby::IsEnabled()
{
	return TNCastleDetail::CVarLobbyCastle.GetValueOnGameThread() != 0;
}

FVector ATN_SandCastleLobby::LayoutSpot(double ClockHour, double Dist, float& OutYawToCenter)
{
	const FVector2D P = TNCastleDetail::ClockPoint(ClockHour, Dist);
	OutYawToCenter = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(-P.Y, -P.X)));
	return FVector(P.X, P.Y, TNCastleDetail::FloorZ);
}

void ATN_SandCastleLobby::GetSpawnSpots(TArray<FTransform>& OutLocalSpots)
{
	OutLocalSpots.Reset();
	// Dos filas de cuatro: la de siempre y otra 3 m detrás, hacia la puerta (los cuatro primeros no se mueven).
	for (const double RowBack : { 0.0, 300.0 })
	{
		for (const double X : { -450.0, -150.0, 150.0, 450.0 })
		{
			const FVector Where(X, 1700.0 - FMath::Abs(X) * 0.25 + RowBack, TNCastleDetail::FloorZ + 95.0);
			const FVector ToEggs = FVector(TNCastleDetail::EggsCenter.X, TNCastleDetail::EggsCenter.Y, Where.Z) - Where;
			OutLocalSpots.Add(FTransform(ToEggs.Rotation(), Where));
		}
	}
}

ATN_SandCastleLobby::ATN_SandCastleLobby()
{
	using namespace TNCastleDetail;
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	SetNetUpdateFrequency(10.f);

	CastleRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CastleRoot"));
	SetRootComponent(CastleRoot);

	// Las mallas se generan en código (editor y ejecución) y no se guardan con el nivel: RF_Transient. El segundo
	// parámetro de CreateDefaultSubobject no basta (solo evita copiar la plantilla del arquetipo): sin la marca, las
	// secciones del castillo se guardaban dentro de LVL_Lobby (35 MB).
	CastleMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("CastleMesh"));
	CastleMesh->SetFlags(RF_Transient);
	CastleMesh->SetupAttachment(CastleRoot);
	CastleMesh->bUseAsyncCooking = false;
	CastleMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);

	DecorMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("DecorMesh"));
	DecorMesh->SetFlags(RF_Transient);
	DecorMesh->SetupAttachment(CastleRoot);
	DecorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DecorMesh->SetCastShadow(true);

	BarrierMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BarrierMesh"));
	BarrierMesh->SetFlags(RF_Transient);
	BarrierMesh->SetupAttachment(CastleRoot);
	BarrierMesh->bUseAsyncCooking = false;
	BarrierMesh->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	BarrierMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	BarrierMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	BarrierMesh->SetVisibility(false);
	BarrierMesh->SetHiddenInGame(true);

	auto MakeLeaf = [this](const TCHAR* Name)
	{
		UStaticMeshComponent* Leaf = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Leaf->SetupAttachment(CastleRoot);
		Leaf->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return Leaf;
	};
	GateLeafLeft = MakeLeaf(TEXT("GateLeafLeft"));
	GateLeafRight = MakeLeaf(TEXT("GateLeafRight"));
	Gate2LeafLeft = MakeLeaf(TEXT("Gate2LeafLeft"));
	Gate2LeafRight = MakeLeaf(TEXT("Gate2LeafRight"));

	auto MakeBlock = [this](const TCHAR* Name, double Y)
	{
		UBoxComponent* Block = CreateDefaultSubobject<UBoxComponent>(Name);
		Block->SetupAttachment(CastleRoot);
		Block->InitBoxExtent(FVector(Gatehouse::HalfW, 40.0, Gatehouse::GateH * 0.5));
		Block->SetRelativeLocation(FVector(0.0, Y, GateFloorZ + Gatehouse::GateH * 0.5));
		Block->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
		Block->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Block->SetHiddenInGame(true);
		return Block;
	};
	GateBlock = MakeBlock(TEXT("GateBlock"), GateY);
	Gate2Block = MakeBlock(TEXT("Gate2Block"), GateY + Gatehouse::Depth);

	for (int32 i = 0; i < NumEggs; ++i)
	{
		UStaticMeshComponent* Lid = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("EggLid%d"), i));
		Lid->SetupAttachment(CastleRoot);
		Lid->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		EggLids.Add(Lid);
	}

	auto MakeSignText = [this](const TCHAR* Name)
	{
		UTextRenderComponent* SignText = CreateDefaultSubobject<UTextRenderComponent>(Name);
		SignText->SetupAttachment(CastleRoot);
		SignText->SetHorizontalAlignment(EHTA_Center);
		SignText->SetVerticalAlignment(EVRTA_TextCenter);
		SignText->SetTextRenderColor(FColor(255, 214, 90));
		SignText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return SignText;
	};
	GateSignText = MakeSignText(TEXT("GateSignText"));
	Gate2SignText = MakeSignText(TEXT("Gate2SignText"));

	// Luces cálidas de antorcha, sin sombras: la sala de la puerta doble y el paso de la torre del homenaje.
	auto MakeLight = [this](const TCHAR* Name, const FVector& Where, float Lumens, float AttenRadius)
	{
		UPointLightComponent* Light = CreateDefaultSubobject<UPointLightComponent>(Name);
		Light->SetupAttachment(CastleRoot);
		Light->SetRelativeLocation(Where);
		Light->SetIntensityUnits(ELightUnits::Lumens);
		Light->SetIntensity(Lumens);
		Light->SetAttenuationRadius(AttenRadius);
		Light->SetLightColor(FLinearColor(1.f, 0.7f, 0.42f));
		Light->SetCastShadows(false);
		return Light;
	};
	RoomLight = MakeLight(TEXT("RoomLight"), FVector(0.0, GateY + Gatehouse::Depth * 0.5, GateFloorZ + 420.0), 2600.f, 900.f);
	TunnelLight = MakeLight(TEXT("TunnelLight"), FVector(0.0, CutY, DoorH - 70.0), 1800.f, 560.f);
}

void ATN_SandCastleLobby::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_SandCastleLobby, bGateOpen);
	DOREPLIFETIME(ATN_SandCastleLobby, EggMask);
}

void ATN_SandCastleLobby::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	BuildAll(true);
}

void ATN_SandCastleLobby::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	// Al cargar el nivel (editor) o al duplicarlo para jugar, las mallas transitorias llegan vacías: se rehacen.
	if (!IsTemplate() && GetWorld()) { BuildAll(false); }
}

void ATN_SandCastleLobby::BeginPlay()
{
	Super::BeginPlay();
	BuildAll(false);
	if (!IsEnabled())
	{
		SetActorHiddenInGame(true);
		SetActorEnableCollision(false);
		SetActorTickEnabled(false);
		return;
	}
	HideMaquette();
	SpawnTreasureChest();
	UE_LOG(LogTortunabo, Log, TEXT("[Castillo] Lobby de castillo de arena en %s."), *GetActorLocation().ToString());
}

void ATN_SandCastleLobby::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Si se quita el castillo, el cofre se va con él; al cambiar de nivel o cerrar se va solo, con el mundo.
	if (EndPlayReason == EEndPlayReason::Destroyed && HasAuthority())
	{
		if (ATN_TreasureChest* Chest = TreasureChest.Get())
		{
			Chest->Destroy();
		}
	}
	TreasureChest.Reset();
	Super::EndPlay(EndPlayReason);
}

void ATN_SandCastleLobby::SpawnTreasureChest()
{
	using namespace TNCastleDetail;
	UWorld* World = GetWorld();
	if (!World || !HasAuthority() || TreasureChest.IsValid())
	{
		return;
	}
	// En la tarima de la azotea, mirando a la plaza (el +X del cofre, al +Y del castillo).
	const FTransform Xf = GetActorTransform();
	const FVector Where = Xf.TransformPosition(FVector(TreasureSpot.X, TreasureSpot.Y, KeepRoofZ + TreasureDaisH));
	const FRotator Facing = Xf.TransformRotation(FRotator(0.f, 90.f, 0.f).Quaternion()).Rotator();
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	TreasureChest = World->SpawnActor<ATN_TreasureChest>(ATN_TreasureChest::StaticClass(), Where, Facing, SpawnParams);
	if (const ATN_TreasureChest* Chest = TreasureChest.Get())
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Castillo] Cofre del tesoro en la azotea de la torre del homenaje: %s."), *Chest->GetActorLocation().ToString());
	}
	else
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Castillo] No se ha podido crear el cofre del tesoro."));
	}
}

void ATN_SandCastleLobby::BuildAll(bool bForce)
{
	if (!bForce && bBuilt && CastleMesh && CastleMesh->GetNumSections() > 0) { return; }
	BuildCastle();
	BuildGateAndEggs();
	bBuilt = true;
}

void ATN_SandCastleLobby::SetDrawSea(bool bDraw)
{
	bDrawSea = bDraw;
	// Si ya estaba construido con otro mar, se rehace; si no, lo tiene en cuenta al construirse.
	if (bBuilt && bSeaBuilt != bDrawSea) { BuildAll(true); }
}

void ATN_SandCastleLobby::HideMaquette()
{
	UWorld* World = GetWorld();
	if (!World) { return; }
	const FVector Center = GetActorLocation();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor == this) { continue; }
		if (ATN_LobbyReadyZone* Zone = Cast<ATN_LobbyReadyZone>(Actor))
		{
			// Se está listo metiéndose en un huevo de la pila o en la sala de la puerta doble.
			Zone->SetActorEnableCollision(false);
			continue;
		}
		// Solo lo que es igual en el editor y en el juego empaquetado (#828): el nombre del objeto, su clase y su malla. La
		// etiqueta del actor solo existe en el editor: con ella, un anfitrión en el editor quitaba la colisión a piezas que
		// un cliente empaquetado conservaba (el suelo viejo «Rectangle2», con la malla Rectangle_*).
		const FString ClassName = Actor->GetClass()->GetName();
		FString MeshName;
		if (const AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(Actor))
		{
			const UStaticMeshComponent* Comp = MeshActor->GetStaticMeshComponent();
			if (Comp && Comp->GetStaticMesh()) { MeshName = Comp->GetStaticMesh()->GetName(); }
		}
		const FString Names = Actor->GetName() + TEXT(" ") + MeshName;
		FVector BoundsOrigin, BoundsExtent;
		Actor->GetActorBounds(false, BoundsOrigin, BoundsExtent);
		// La muralla de la maqueta (anillo «SandWall» / «Extrude»), las vallas y torres de la zona de salida, los huevos
		// sueltos, la carpa vieja, el poste y el suelo de la zona de salida. El suelo grande solo deja de verse.
		const bool bRing = (Names.Contains(TEXT("Extrude")) || Names.Contains(TEXT("SandWall"))) && BoundsExtent.Z > 150.0;
		const bool bFence = ClassName.Contains(TEXT("BP_Fence")) || ClassName.Contains(TEXT("BP_Tower"));
		const bool bOldEgg = Actor->IsA<AStaticMeshActor>() && Names.Contains(TEXT("Capsule"));
		const bool bOldProps = ClassName.Contains(TEXT("ChangingTent")) || ClassName.Contains(TEXT("SM_Palo")) || MeshName.StartsWith(TEXT("Rectangle"));
		if (bRing || bFence || bOldEgg || bOldProps)
		{
			Actor->SetActorHiddenInGame(true);
			Actor->SetActorEnableCollision(false);
			continue;
		}
		const bool bBigFloor = Actor->IsA<AStaticMeshActor>() && BoundsExtent.Z < 20.0 && BoundsExtent.X > 2000.0
			&& FVector::Dist2D(BoundsOrigin, Center) < 800.0;
		if (bBigFloor)
		{
			// Debajo del suelo del castillo y de la playa: no se ve (evita parpadeos), pero sigue sosteniendo.
			Actor->SetActorHiddenInGame(true);
		}
	}
}

void ATN_SandCastleLobby::BuildCastle()
{
	using namespace TNCastleDetail;
	FBuffers B;
	FBuffers Decor;
	FBuffers Barrier;
	// Piezas que Arte puede sustituir (Docs/Arte_Assets.md): todas en locales del castillo; la barrera no es visible.
	TNArt::FPieceLog Log(TEXT("Castle"));
	const FVector Up(0.0, 0.0, 1.0);
	bSeaBuilt = bDrawSea;

	// ── Suelo de arena redondo (con manchas de arena mojada y de arena clara) y playa por fuera ──
	{
		constexpr int32 Rings = 14;
		constexpr int32 Spokes = 72;
		TNArt::FPieceScope FloorPiece(Log, TN_ART("Lobby.Castle.Floor"), TNArt::PiecePivot(FVector(0.0, 0.0, FloorZ)), { &B });
		for (int32 i = 0; i < Rings; ++i)
		{
			const double R0 = (RO + 30.0) * i / Rings;
			const double R1 = (RO + 30.0) * (i + 1) / Rings;
			for (int32 k = 0; k < Spokes; ++k)
			{
				const double A0 = TNProcMap::TwoPi * k / Spokes, A1 = TNProcMap::TwoPi * (k + 1) / Spokes;
				const double N = TNProcMesh::TNProcHashNoise(i, k, 71u);
				const FLinearColor C = N > 0.6 ? Col(0xE3C284) : (N > -0.25 ? Col(0xF3DDA6) : Col(0xEED29A));
				const FVector P0(R0 * FMath::Cos(A0), R0 * FMath::Sin(A0), FloorZ), P1(R0 * FMath::Cos(A1), R0 * FMath::Sin(A1), FloorZ);
				const FVector P2(R1 * FMath::Cos(A1), R1 * FMath::Sin(A1), FloorZ), P3(R1 * FMath::Cos(A0), R1 * FMath::Sin(A0), FloorZ);
				B.AddQuad(P0, P1, P2, P3, Up, C);
			}
		}
	}
	// Playa de fuera (tapa el suelo de la maqueta) hasta el horizonte y el mar alrededor. Sin mar ni orilla si el valle
	// del lobby ocupa su sitio (bDrawSea).
	{
		constexpr int32 Spokes = 72;
		constexpr double ROut = 3900.0;
		{
			TNArt::FPieceScope BeachPiece(Log, TN_ART("Lobby.Castle.OuterBeach"), TNArt::PiecePivot(FVector(0.0, 0.0, 4.0)), { &Decor });
			for (int32 k = 0; k < Spokes; ++k)
			{
				const double A0 = TNProcMap::TwoPi * k / Spokes, A1 = TNProcMap::TwoPi * (k + 1) / Spokes;
				const double RIn = RO + 20.0;
				Decor.AddQuad(FVector(RIn * FMath::Cos(A0), RIn * FMath::Sin(A0), 4.0), FVector(RIn * FMath::Cos(A1), RIn * FMath::Sin(A1), 4.0),
					FVector(ROut * FMath::Cos(A1), ROut * FMath::Sin(A1), 4.0), FVector(ROut * FMath::Cos(A0), ROut * FMath::Sin(A0), 4.0), Up,
					(k % 5 == 0) ? Col(0xEBD39C) : Col(0xF2DCA8));
			}
		}
		if (bDrawSea)
		{
			TNArt::FPieceScope SeaPiece(Log, TN_ART("Lobby.Castle.Sea"), TNArt::PiecePivot(FVector(0.0, 0.0, -34.0)), { &Decor });
			for (int32 k = 0; k < Spokes; ++k)
			{
				const double A0 = TNProcMap::TwoPi * k / Spokes, A1 = TNProcMap::TwoPi * (k + 1) / Spokes;
				Decor.AddQuad(FVector(ROut * FMath::Cos(A0), ROut * FMath::Sin(A0), 4.0), FVector(ROut * FMath::Cos(A1), ROut * FMath::Sin(A1), 4.0),
					FVector(4400.0 * FMath::Cos(A1), 4400.0 * FMath::Sin(A1), -30.0), FVector(4400.0 * FMath::Cos(A0), 4400.0 * FMath::Sin(A0), -30.0), Up, Col(0xE2F6F2));
			}
			Decor.AddQuad(FVector(-30000.0, -30000.0, -34.0), FVector(30000.0, -30000.0, -34.0), FVector(30000.0, 30000.0, -34.0), FVector(-30000.0, 30000.0, -34.0),
				Up, Col(0x1E9CC6));
		}
	}

	// ── Muralla redonda: cara de dentro y de fuera, adarve arriba, almenas por fuera y el hueco de la puerta doble ──
	{
		constexpr int32 Seg = 180;
		TNArt::FPieceScope WallPiece(Log, TN_ART("Lobby.Castle.Wall"), TNArt::PiecePivot(FVector::ZeroVector), { &B });
		for (int32 k = 0; k < Seg; ++k)
		{
			const double A0 = TNProcMap::TwoPi * k / Seg, A1 = TNProcMap::TwoPi * (k + 1) / Seg;
			// En el sentido del reloj desde +Y: x = -r·sen(a), y = r·cos(a).
			if (InGate((A0 + A1) * 0.5)) { continue; }
			const double H0 = WallHeightAt(A0), H1 = WallHeightAt(A1);
			auto P = [](double Rad, double A, double Z) { return FVector(-Rad * FMath::Sin(A), Rad * FMath::Cos(A), Z); };
			const FVector In(FMath::Sin((A0 + A1) * 0.5), -FMath::Cos((A0 + A1) * 0.5), 0.0);
			const float Tone = TNProcMesh::TNProcTone(k, 17u) * 0.1f + 0.95f;
			B.AddQuad(P(R, A0, 0.0), P(R, A1, 0.0), P(R, A1, H1), P(R, A0, H0), In, SandC() * Tone);
			B.AddQuad(P(RO, A0, 0.0), P(RO, A1, 0.0), P(RO, A1, H1), P(RO, A0, H0), -In, SandDark() * Tone);
			B.AddQuad(P(R, A0, H0), P(R, A1, H1), P(RO, A1, H1), P(RO, A0, H0), Up, SandLight() * Tone);
			// Marcas del molde de cubo: tres franjas que sobresalen un poco por dentro.
			for (const double Z : { 150.0, 320.0, 470.0 })
			{
				B.AddQuad(P(R - 7.0, A0, Z - 8.0), P(R - 7.0, A1, Z - 8.0), P(R - 7.0, A1, Z + 8.0), P(R - 7.0, A0, Z + 8.0), In, SandDark());
				B.AddQuad(P(R, A0, Z + 8.0), P(R, A1, Z + 8.0), P(R - 7.0, A1, Z + 8.0), P(R - 7.0, A0, Z + 8.0), Up, SandDark());
			}
			// Almenas en el borde de fuera, una sí y otra no.
			if (k % 2 == 0)
			{
				const FVector M0 = P(RO - 30.0, A0, H0), M1 = P(RO - 30.0, A1, H1);
				const FVector Dir = (M1 - M0).GetSafeNormal2D();
				B.AddBox((M0 + M1) * 0.5 + Up * 45.0, Dir, FVector(FVector::Dist2D(M0, M1) * 0.5, 30.0, 45.0), SandC());
			}
			// Barrera invisible sobre el borde de fuera.
			Barrier.AddQuad(P(RO + 10.0, A0, H0), P(RO + 10.0, A1, H1), P(RO + 10.0, A1, H1 + 900.0), P(RO + 10.0, A0, H0 + 900.0), In, SandC());
		}
	}

	// ── Puerta doble a las 12: la misma estructura con la que se sale en el mapa procedural ──
	{
		TNArt::FPieceScope GatePiece(Log, TN_ART("Lobby.Castle.Gatehouse"), TNArt::PiecePivot(FVector(0.0, GateY, 0.0)), { &B, &Decor });
		BuildGatehouse(B, Decor, Barrier, FVector(0.0, GateY, 0.0), GateFloorZ, 0);
	}

	// ── Torres irregulares repartidas por la muralla ──
	for (int32 t = 0; t < static_cast<int32>(UE_ARRAY_COUNT(Towers)); ++t)
	{
		const FTowerDef& Def = Towers[t];
		{
			const FVector2D At = ClockPoint(Def.Clock, RM);
			TNArt::FPieceScope TowerPiece(Log, TN_ART("Lobby.Castle.Tower"),
				TowerPivot(At, 0.0, Def.Radius, Def.Height, FMath::RadiansToDegrees(FMath::Atan2(At.Y, At.X))), { &B, &Decor });
			AddTower(B, Decor, At, Def.Radius, Def.Height, Col(Def.Flag), t + 2);
		}
		// Barrera por fuera de la azotea de cada torre.
		const FVector2D C = ClockPoint(Def.Clock, RM);
		TNProcMesh::TNProcAddCylinder(Barrier, FVector(C.X, C.Y, Def.Height), FVector(C.X, C.Y, Def.Height + 900.0), Def.Radius + 40.0, Def.Radius + 40.0, 12, SandC(), false);
	}

	// ── Muro interior (de las 3:40 a las 8:20) con adarve, la torre del homenaje en medio y un tobogán a cada lado ──
	{
		const double Half = FMath::Sqrt(R * R - CutY * CutY) + 60.0;
		const double DownStepsX1 = DownStepsX0 - DownSteps * DownStepL;
		auto InSlideMouth = [](double X, double Face)
		{
			return (Face > 0.0 && FMath::Abs(X - SlideLeftX) < SlideHalfW + 40.0) || (Face < 0.0 && FMath::Abs(X - SlideRightX) < SlideHalfW + 40.0);
		};
		for (const double Sign : { -1.0, 1.0 })
		{
			const double X0 = Sign * (KeepR - 20.0), X1 = Sign * Half;
			const FVector Mid((X0 + X1) * 0.5, CutY, CutH * 0.5);
			// Cada mitad del muro (sin conchas ni guirnaldas), +X hacia la muralla redonda.
			TNArt::FPieceScope InnerWallPiece(Log, TN_ART("Lobby.Castle.InnerWall"), TNArt::PiecePivot(FVector(Mid.X, CutY, 0.0), Sign > 0.0 ? 0.0 : 180.0), { &B });
			B.AddBox(Mid, FVector(1.0, 0.0, 0.0), FVector(FMath::Abs(X1 - X0) * 0.5, CutHalfT, CutH * 0.5), SandC());
			for (const double Z : { 150.0, 320.0, 470.0 })
			{
				B.AddBox(FVector(Mid.X, CutY, Z), FVector(1.0, 0.0, 0.0), FVector(FMath::Abs(X1 - X0) * 0.5, CutHalfT + 7.0, 8.0), SandDark());
			}
			// Almenas a los dos lados del adarve, juntas (no se cuela una tortuga entre dos); sin almenas donde baja la
			// escalera del rellano ni en la boca de cada tobogán.
			for (double X = FMath::Min(X0, X1) + 60.0; X < FMath::Max(X0, X1) - 50.0; X += 150.0)
			{
				if (FMath::Abs(X) < FMath::Abs(DownStepsX1) + 60.0) { continue; }
				for (const double Face : { -1.0, 1.0 })
				{
					if (InSlideMouth(X, Face)) { continue; }
					B.AddBox(FVector(X, CutY + Face * (CutHalfT - 22.0), CutH + 45.0), FVector(1.0, 0.0, 0.0), FVector(45.0, 22.0, 45.0), SandC());
				}
			}
			// Conchas incrustadas en la cara que da a la plaza.
			const FLinearColor ShellColors[4] = { Col(0xFFB4A2), Col(0xFFE0C2), Col(0xE6D0FF), Col(0xFFF6E8) };
			for (int32 s = 0; s < 6; ++s)
			{
				const double X = FMath::Lerp(X0, X1, 0.12 + 0.15 * s);
				if (FMath::Abs(X - SlideLeftX) < SlideHalfW + 60.0) { continue; }
				const FVector ShellAt(X, CutY + CutHalfT, 230.0 + 120.0 * ((s + (Sign > 0.0 ? 1 : 0)) % 3));
				TNArt::FPieceScope ShellPiece(Log, TN_ART("Lobby.Castle.Shell"), ScallopPivot(ShellAt, FVector(0.0, 1.0, 0.0), Up, 30.0), { &Decor });
				AddScallop(Decor, ShellAt, FVector(0.0, 1.0, 0.0), Up, 30.0, ShellColors[s % 4]);
			}
			// Guirnaldas de banderines por encima del adarve, de mástil en mástil sobre las almenas de la cara sur.
			const double GX0 = Sign * (FMath::Abs(DownStepsX1) + 80.0);
			const double GX1 = X1 - Sign * 260.0;
			const int32 Poles = FMath::Max(2, FMath::RoundToInt32(FMath::Abs(GX1 - GX0) / 420.0) + 1);
			// Toda la línea de mástiles y banderines de cada mitad: pie del primer mástil, +X hacia la muralla redonda.
			TNArt::FPieceScope BuntingPiece(Log, TN_ART("Lobby.Castle.Bunting"),
				TNArt::PiecePivot(FVector(GX0, CutY - (CutHalfT - 22.0), CutH + 90.0), Sign > 0.0 ? 0.0 : 180.0), { &Decor });
			FVector PrevTop = FVector::ZeroVector;
			for (int32 p = 0; p < Poles; ++p)
			{
				const double X = FMath::Lerp(GX0, GX1, static_cast<double>(p) / (Poles - 1));
				const FVector Foot(X, CutY - (CutHalfT - 22.0), CutH + 90.0);
				const FVector Top = Foot + Up * 170.0;
				TNProcMesh::TNProcAddCylinder(Decor, Foot, Top, 4.0, 3.0, 6, Col(0x7A4E2B));
				if (p > 0) { AddBunting(Decor, PrevTop, Top, p + (Sign > 0.0 ? 2 : 0)); }
				PrevTop = Top;
			}
		}
		// Tobogán de la izquierda a la plaza y de la derecha al patio de pruebas. Pivote: boca del tobogán en el muro, a ras
		// de suelo, +X hacia donde baja.
		{
			TNArt::FPieceScope SlidePiece(Log, TN_ART("Lobby.Castle.Slide"), TNArt::PiecePivot(FVector(SlideLeftX, CutY + CutHalfT, 0.0), 90.0), { &B, &Decor });
			AddWallSlide(B, Decor, SlideLeftX, 1.0, Col(0xFF6A52), &Log);
		}
		{
			TNArt::FPieceScope SlidePiece(Log, TN_ART("Lobby.Castle.Slide"), TNArt::PiecePivot(FVector(SlideRightX, CutY - CutHalfT, 0.0), -90.0), { &B, &Decor });
			AddWallSlide(B, Decor, SlideRightX, -1.0, Col(0x2EC4B6), &Log);
		}
		// Mirador del adarve derecho, junto a la muralla: catalejo en su trípode, cubo con pala y un banderón.
		{
			const FVector Look(Half - 520.0, CutY, CutH);
			TNArt::FPieceScope LookoutPiece(Log, TN_ART("Lobby.Castle.Lookout"), TNArt::PiecePivot(Look), { &Decor });
			for (int32 l = 0; l < 3; ++l)
			{
				const double A = TNProcMap::TwoPi * l / 3.0;
				Decor.AddBeam(Look + FVector(FMath::Cos(A) * 34.0, FMath::Sin(A) * 34.0, 0.0), Look + Up * 110.0, 3.0, Col(0x7A4E2B));
			}
			TNProcMesh::TNProcAddCylinder(Decor, Look + Up * 112.0 + FVector(-40.0, 30.0, -8.0), Look + Up * 112.0 + FVector(55.0, -40.0, 18.0), 7.0, 11.0, 10, Col(0xB88A3E));
			TNProcMesh::TNProcAddCylinder(Decor, Look + FVector(120.0, 40.0, 0.0), Look + FVector(120.0, 40.0, 46.0), 22.0, 28.0, 12, Col(0xFF6A52));
			Decor.AddBeam(Look + FVector(120.0, 40.0, 40.0), Look + FVector(150.0, 10.0, 120.0), 3.0, Col(0xFFCB3D));
			const FVector FlagFoot(Half - 300.0, CutY + CutHalfT - 30.0, CutH + 90.0);
			TNProcMesh::TNProcAddCylinder(Decor, FlagFoot, FlagFoot + Up * 420.0, 7.0, 5.0, 6, Col(0x7A4E2B));
			Decor.AddTri(FlagFoot + Up * 416.0, FlagFoot + Up * 330.0, FlagFoot + Up * 390.0 + FVector(-190.0, 0.0, -10.0), FVector(0.0, 1.0, 0.0), Col(0x2EC4B6));
			Decor.AddTri(FlagFoot + Up * 416.0, FlagFoot + Up * 330.0, FlagFoot + Up * 390.0 + FVector(-190.0, 0.0, -10.0), FVector(0.0, -1.0, 0.0), Col(0x2EC4B6) * 0.85f);
		}
	}

	// ── Torre del homenaje: cilindro con un paso de norte a sur, franjas, azotea-balcón y torreón ──
	{
		const FVector KeepC(0.0, CutY, 0.0);
		constexpr int32 KSeg = 40;
		// La torre con su paso por dentro (sin el torreón, la tarima del cofre ni las escaleras, que son piezas aparte).
		const int32 KeepPiece = Log.Begin(TN_ART("Lobby.Castle.Keep"), TNArt::PiecePivot(KeepC), { &B, &Decor });
		for (int32 k = 0; k < KSeg; ++k)
		{
			const double A0 = TNProcMap::TwoPi * k / KSeg, A1 = TNProcMap::TwoPi * (k + 1) / KSeg;
			const double Am = (A0 + A1) * 0.5;
			const bool bDoorSide = FMath::Abs(KeepR * FMath::Sin(Am)) < DoorHalfW + 5.0;
			const double Z0 = bDoorSide ? DoorH : 0.0;
			const FVector Out(-FMath::Sin(Am), FMath::Cos(Am), 0.0);
			B.AddQuad(KeepPoint(KeepR, A0, Z0), KeepPoint(KeepR, A1, Z0), KeepPoint(KeepR, A1, KeepRoofZ), KeepPoint(KeepR, A0, KeepRoofZ), Out, SandC());
			for (const double Z : { 200.0, 470.0, 740.0 })
			{
				if (Z < Z0 + 20.0) { continue; }
				B.AddQuad(KeepPoint(KeepR + 8.0, A0, Z - 9.0), KeepPoint(KeepR + 8.0, A1, Z - 9.0), KeepPoint(KeepR + 8.0, A1, Z + 9.0), KeepPoint(KeepR + 8.0, A0, Z + 9.0), Out, SandDark());
			}
			// Azotea (el balcón): anillo de arena clara y almenas en el borde, con hueco donde llega la escalera (lado oeste).
			B.AddTri(KeepC + Up * KeepRoofZ, KeepPoint(KeepR + 30.0, A0, KeepRoofZ), KeepPoint(KeepR + 30.0, A1, KeepRoofZ), Up, SandLight());
			B.AddQuad(KeepPoint(KeepR, A0, KeepRoofZ - 30.0), KeepPoint(KeepR, A1, KeepRoofZ - 30.0), KeepPoint(KeepR + 30.0, A1, KeepRoofZ), KeepPoint(KeepR + 30.0, A0, KeepRoofZ), Out, SandDark());
			const double Deg = FMath::RadiansToDegrees(Am);
			const bool bStairLanding = (Deg > 48.0 && Deg < LandingEndDeg + 4.0) || (Deg > EastLandingStartDeg - 4.0 && Deg < EastLandingEndDeg + 4.0);
			if (k % 2 == 0 && !bStairLanding)
			{
				B.AddBox(KeepPoint(KeepR + 8.0, Am, KeepRoofZ + 50.0), Out, FVector(20.0, 36.0, 50.0), SandC());
			}
			if (!bStairLanding)
			{
				Barrier.AddQuad(KeepPoint(KeepR + 32.0, A0, KeepRoofZ), KeepPoint(KeepR + 32.0, A1, KeepRoofZ), KeepPoint(KeepR + 32.0, A1, KeepRoofZ + 260.0),
					KeepPoint(KeepR + 32.0, A0, KeepRoofZ + 260.0), -Out, SandC());
			}
		}
		// Paso por dentro: paredes, techo, antorchas y arcos en las dos bocas.
		for (const double X : { -DoorHalfW, DoorHalfW })
		{
			B.AddBox(FVector(X + (X > 0.0 ? 8.0 : -8.0), CutY, DoorH * 0.5), FVector(1.0, 0.0, 0.0), FVector(8.0, KeepR, DoorH * 0.5), SandDark());
			const FVector TorchAt(X, CutY, 220.0), TorchOut(X > 0.0 ? -1.0 : 1.0, 0.0, 0.0);
			TNArt::FPieceScope TorchPiece(Log, TN_ART("Lobby.Castle.Torch"), TorchPivot(TorchAt, TorchOut), { &Decor });
			AddWallTorch(Decor, TorchAt, TorchOut);
		}
		B.AddBox(FVector(0.0, CutY, DoorH + 10.0), FVector(1.0, 0.0, 0.0), FVector(DoorHalfW + 16.0, KeepR, 10.0), SandDark());
		for (const double Face : { 1.0, -1.0 })
		{
			for (int32 s = 0; s < 10; ++s)
			{
				const double T0 = PI * s / 10.0, T1 = PI * (s + 1) / 10.0;
				const FVector C0(-(DoorHalfW + 16.0) * FMath::Cos(T0), CutY + Face * (KeepR + 4.0), DoorH - 40.0 + 60.0 * FMath::Sin(T0));
				const FVector C1(-(DoorHalfW + 16.0) * FMath::Cos(T1), CutY + Face * (KeepR + 4.0), DoorH - 40.0 + 60.0 * FMath::Sin(T1));
				const FVector D = (C1 - C0).GetSafeNormal();
				const FVector AxisX = FVector(D.X, D.Y, 0.0).IsNearlyZero() ? FVector(1.0, 0.0, 0.0) : FVector(D.X, D.Y, 0.0).GetSafeNormal();
				Decor.AddBox((C0 + C1) * 0.5, AxisX, FVector(FVector::Dist(C0, C1) * 0.5, 10.0, 16.0), (s % 2) ? SandDark() : Col(0xCFA766));
			}
		}
		Log.End(KeepPiece);
		// Torrecilla sobre la azotea (al sur), con tejado de cono y bandera: la silueta alta del castillo.
		{
			TNArt::FPieceScope TurretPiece(Log, TN_ART("Lobby.Castle.KeepTurret"), TNArt::PiecePivot(FVector(0.0, CutY - 170.0, KeepRoofZ)), { &B, &Decor });
			AddTower(B, Decor, FVector2D(0.0, CutY - 170.0), 150.0, 430.0, Col(0xFF6A52), 0, KeepRoofZ);
		}
		// Tarima del cofre del tesoro, delante del torreón: arena con un reborde oscuro y conchas alrededor. El cofre lo
		// pone el servidor encima (SpawnTreasureChest); la tarima lo sube un poco para que asome por las almenas.
		{
			TNArt::FPieceScope DaisPiece(Log, TN_ART("Lobby.Castle.TreasureDais"), TNArt::PiecePivot(FVector(TreasureSpot.X, TreasureSpot.Y, KeepRoofZ)), { &B, &Decor });
			const double DaisTopZ = KeepRoofZ + TreasureDaisH;
			TNProcMesh::TNProcAddCylinder(B, FVector(TreasureSpot.X, TreasureSpot.Y, KeepRoofZ - 2.0), FVector(TreasureSpot.X, TreasureSpot.Y, DaisTopZ),
				TreasureDaisR, TreasureDaisR - 6.0, 28, SandC());
			TNProcMesh::TNProcAddCylinder(B, FVector(TreasureSpot.X, TreasureSpot.Y, DaisTopZ - 9.0), FVector(TreasureSpot.X, TreasureSpot.Y, DaisTopZ - 2.0),
				TreasureDaisR - 1.0, TreasureDaisR - 3.0, 28, SandDark(), false);
			const FLinearColor DaisShells[3] = { Col(0xFFB4A2), Col(0xFFE0C2), Col(0xE6D0FF) };
			for (int32 s = 0; s < 6; ++s)
			{
				const double A = TNProcMap::TwoPi * (s + 0.5) / 6.0;
				const FVector Out(FMath::Cos(A), FMath::Sin(A), 0.0);
				const FVector ShellAt = FVector(TreasureSpot.X, TreasureSpot.Y, KeepRoofZ + 6.0) + Out * (TreasureDaisR - 2.5);
				TNArt::FPieceScope ShellPiece(Log, TN_ART("Lobby.Castle.Shell"), ScallopPivot(ShellAt, Out, Up, 9.0), { &Decor });
				AddScallop(Decor, ShellAt, Out, Up, 9.0, DaisShells[s % 3]);
			}
		}
		// Escalera de caracol por fuera, de peldaños macizos: del suelo (este, 290°) a la azotea (oeste, 70°) pasando por
		// encima del arco de la puerta (0°). Los de la entrada, más largos hacia la plaza. Con los rellanos y las dos
		// escaleras rectas a los adarves, una sola pieza con pivote en el eje de la torre.
		TNArt::FPieceScope StairsPiece(Log, TN_ART("Lobby.Castle.KeepStairs"), TNArt::PiecePivot(KeepC), { &B });
		const double StepDeg = (StairEndDeg - StairStartDeg) / StairSteps;
		for (int32 s = 0; s < StairSteps; ++s)
		{
			const double A0 = StairStartDeg + StepDeg * s - 0.4, A1 = StairStartDeg + StepDeg * (s + 1);
			const double Z = KeepRoofZ * (s + 1) / StairSteps;
			const double Outer = StairOut + StairEntryFlare * FMath::Max(0, StairEntrySteps - s);
			AddKeepSector(B, StairIn - 4.0, Outer, A0, A1, Z - StairStepT, Z, (s % 2) ? SandLight() : Col(0xEFD29A), SandDark());
			if (s < StairEntrySteps)
			{
				continue;
			}
			const double Am = FMath::DegreesToRadians((A0 + A1) * 0.5);
			const FVector Out(-FMath::Sin(Am), FMath::Cos(Am), 0.0);
			// Barandilla invisible por fuera (no se cae uno al subir) y bolardos de arena de adorno.
			Barrier.AddQuad(KeepPoint(StairOut + 12.0, FMath::DegreesToRadians(A0), Z), KeepPoint(StairOut + 12.0, FMath::DegreesToRadians(A1), Z),
				KeepPoint(StairOut + 12.0, FMath::DegreesToRadians(A1), Z + 180.0), KeepPoint(StairOut + 12.0, FMath::DegreesToRadians(A0), Z + 180.0), -Out, SandC());
			if (s % 3 == 0)
			{
				TNProcMesh::TNProcAddCylinder(B, KeepPoint(StairOut - 15.0, FMath::DegreesToRadians(A0 + 1.0), Z), KeepPoint(StairOut - 15.0, FMath::DegreesToRadians(A0 + 1.0), Z + 55.0),
					14.0, 11.0, 8, SandDark());
			}
		}
		// Rellano de arriba (de 70° a 100°, a la altura de la azotea), con su barandilla y bolardos; por el oeste sigue la
		// escalera que baja al adarve izquierdo del muro.
		const double LandStart = StairEndDeg - 360.0;
		for (double A = LandStart; A < LandingEndDeg - 0.1; A += 5.0)
		{
			const double A1 = FMath::Min(A + 5.0, LandingEndDeg);
			AddKeepSector(B, StairIn - 4.0, StairOut, A - 0.3, A1, KeepRoofZ - 45.0, KeepRoofZ, SandLight(), SandDark());
			const double Am = FMath::DegreesToRadians((A + A1) * 0.5);
			const FVector Out(-FMath::Sin(Am), FMath::Cos(Am), 0.0);
			const bool bToSteps = (A + A1) * 0.5 > 81.0 && (A + A1) * 0.5 < 99.0;
			if (!bToSteps)
			{
				Barrier.AddQuad(KeepPoint(StairOut + 12.0, FMath::DegreesToRadians(A), KeepRoofZ), KeepPoint(StairOut + 12.0, FMath::DegreesToRadians(A1), KeepRoofZ),
					KeepPoint(StairOut + 12.0, FMath::DegreesToRadians(A1), KeepRoofZ + 180.0), KeepPoint(StairOut + 12.0, FMath::DegreesToRadians(A), KeepRoofZ + 180.0), -Out, SandC());
				TNProcMesh::TNProcAddCylinder(B, KeepPoint(StairOut - 15.0, Am, KeepRoofZ), KeepPoint(StairOut - 15.0, Am, KeepRoofZ + 55.0), 14.0, 11.0, 8, SandDark());
			}
		}
		{
			// Cierre sur del rellano (barandilla invisible radial en 100°).
			const double AEnd = FMath::DegreesToRadians(LandingEndDeg);
			const FVector Fwd(-FMath::Cos(AEnd), -FMath::Sin(AEnd), 0.0);
			Barrier.AddQuad(KeepPoint(StairIn, AEnd, KeepRoofZ), KeepPoint(StairOut + 12.0, AEnd, KeepRoofZ), KeepPoint(StairOut + 12.0, AEnd, KeepRoofZ + 180.0),
				KeepPoint(StairIn, AEnd, KeepRoofZ + 180.0), -Fwd, SandC());
		}
		// Rellano del este (de 255° a 285°, a la altura de la azotea): se llega cruzando la azotea y baja al adarve derecho.
		for (double A = EastLandingStartDeg; A < EastLandingEndDeg - 0.1; A += 5.0)
		{
			const double A1 = FMath::Min(A + 5.0, EastLandingEndDeg);
			AddKeepSector(B, StairIn - 4.0, StairOut, A - 0.3, A1, KeepRoofZ - 45.0, KeepRoofZ, SandLight(), SandDark());
			const double Am = FMath::DegreesToRadians((A + A1) * 0.5);
			const FVector Out(-FMath::Sin(Am), FMath::Cos(Am), 0.0);
			const bool bToSteps = (A + A1) * 0.5 > 261.0 && (A + A1) * 0.5 < 279.0;
			if (!bToSteps)
			{
				Barrier.AddQuad(KeepPoint(StairOut + 12.0, FMath::DegreesToRadians(A), KeepRoofZ), KeepPoint(StairOut + 12.0, FMath::DegreesToRadians(A1), KeepRoofZ),
					KeepPoint(StairOut + 12.0, FMath::DegreesToRadians(A1), KeepRoofZ + 180.0), KeepPoint(StairOut + 12.0, FMath::DegreesToRadians(A), KeepRoofZ + 180.0), -Out, SandC());
				TNProcMesh::TNProcAddCylinder(B, KeepPoint(StairOut - 15.0, Am, KeepRoofZ), KeepPoint(StairOut - 15.0, Am, KeepRoofZ + 55.0), 14.0, 11.0, 8, SandDark());
			}
		}
		for (const double EdgeDeg : { EastLandingStartDeg, EastLandingEndDeg })
		{
			// Cierres radiales del rellano del este (no está unido a la escalera de caracol).
			const double AEdge = FMath::DegreesToRadians(EdgeDeg);
			const FVector Fwd(-FMath::Cos(AEdge), -FMath::Sin(AEdge), 0.0);
			Barrier.AddQuad(KeepPoint(StairIn + 30.0, AEdge, KeepRoofZ), KeepPoint(StairOut + 12.0, AEdge, KeepRoofZ), KeepPoint(StairOut + 12.0, AEdge, KeepRoofZ + 180.0),
				KeepPoint(StairIn + 30.0, AEdge, KeepRoofZ + 180.0), EdgeDeg < 270.0 ? Fwd : -Fwd, SandC());
		}
		// Escaleras rectas de los dos rellanos a los adarves, por encima del muro: once peldaños macizos de 25 cm cada una
		// (al oeste, la del tobogán de la plaza; al este, la del tobogán del patio de pruebas).
		for (const double Side : { -1.0, 1.0 })
		{
			for (int32 s = 0; s < DownSteps; ++s)
			{
				const double Near = FMath::Abs(DownStepsX0) + DownStepL * s - 2.0, Far = FMath::Abs(DownStepsX0) + DownStepL * (s + 1);
				const double Top = KeepRoofZ - 25.0 * (s + 1);
				AddAxisBox(B, FVector(FMath::Min(Side * Near, Side * Far), CutY - CutHalfT, CutH - 1.0), FVector(FMath::Max(Side * Near, Side * Far), CutY + CutHalfT, Top),
					(s % 2) ? SandLight() : Col(0xEFD29A));
			}
			const double NearEnd = FMath::Abs(DownStepsX0) - 30.0, FarEnd = FMath::Abs(DownStepsX0) + DownStepL * DownSteps;
			for (const double Face : { -1.0, 1.0 })
			{
				AddAxisBox(Barrier, FVector(FMath::Min(Side * NearEnd, Side * FarEnd), CutY + Face * (CutHalfT + 4.0) - 4.0, CutH),
					FVector(FMath::Max(Side * NearEnd, Side * FarEnd), CutY + Face * (CutHalfT + 4.0) + 4.0, KeepRoofZ + 200.0), SandC());
			}
		}
	}

	// ── Montículo de la pila de huevos (dos alturas) con el escalón de la concha hacia la puerta ──
	{
		TNArt::FPieceScope MoundPiece(Log, TN_ART("Lobby.Castle.EggMound"), TNArt::PiecePivot(FVector(EggsCenter.X, EggsCenter.Y, FloorZ), 90.0), { &B, &Decor });
		BuildEggMound(B, Decor, FVector(EggsCenter.X, EggsCenter.Y, 0.0), FloorZ, FVector(0.0, 1.0, 0.0));
	}

	// ── Adornos sueltos por la plaza y el patio: conchas y estrellas ──
	for (int32 i = 0; i < 60; ++i)
	{
		const double A = TNProcMap::TwoPi * FMath::Frac(i * 0.618034 + 0.11);
		const double Dist = (R - 200.0) * FMath::Sqrt(FMath::Frac(i * 0.754877 + 0.29));
		const FVector P(-Dist * FMath::Sin(A), Dist * FMath::Cos(A), FloorZ);
		if (FVector2D::Distance(FVector2D(P.X, P.Y), EggsCenter) < EggMound::Tier1R + 120.0) { continue; }
		if (FMath::Abs(P.Y - CutY) < 260.0) { continue; }
		if ((FMath::Abs(P.X - SlideLeftX) < 150.0 && P.Y > CutY && P.Y < CutY + CutHalfT + SlideRun + 60.0)
			|| (FMath::Abs(P.X - SlideRightX) < 150.0 && P.Y < CutY && P.Y > CutY - CutHalfT - SlideRun - 60.0)) { continue; }
		if (i % 3 == 0)
		{
			const double Size = 22.0 + 10.0 * FMath::Frac(i * 0.31);
			TNArt::FPieceScope StarPiece(Log, TN_ART("Lobby.Castle.Starfish"), StarfishPivot(P, Size, i * 0.7), { &Decor });
			AddStarfish(Decor, P, Size, i * 0.7, (i % 2) ? Col(0xFF8A70) : Col(0xFFB077));
		}
		else
		{
			const FVector Dir(FMath::Cos(i * 1.3), FMath::Sin(i * 1.3), 0.0);
			const double Size = 18.0 + 8.0 * FMath::Frac(i * 0.43);
			TNArt::FPieceScope ShellPiece(Log, TN_ART("Lobby.Castle.Shell"), ScallopPivot(P + Up * 2.0, Up, Dir, Size), { &Decor });
			AddScallop(Decor, P + Up * 2.0, Up, Dir, Size, (i % 2) ? Col(0xFFE0C2) : Col(0xFFB4A2));
		}
	}

	UMaterialInterface* Mat = VertexColorMaterial();
	UploadSection(CastleMesh, B, true, Mat, &Log);
	UploadSection(DecorMesh, Decor, false, Mat, &Log);
	UploadSection(BarrierMesh, Barrier, true, nullptr);
	// La malla de arte de cada pieza con sustituto, en su sitio (hija de la malla del castillo: mismos ejes).
	TNArt::SpawnPieceArt(CastleMesh, Log);

	// Rótulos de los carteles de las dos puertas (la 1 por la cara de la plaza y la 2 por fuera): siempre dentro de la
	// tabla (se encogen si el nombre es largo).
	auto PlaceSignText = [this](UTextRenderComponent* SignText, double SignGateY, double Face)
	{
		if (!SignText) { return; }
		SignText->SetRelativeLocationAndRotation(FVector(0.0, GateY, 0.0) + GatehouseSignText(SignGateY, Face, GateFloorZ), FRotator(0.f, Face > 0.0 ? 90.f : -90.f, 0.f));
		SignText->SetWorldSize(88.f);
		SignText->SetText(GateName);
		const double Width = SignText->GetTextLocalSize().Y;
		if (Width > 540.0) { SignText->SetWorldSize(static_cast<float>(88.0 * 540.0 / Width)); }
	};
	PlaceSignText(GateSignText, 0.0, -1.0);
	PlaceSignText(Gate2SignText, Gatehouse::Depth, 1.0);
}

void ATN_SandCastleLobby::BuildGateAndEggs()
{
	using namespace TNCastleDetail;
	UMaterialInterface* Mat = VertexColorMaterial();

	// Puerta doble: las hojas izquierdas (bisagra en -X) cerradas apuntan a +X; las derechas, a -X.
	FBuffers Leaf;
	BuildLeaf(Leaf, Gatehouse::HalfW, Gatehouse::GateH);
	UStaticMesh* LeafMesh = TNProcRuntimeMesh::MakeStaticMesh(this, Leaf, Mat);
	const double Gate2Y = GateY + Gatehouse::Depth;
	if (GateLeafLeft) { TNArt::SetMesh(GateLeafLeft, LeafMesh, TN_ART("Lobby.Castle.GateLeaf")); GateLeafLeft->SetRelativeLocationAndRotation(FVector(-Gatehouse::HalfW, GateY, GateFloorZ), FRotator::ZeroRotator); }
	if (GateLeafRight) { TNArt::SetMesh(GateLeafRight, LeafMesh, TN_ART("Lobby.Castle.GateLeaf")); GateLeafRight->SetRelativeLocationAndRotation(FVector(Gatehouse::HalfW, GateY, GateFloorZ), FRotator(0.f, 180.f, 0.f)); }
	if (Gate2LeafLeft) { TNArt::SetMesh(Gate2LeafLeft, LeafMesh, TN_ART("Lobby.Castle.GateLeaf")); Gate2LeafLeft->SetRelativeLocationAndRotation(FVector(-Gatehouse::HalfW, Gate2Y, GateFloorZ), FRotator::ZeroRotator); }
	if (Gate2LeafRight) { TNArt::SetMesh(Gate2LeafRight, LeafMesh, TN_ART("Lobby.Castle.GateLeaf")); Gate2LeafRight->SetRelativeLocationAndRotation(FVector(Gatehouse::HalfW, Gate2Y, GateFloorZ), FRotator(0.f, 180.f, 0.f)); }
	bGateBlocking = true;
	if (GateBlock) { GateBlock->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); }

	// Huevos: base en los adornos (sin colisión: se entra andando) y tapa que baja al ocuparlo.
	FBuffers Cups;
	TNArt::FPieceLog CupLog(TEXT("CastleEggCups"));
	for (int32 i = 0; i < NumEggs; ++i)
	{
		const FVector Spot = EggSpot(i);
		{
			TNArt::FPieceScope CupPiece(CupLog, TN_ART("Lobby.Castle.EggCup"), TNArt::PiecePivot(Spot), { &Cups });
			BuildEggCup(Cups, Spot, Col(0xFFF3DC), Col(EggAccent(i)));
		}
		if (EggLids.IsValidIndex(i) && EggLids[i])
		{
			FBuffers Lid;
			BuildEggLid(Lid, Pal(0xFFF3DC), Pal(EggAccent(i)));
			TNArt::SetMesh(EggLids[i], TNProcRuntimeMesh::MakeStaticMesh(this, Lid, Mat), TN_ART("Lobby.Castle.EggLid"));
			EggLids[i]->SetRelativeLocation(Spot + FVector(0.0, 0.0, EggSeam + 160.0));
		}
	}
	// Las bases van con los adornos del castillo: se añaden a su sección aparte.
	if (DecorMesh && !Cups.IsEmpty())
	{
		TNArt::UploadSection(DecorMesh, 1, Cups, false, Mat, &CupLog);
	}
	TNArt::SpawnPieceArt(CastleMesh, CupLog);
}

void ATN_SandCastleLobby::ServerUpdate(float DeltaSeconds)
{
	using namespace TNCastleDetail;
	UWorld* World = GetWorld();
	if (!World) { return; }
	ATN_HQGameMode* HQ = World->GetAuthGameMode<ATN_HQGameMode>();
	const FTransform Xf = GetActorTransform();

	int32 Mask = 0;
	int32 NumPlayers = 0;
	int32 NumInRoom = 0;
	int32 NumInEggs = 0;
	bool bNearFromPlaza = false;
	bool bWantsOut = false;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		const APawn* PawnInLobby = PC ? PC->GetPawn() : nullptr;
		bool bInEgg = false;
		bool bInRoom = false;
		if (PawnInLobby)
		{
			++NumPlayers;
			const FVector Local = Xf.InverseTransformPosition(PawnInLobby->GetActorLocation());
			for (int32 i = 0; i < NumEggs; ++i)
			{
				const FVector Spot = EggSpot(i);
				// Dentro del huevo: cerca de su eje y a la altura de su base (el de arriba está en el piso alto del montículo).
				if (FVector2D::Distance(FVector2D(Local.X, Local.Y), FVector2D(Spot.X, Spot.Y)) < 95.0 && Local.Z > Spot.Z && Local.Z < Spot.Z + 320.0)
				{
					Mask |= 1 << i;
					bInEgg = true;
					break;
				}
			}
			// Sala de la puerta doble (locales de la puerta doble: origen en el umbral de la puerta 1).
			const FVector GateLocal = Local - FVector(0.0, GateY, GateFloorZ);
			bInRoom = !bInEgg && Gatehouse::IsInRoom(GateLocal);
			NumInEggs += bInEgg ? 1 : 0;
			NumInRoom += bInRoom ? 1 : 0;
			bNearFromPlaza |= GateLocal.Y < 0.0 && FVector2D(GateLocal.X, GateLocal.Y).Size() < 750.0;
			// De pie junto a la puerta 1 por dentro: quiere salir.
			bWantsOut |= bInRoom && GateLocal.Y < Gatehouse::RoomY0 + 60.0;
		}
		// Sin tortuga (se destruyen justo antes de viajar) no se toca su estado: si no, la cuenta atrás se cancelaría en el
		// último momento y el huevo de carga se abriría antes del viaje.
		if (PC && PawnInLobby)
		{
			const bool bReady = bInEgg || bInRoom;
			const bool* Sent = ReadySent.Find(PC);
			if (!Sent || *Sent != bReady)
			{
				ReadySent.Add(PC, bReady);
				if (HQ) { HQ->SetPlayerReadyState(PC, bReady); }
			}
		}
	}
	EggMask = Mask;
	if (NumInRoom + NumInEggs > 0)
	{
		StartStyle = NumInRoom >= NumInEggs ? ETNMatchStartStyle::Gate : ETNMatchStartStyle::Eggs;
	}

	// Puerta 1: se abre si alguien se acerca por la plaza o quiere salir, y mientras haya gente dentro sin estar todos.
	// Con todos dentro se cierra (se ve la segunda puerta cerrada delante) y queda así hasta el viaje.
	const bool bAllInRoom = NumPlayers > 0 && NumInRoom == NumPlayers;
	const bool bWantOpen = bNearFromPlaza || bWantsOut || (NumInRoom > 0 && !bAllInRoom);
	GateHoldTimer = bWantOpen ? 1.5f : GateHoldTimer - DeltaSeconds;
	bGateOpen = GateHoldTimer > 0.f;
}

void ATN_SandCastleLobby::Tick(float DeltaSeconds)
{
	using namespace TNCastleDetail;
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;

	if (HasAuthority())
	{
		ServerTimer -= DeltaSeconds;
		if (ServerTimer <= 0.f)
		{
			ServerTimer = 0.2f;
			ServerUpdate(0.2f);
		}
	}

	// Puerta 1: se abre hacia la plaza (-Y) en ~1,2 s; bloquea solo cerrada del todo. La 2 sigue cerrada.
	GateOpenness = FMath::FInterpConstantTo(GateOpenness, bGateOpen ? 1.f : 0.f, DeltaSeconds, 0.8f);
	const float GateAngle = 100.f * SmoothStep01(GateOpenness);
	if (GateLeafLeft) { GateLeafLeft->SetRelativeRotation(FRotator(0.f, -GateAngle, 0.f)); }
	if (GateLeafRight) { GateLeafRight->SetRelativeRotation(FRotator(0.f, 180.f + GateAngle, 0.f)); }
	const bool bShouldBlock = !bGateOpen && GateOpenness < 0.05f;
	if (GateBlock && bShouldBlock != bGateBlocking)
	{
		bGateBlocking = bShouldBlock;
		GateBlock->SetCollisionEnabled(bShouldBlock ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}

	// Huevos: la tapa flota encima dando vueltecitas y baja a cerrar el huevo cuando alguien se mete (y se mece).
	for (int32 i = 0; i < EggLids.Num() && i < NumEggs; ++i)
	{
		if (!EggLids[i]) { continue; }
		const bool bOccupied = (EggMask & (1 << i)) != 0;
		EggClose[i] = FMath::FInterpConstantTo(EggClose[i], bOccupied ? 1.f : 0.f, DeltaSeconds, 2.2f);
		const float Close = SmoothStep01(EggClose[i]);
		const float Bob = 10.f * FMath::Sin(Clock * 1.6f + i * 0.9f);
		const float Height = FMath::Lerp(160.f + Bob, 0.f, Close);
		const float Tilt = FMath::Lerp(16.f, 0.f, Close);
		const float Rock = Close * 3.5f * FMath::Sin(Clock * 3.1f + i);
		EggLids[i]->SetRelativeLocationAndRotation(EggSpot(i) + FVector(0.0, 0.0, EggSeam + Height),
			FRotator(Tilt * FMath::Sin(Clock * 0.7f + i) + Rock, Clock * 12.f * (1.f - Close) + i * 40.f, Tilt * FMath::Cos(Clock * 0.7f + i)));
	}
}
