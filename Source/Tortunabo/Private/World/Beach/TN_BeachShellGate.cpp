#include "World/Beach/TN_BeachShellGate.h"
#include "World/Beach/TN_BeachTickWakeSubsystem.h"
#include "World/Beach/TN_BeachNearby.h"
#include "World/Beach/TN_BeachTrapSynthComponent.h"
#include "Core/TN_Log.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "TN_BeachTrapKit.h"

/**
 * Geometría de la puerta: bastidor de madera de deriva (postes y dintel), hojas con un mosaico de conchas de vieira por
 * las dos caras sobre un tablero, pared de arena con almenas o dos rocas, peana del interruptor con un caminito de
 * conchitas hasta la puerta y la concha grande del interruptor (malla aparte: baja al pisarla).
 */
namespace TNBeachShellGateDetail
{
	using FBuffers = TNBeachTrapKit::FBuffers;
	using FHulls = TNBeachTrapKit::FHulls;

	constexpr double PostWidth = 26.0;
	constexpr double FrameDepth = 40.0;
	constexpr double LeafThick = 24.0;
	constexpr double LeafLift = 6.0;
	constexpr double WallHalfThick = 100.0;
	constexpr double WallHeight = 440.0;
	constexpr double OpenTime = 0.45;
	constexpr double CloseTime = 0.7;
	/** Tras cerrarse, segundos que sigue despierta (chispas y golpe de las hojas) antes de poder dormirse de lejos. */
	constexpr double SettleTail = 1.0;

	/** Margen (cm) del radio de alcance del portón sobre el vano y el interruptor: la cápsula y el empuje desde fuera. */
	constexpr double ReachMargin = 250.0;

	FLinearColor Driftwood(int32 Index)
	{
		static const uint32 Hex[3] = { 0xB89A7A, 0xA88B6C, 0xC4A886 };
		return TNPlaygroundKit::Rgb(Hex[((Index % 3) + 3) % 3]);
	}

	/** Hoja en el espacio de su bisagra: grosor en X, de Y = 0 (bisagra) a LeafW, de Z = LeafLift a LeafLift + LeafH. */
	void BuildLeaf(FBuffers& B, double LeafW, double LeafH, uint32 Seed)
	{
		const double Z0 = LeafLift;
		TNPlaygroundKit::AddAxisBox(B, FVector(0.0, LeafW * 0.5, Z0 + LeafH * 0.5), FVector(LeafThick * 0.5 - 3.0, LeafW * 0.5, LeafH * 0.5), Driftwood(1));
		const FLinearColor Rope = TNPlaygroundKit::Rgb(0xE9D2A2);
		for (const double Face : { -1.0, 1.0 })
		{
			const double X = Face * (LeafThick * 0.5 - 2.0);
			// Marco de cuerda por el borde de cada cara.
			TNPlaygroundKit::AddAxisBox(B, FVector(X, LeafW * 0.5, Z0 + 5.0), FVector(3.0, LeafW * 0.5, 5.0), Rope);
			TNPlaygroundKit::AddAxisBox(B, FVector(X, LeafW * 0.5, Z0 + LeafH - 5.0), FVector(3.0, LeafW * 0.5, 5.0), Rope);
			TNPlaygroundKit::AddAxisBox(B, FVector(X, 5.0, Z0 + LeafH * 0.5), FVector(3.0, 5.0, LeafH * 0.5), Rope);
			TNPlaygroundKit::AddAxisBox(B, FVector(X, LeafW - 5.0, Z0 + LeafH * 0.5), FVector(3.0, 5.0, LeafH * 0.5), Rope);
			// Filas de conchas escalonadas (como tejas), de abajo arriba.
			const FVector Normal(Face, 0.0, 0.0);
			constexpr double RowStep = 44.0;
			constexpr double ColStep = 50.0;
			const int32 Rows = FMath::Max(1, FMath::FloorToInt32((LeafH - 30.0) / RowStep));
			const int32 Cols = FMath::Max(1, FMath::FloorToInt32((LeafW - 20.0) / ColStep));
			int32 Index = 0;
			for (int32 r = 0; r < Rows; ++r)
			{
				const double Shift = (r % 2 == 0) ? 0.0 : ColStep * 0.5;
				for (int32 c = 0; c < Cols; ++c)
				{
					const double Y = 22.0 + Shift + c * ColStep;
					if (Y > LeafW - 18.0)
					{
						continue;
					}
					const double Z = Z0 + 16.0 + r * RowStep;
					const FLinearColor Col = TNBeachTrapKit::ShellTone(static_cast<int32>(TNPlaygroundKit::Hash01(Index++, r, Seed) * 5.0));
					TNPlaygroundKit::AddShellFan(B, FVector(Face * (LeafThick * 0.5 + 0.5 + 0.4 * r), Y, Z), Normal, FVector::UpVector, 30.0, Col);
				}
			}
			// Tirador: una perla en el borde que se junta con la otra hoja.
			TNPlaygroundKit::AddBall(B, FVector(Face * (LeafThick * 0.5 + 8.0), LeafW - 26.0, Z0 + LeafH * 0.48), 10.0, 10, TNPlaygroundKit::Rgb(0xFFF8F0, 0.7f));
		}
	}

	/** Postes y dintel de madera de deriva con estrella y conchitas; cascos de colisión. */
	void BuildFrame(FBuffers& B, FHulls& Hulls, double DoorW, double DoorH, double LintelTop, uint32 Seed)
	{
		for (const double Side : { -1.0, 1.0 })
		{
			const FVector Center(0.0, Side * (DoorW * 0.5 + PostWidth * 0.5), LintelTop * 0.5);
			const FVector Half(FrameDepth, PostWidth * 0.5, LintelTop * 0.5);
			TNPlaygroundKit::AddAxisBox(B, Center, Half, Driftwood(Side > 0.0 ? 0 : 2));
			Hulls.Add(TNPlaygroundKit::HullAxisBox(Center, Half));
			for (const double Face : { -1.0, 1.0 })
			{
				TNPlaygroundKit::AddShellFan(B, FVector(Face * (FrameDepth + 0.5), Center.Y, DoorH * 0.45), FVector(Face, 0.0, 0.0), FVector::UpVector, 16.0,
					TNBeachTrapKit::ShellTone(static_cast<int32>(Seed % 5u) + 1));
			}
		}
		const double LintelBottom = DoorH + 4.0;
		const FVector LintelC(0.0, 0.0, 0.5 * (LintelBottom + LintelTop));
		const FVector LintelH(FrameDepth + 4.0, DoorW * 0.5 + PostWidth + 8.0, FMath::Max(12.0, 0.5 * (LintelTop - LintelBottom)));
		TNPlaygroundKit::AddAxisBox(B, LintelC, LintelH, Driftwood(1));
		Hulls.Add(TNPlaygroundKit::HullAxisBox(LintelC, LintelH));
		for (const double Face : { -1.0, 1.0 })
		{
			TNPlaygroundKit::AddStarfish(B, FVector(Face * (LintelH.X + 0.5), 0.0, LintelC.Z), FVector(Face, 0.0, 0.0), FVector::UpVector, FMath::Min(38.0, LintelH.Z * 1.4),
				4.0, TNPlaygroundKit::Rgb(0xFF8A70));
		}
	}

	/** Pared de arena a los lados del bastidor y por encima del dintel, con almenas y conchas incrustadas. */
	void BuildWall(FBuffers& B, FHulls& Hulls, double DoorW, double WallHalf, double LintelTop, uint32 Seed)
	{
		const double Inner = DoorW * 0.5 + PostWidth;
		for (const double Side : { -1.0, 1.0 })
		{
			const double Y0 = Side * Inner;
			const double Y1 = Side * WallHalf;
			TNBeachTrapKit::AddSandBox(B, &Hulls, FVector(-WallHalfThick, FMath::Min(Y0, Y1), -20.0), FVector(WallHalfThick, FMath::Max(Y0, Y1), WallHeight));
		}
		TNBeachTrapKit::AddSandBox(B, &Hulls, FVector(-WallHalfThick, -Inner, LintelTop), FVector(WallHalfThick, Inner, WallHeight), false);
		// Almenas (sin colisión: están muy altas).
		const int32 Merlons = FMath::Max(3, FMath::RoundToInt32(2.0 * WallHalf / 95.0));
		for (int32 m = 0; m < Merlons; ++m)
		{
			const double Y = -WallHalf + (m + 0.5) * 2.0 * WallHalf / Merlons;
			if (m % 2 == 0)
			{
				TNPlaygroundKit::AddAxisBox(B, FVector(0.0, Y, WallHeight + 22.0), FVector(WallHalfThick * 0.8, 2.0 * WallHalf / Merlons * 0.45, 22.0), TNBeachTrapKit::SandTop());
			}
		}
		// Conchas y estrellas incrustadas en las dos caras.
		for (int32 d = 0; d < 8; ++d)
		{
			const double Face = (d % 2 == 0) ? -1.0 : 1.0;
			const double Y = (TNPlaygroundKit::Hash01(d, 1, Seed) > 0.5 ? 1.0 : -1.0) * FMath::Lerp(Inner + 50.0, WallHalf - 40.0, TNPlaygroundKit::Hash01(d, 2, Seed));
			const double Z = FMath::Lerp(90.0, WallHeight - 70.0, TNPlaygroundKit::Hash01(d, 3, Seed));
			if (d % 3 == 0)
			{
				TNPlaygroundKit::AddStarfish(B, FVector(Face * (WallHalfThick + 3.0), Y, Z), FVector(Face, 0.0, 0.0), FVector::UpVector, 26.0, 3.0, TNPlaygroundKit::Rgb(0xFF9A70));
			}
			else
			{
				TNPlaygroundKit::AddShellFan(B, FVector(Face * (WallHalfThick + 3.0), Y, Z), FVector(Face, 0.0, 0.0), FVector::UpVector, 24.0, TNBeachTrapKit::ShellTone(d));
			}
		}
	}

	/** Dos rocas a los lados del bastidor. */
	void BuildRocks(FBuffers& B, FHulls& Hulls, double DoorW, double Fit, uint32 Seed)
	{
		const double Inner = DoorW * 0.5 + PostWidth;
		const double RockR = FMath::Max(100.0, (Fit - Inner) / 1.75);
		const double RockY = Inner + RockR * 0.75;
		for (const double Side : { -1.0, 1.0 })
		{
			const FVector Base(0.0, Side * RockY, 0.0);
			TNProcMesh::TNProcAddBoulder(B, Base, RockR, 400.0, Seed + (Side > 0.0 ? 3u : 5u), TNBeachTrapKit::RockTone(Side > 0.0 ? 0 : 2));
			Hulls.Add(TNPlaygroundKit::HullCylinder(Base - FVector(0.0, 0.0, 25.0), 420.0, RockR * 0.95, RockR * 0.45, 10));
		}
		// Algas secas colgando del dintel entre las rocas.
		for (int32 s = 0; s < 5; ++s)
		{
			const double Y = FMath::Lerp(-Inner, Inner, (s + 0.5) / 5.0);
			TNPlaygroundKit::AddRod(B, FVector(-FrameDepth - 4.0, Y, 395.0), FVector(-FrameDepth - 6.0, Y + 6.0, 395.0 - FMath::Lerp(30.0, 70.0, TNPlaygroundKit::Hash01(s, 4, Seed))),
				4.0, 4, TNPlaygroundKit::Rgb(0x4A5E2A, 0.3f), FVector::ForwardVector);
		}
	}

	/** Peana del interruptor (fija) y caminito de conchitas hasta la puerta. */
	void BuildSwitchPad(FBuffers& B, const FVector& At, double Radius, double DoorW)
	{
		TNPlaygroundKit::AddFrustum(B, At - FVector(0.0, 0.0, 6.0), At + FVector(0.0, 0.0, 5.0), Radius + 6.0, Radius, 20, TNPlaygroundKit::Rgb(0xD8D2C4, 0.1f),
			TNPlaygroundKit::Rgb(0xE8E2D4, 0.1f), false, true);
		const FVector Door(-FrameDepth - 30.0, At.Y > 0.0 ? DoorW * 0.3 : -DoorW * 0.3, 1.0);
		for (int32 k = 1; k <= 4; ++k)
		{
			const FVector P = FMath::Lerp(At, Door, k / 5.0) + FVector(0.0, 0.0, 1.5);
			TNPlaygroundKit::AddShellFan(B, P, FVector::UpVector, (Door - At).GetSafeNormal2D(), 14.0, TNBeachTrapKit::ShellTone(k));
		}
	}

	/** Concha grande del interruptor, abombada, con la charnela hacia -X (en su propio espacio, apoyada en Z = 5). */
	void BuildSwitchShell(FBuffers& B, double Radius, uint32 Seed)
	{
		constexpr int32 Ribs = 9;
		const FVector Hinge(-Radius * 0.55, 0.0, 6.0);
		const FLinearColor A = TNBeachTrapKit::ShellTone(static_cast<int32>(Seed % 5u));
		const FLinearColor C = TNPlaygroundKit::Shade(TNPlaygroundKit::Rgb(0xFF9A7A, 0.35f), 1.0);
		for (int32 i = 0; i < Ribs; ++i)
		{
			const double A0 = FMath::DegreesToRadians(-72.0 + 144.0 * i / Ribs);
			const double A1 = FMath::DegreesToRadians(-72.0 + 144.0 * (i + 1) / Ribs);
			const double Am = 0.5 * (A0 + A1);
			const FVector E0 = Hinge + FVector(FMath::Cos(A0), FMath::Sin(A0), 0.0) * Radius * 1.3;
			const FVector E1 = Hinge + FVector(FMath::Cos(A1), FMath::Sin(A1), 0.0) * Radius * 1.3;
			const FVector Ridge = Hinge + FVector(FMath::Cos(Am), FMath::Sin(Am), 0.0) * Radius * 0.75 + FVector(0.0, 0.0, 26.0);
			const FVector Lobe = Hinge + FVector(FMath::Cos(Am), FMath::Sin(Am), 0.0) * Radius * 1.38 + FVector(0.0, 0.0, 6.0);
			const FLinearColor Col = (i % 2 == 0) ? A : C;
			B.AddTri(Hinge + FVector(0.0, 0.0, 10.0), E0, Ridge, FVector::UpVector, Col);
			B.AddTri(Hinge + FVector(0.0, 0.0, 10.0), Ridge, E1, FVector::UpVector, TNPlaygroundKit::Shade(Col, 0.9));
			B.AddTri(E0, Lobe, Ridge, FVector::UpVector, TNPlaygroundKit::Shade(Col, 0.95));
			B.AddTri(Ridge, Lobe, E1, FVector::UpVector, TNPlaygroundKit::Shade(Col, 0.85));
		}
		// Orejetas de la charnela.
		TNPlaygroundKit::AddAxisBox(B, Hinge + FVector(-8.0, 0.0, 2.0), FVector(10.0, Radius * 0.35, 5.0), A);
	}

	double EaseOutBack(double X)
	{
		const double T = FMath::Clamp(X, 0.0, 1.0) - 1.0;
		return 1.0 + T * T * (2.2 * T + 1.2);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// ATN_BeachShellGate
// ─────────────────────────────────────────────────────────────────────────────

ATN_BeachShellGate::ATN_BeachShellGate()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(2.f);

	FrameMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrameMesh"));
	FrameMesh->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureVisual(FrameMesh);

	FrameCollision = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FrameCollision"));
	FrameCollision->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureSolid(FrameCollision, true);

	HingeLeft = CreateDefaultSubobject<USceneComponent>(TEXT("HingeLeft"));
	HingeLeft->SetupAttachment(GetRootComponent());
	HingeRight = CreateDefaultSubobject<USceneComponent>(TEXT("HingeRight"));
	HingeRight->SetupAttachment(GetRootComponent());

	LeafMeshLeft = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeafMeshLeft"));
	LeafMeshLeft->SetupAttachment(HingeLeft);
	TNBeachTrapKit::ConfigureVisual(LeafMeshLeft);
	LeafMeshRight = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeafMeshRight"));
	LeafMeshRight->SetupAttachment(HingeRight);
	TNBeachTrapKit::ConfigureVisual(LeafMeshRight);

	LeafBoxLeft = CreateDefaultSubobject<UBoxComponent>(TEXT("LeafBoxLeft"));
	LeafBoxLeft->SetupAttachment(HingeLeft);
	LeafBoxRight = CreateDefaultSubobject<UBoxComponent>(TEXT("LeafBoxRight"));
	LeafBoxRight->SetupAttachment(HingeRight);
	for (UBoxComponent* Leaf : { LeafBoxLeft.Get(), LeafBoxRight.Get() })
	{
		Leaf->InitBoxExtent(FVector(12.0, 75.0, 160.0));
		Leaf->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
		Leaf->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Leaf->SetGenerateOverlapEvents(false);
		Leaf->SetCanEverAffectNavigation(false);
		Leaf->SetHiddenInGame(true);
	}

	SwitchMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SwitchMesh"));
	SwitchMesh->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureVisual(SwitchMesh);
}

void ATN_BeachShellGate::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_BeachShellGate, GateState);
}

void ATN_BeachShellGate::ApplySpec()
{
	using namespace TNBeachShellGateDetail;
	const double Fit = TNBeachTrapKit::FitRadius(Spec.Element, Spec.SizeScale);
	const uint32 Seed = TNBeachTrapKit::SeedOf(Spec.Seed, 61u);
	const bool bBare = Spec.Extent > 1.f;
	const bool bRocks = !bBare && (static_cast<uint32>(Spec.Seed) & 1u) != 0u;
	const double WallHalf = Fit * 0.95 - 10.0;
	double LintelTop = 0.0;
	if (bBare)
	{
		DoorWidth = FMath::Max(160.0, static_cast<double>(Spec.Extent) - 2.0 * PostWidth);
		DoorHeight = FMath::Min(330.0, BareDoorHeight - 60.0);
		LintelTop = BareDoorHeight;
	}
	else
	{
		DoorWidth = FMath::Clamp(2.0 * (WallHalf - 200.0), 180.0, 300.0);
		DoorHeight = 330.0;
		LintelTop = DoorHeight + 70.0;
	}

	TNBeachTrapKit::FBuffers Frame;
	TNBeachTrapKit::FHulls Hulls;
	BuildFrame(Frame, Hulls, DoorWidth, DoorHeight, LintelTop, Seed);
	if (!bBare)
	{
		if (bRocks)
		{
			BuildRocks(Frame, Hulls, DoorWidth, Fit, Seed);
		}
		else
		{
			BuildWall(Frame, Hulls, DoorWidth, WallHalf, LintelTop, Seed);
		}
	}
	// Interruptor por delante (-X) y a un lado (en la desnuda, siempre a -Y: hacia dentro de la sala).
	const double SwitchSide = bBare ? -1.0 : ((TNPlaygroundKit::Hash01(1, 2, Seed) > 0.5) ? 1.0 : -1.0);
	SwitchLocal = FVector(-380.0, SwitchSide * (DoorWidth * 0.5 + 170.0), 0.0);
	BuildSwitchPad(Frame, SwitchLocal, SwitchRadius, DoorWidth);
	TNBeachTrapKit::SetMesh(FrameMesh, this, Frame, TN_ART("Beach.ShellGate.Frame"));
	FrameCollision->SetCollisionConvexMeshes(Hulls);

	TNBeachTrapKit::FBuffers Shell;
	BuildSwitchShell(Shell, SwitchRadius * 0.62, Seed);
	TNBeachTrapKit::SetMesh(SwitchMesh, this, Shell, TN_ART("Beach.ShellGate.Switch"));
	SwitchMesh->SetRelativeLocation(SwitchLocal);

	// Hojas: la de -Y crece hacia +Y desde su bisagra; la de +Y, girada 180°, igual.
	const double LeafW = DoorWidth * 0.5 - 3.0;
	const double LeafH = DoorHeight - 10.0;
	HingeLeft->SetRelativeLocation(FVector(0.0, -DoorWidth * 0.5, 0.0));
	HingeRight->SetRelativeLocation(FVector(0.0, DoorWidth * 0.5, 0.0));
	TNBeachTrapKit::FBuffers LeafA;
	BuildLeaf(LeafA, LeafW, LeafH, Seed);
	TNBeachTrapKit::SetMesh(LeafMeshLeft, this, LeafA, TN_ART("Beach.ShellGate.Leaf"));
	TNBeachTrapKit::FBuffers LeafB;
	BuildLeaf(LeafB, LeafW, LeafH, Seed + 17u);
	TNBeachTrapKit::SetMesh(LeafMeshRight, this, LeafB, TN_ART("Beach.ShellGate.Leaf"));
	for (UBoxComponent* Leaf : { LeafBoxLeft.Get(), LeafBoxRight.Get() })
	{
		Leaf->SetBoxExtent(FVector(LeafThick * 0.5, LeafW * 0.5, LeafH * 0.5), false);
		Leaf->SetRelativeLocation(FVector(0.0, LeafW * 0.5, LeafLift + LeafH * 0.5));
	}
	UE_LOG(LogTortunabo, Verbose, TEXT("[Playa] Puerta de conchas %s: %s, hueco de %.0f x %.0f."), *GetName(),
		bBare ? TEXT("desnuda") : (bRocks ? TEXT("entre rocas") : TEXT("en pared")), DoorWidth, DoorHeight);
}

void ATN_BeachShellGate::BeginPlay()
{
	Super::BeginPlay();
	Voice = UTN_BeachTrapSynthComponent::AttachTo(this, GetActorTransform().TransformPosition(FVector(0.0, 0.0, 160.0)), 600.f, 3000.f);
	Sparkle.Init(this, ETNTrapBurstShape::Chip, TNPlaygroundKit::Rgb(0xFFF3E4, 0.6f), 20);
	Sparkle.SetMotion(-500.f, 1.5f, 16.f, 4.f, 0.4f, 0.8f);
	bLastOpen = GateState.bOpen;
}

// ─────────────────────────────────────────────────────────────────────────────
// Estado
// ─────────────────────────────────────────────────────────────────────────────

double ATN_BeachShellGate::LeafAngle(double ServerTime) const
{
	using namespace TNBeachShellGateDetail;
	const double T = FMath::Max(0.0, ServerTime - static_cast<double>(GateState.ChangedAt));
	if (GateState.bOpen)
	{
		return FMath::Lerp(static_cast<double>(GateState.FromAngle), static_cast<double>(OpenDeg), EaseOutBack(T / OpenTime));
	}
	const double U = FMath::Clamp(T / CloseTime, 0.0, 1.0);
	return FMath::Max(0.0, FMath::Lerp(static_cast<double>(GateState.FromAngle), 0.0, U * U));
}

double ATN_BeachShellGate::ReachRadius() const
{
	const double Local = FMath::Max(DoorWidth, FVector2D(SwitchLocal.X, SwitchLocal.Y).Size() + SwitchRadius) + TNBeachShellGateDetail::ReachMargin;
	return Local * GetActorTransform().GetMaximumAxisScale();
}

bool ATN_BeachShellGate::IsOnSwitch(const ACharacter* Character) const
{
	const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (!Capsule)
	{
		return false;
	}
	const FVector Local = GetActorTransform().InverseTransformPosition(Capsule->GetComponentLocation());
	const double Feet = Local.Z - Capsule->GetScaledCapsuleHalfHeight();
	return FVector2D(Local.X - SwitchLocal.X, Local.Y - SwitchLocal.Y).Size() < SwitchRadius && Feet > -40.0 && Feet < 90.0;
}

void ATN_BeachShellGate::SetOpen(bool bInOpen, int8 InDir, double ServerTime)
{
	const double Current = LeafAngle(ServerTime);
	GateState.FromAngle = static_cast<float>(Current);
	// Si ya estaba entreabierta hacia un lado, sigue hacia ese lado (sin saltos).
	GateState.Dir = Current > 1.0 ? GateState.Dir : InDir;
	GateState.bOpen = bInOpen;
	GateState.ChangedAt = static_cast<float>(ServerTime);
	if (bInOpen)
	{
		LastHoldTime = ServerTime;
	}
	PushTime.Reset();
	HandleStateChanged();
	ForceNetUpdate();
}

void ATN_BeachShellGate::ServerUpdate(float DeltaSeconds, double ServerTime)
{
	using namespace TNBeachShellGateDetail;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const FTransform ActorXf = GetActorTransform();
	const double LeafW = DoorWidth * 0.5;
	bool bHold = false;
	bool bSwitch = false;
	int8 PushDir = 0;
	TArray<ACharacter*> Near;
	TNBeachNearby::Gather(World, ActorXf.GetLocation(), ReachRadius(), Near);
	for (ACharacter* Walker : Near)
	{
		if (!TNBeachTrapKit::IsFreeTurtle(Walker))
		{
			continue;
		}
		const UCapsuleComponent* Capsule = Walker->GetCapsuleComponent();
		const FVector Local = ActorXf.InverseTransformPosition(Walker->GetActorLocation());
		const double CapR = Capsule ? Capsule->GetScaledCapsuleRadius() : 34.0;
		bSwitch |= IsOnSwitch(Walker);
		const bool bInDoorway = FMath::Abs(Local.Y) < DoorWidth * 0.5 && FMath::Abs(Local.X) < LeafW + 60.0 && Local.Z > -100.0 && Local.Z < DoorHeight + 150.0;
		bHold |= bInDoorway;
		// Empujar: pegada a una hoja cerrada y andando hacia ella.
		const UCharacterMovementComponent* Move = Walker->GetCharacterMovement();
		const bool bAgainst = !GateState.bOpen && Move && !Move->IsFalling() && FMath::Abs(Local.Y) < DoorWidth * 0.5 - 10.0
			&& FMath::Abs(Local.X) < LeafThick * 0.5 + CapR + 35.0 && Local.Z < DoorHeight;
		float& Pushed = PushTime.FindOrAdd(Walker);
		if (bAgainst)
		{
			const FVector LocalAccel = ActorXf.InverseTransformVectorNoScale(Move->GetCurrentAcceleration()).GetSafeNormal2D();
			const double Toward = Local.X < 0.0 ? LocalAccel.X : -LocalAccel.X;
			Pushed = Toward > 0.5 ? Pushed + DeltaSeconds : FMath::Max(0.f, Pushed - DeltaSeconds);
			if (Pushed >= PushSeconds)
			{
				PushDir = static_cast<int8>(Local.X < 0.0 ? 1 : -1);
			}
		}
		else
		{
			Pushed = FMath::Max(0.f, Pushed - DeltaSeconds * 2.f);
		}
	}
	for (auto It = PushTime.CreateIterator(); It; ++It)
	{
		// Quien se ha alejado deja de empujar: se le descuenta como a quien no va hacia la hoja.
		if (!Near.Contains(It.Key().Get()))
		{
			It.Value() = FMath::Max(0.f, It.Value() - DeltaSeconds * 2.f);
		}
		if (!It.Key().IsValid() || It.Value() <= 0.f)
		{
			It.RemoveCurrent();
		}
	}

	if (bSwitch || (GateState.bOpen && bHold))
	{
		LastHoldTime = ServerTime;
	}
	if (!GateState.bOpen)
	{
		if (bSwitch)
		{
			// El interruptor está delante (-X): abre hacia +X.
			SetOpen(true, 1, ServerTime);
		}
		else if (PushDir != 0)
		{
			SetOpen(true, PushDir, ServerTime);
		}
		else if (bHold && LeafAngle(ServerTime) > 3.0)
		{
			// Alguien se ha metido mientras se cerraba: vuelve a abrirse.
			SetOpen(true, GateState.Dir, ServerTime);
		}
	}
	else if (ServerTime - LastHoldTime > OpenHold && LeafAngle(ServerTime) >= OpenDeg - 1.0)
	{
		SetOpen(false, GateState.Dir, ServerTime);
	}
}

void ATN_BeachShellGate::OnRep_GateState()
{
	// Dormida por distancia (UTN_BeachTickWakeSubsystem): se abre o se cierra ya, sin esperar a la siguiente mirada.
	if (!IsActorTickEnabled())
	{
		SetActorTickEnabled(true);
	}
	HandleStateChanged();
}

float ATN_BeachShellGate::GetTickWakeDistance() const
{
	return GetFootprintRadius() + 0.5f * FMath::Max(0.f, Spec.Extent) + TNBeachTickWake::ReachMargin;
}

bool ATN_BeachShellGate::IsTickBusy() const
{
	const double SinceChange = TNBeachTrapKit::ServerNow(GetWorld()) - static_cast<double>(GateState.ChangedAt);
	return GateState.bOpen || SinceChange < TNBeachShellGateDetail::CloseTime + TNBeachShellGateDetail::SettleTail;
}

void ATN_BeachShellGate::HandleStateChanged()
{
	if (GateState.bOpen == bLastOpen)
	{
		return;
	}
	bLastOpen = GateState.bOpen;
	if (GetNetMode() == NM_DedicatedServer || TNBeachTrapKit::ServerNow(GetWorld()) - GateState.ChangedAt > 1.5)
	{
		return;
	}
	const FVector Door = GetActorTransform().TransformPosition(FVector(0.0, 0.0, DoorHeight * 0.5));
	if (Voice)
	{
		Voice->TriggerSoundAt(ETNBeachTrapSound::Clack, Door, GateState.bOpen ? 1.f : 0.85f, 0.9f);
		Voice->TriggerSound(ETNBeachTrapSound::Grind, GateState.bOpen ? 1.1f : 0.9f, 0.7f);
	}
	if (GateState.bOpen)
	{
		Sparkle.Burst(Door, 10, FVector::UpVector, 300.f, 1.4f, static_cast<float>(DoorWidth * 0.4));
	}
}

void ATN_BeachShellGate::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = Clock.Advance(GetWorld(), DeltaSeconds);
	if (HasAuthority())
	{
		ServerUpdate(DeltaSeconds, TNBeachTrapKit::ServerNow(GetWorld()));
	}

	// Hojas: ángulo desde el estado replicado; solo chocan cerradas.
	const double Angle = LeafAngle(Now);
	const int8 DirSign = GateState.Dir >= 0 ? 1 : -1;
	if (Angle != ShownLeafAngle || DirSign != ShownLeafDir)
	{
		ShownLeafAngle = Angle;
		ShownLeafDir = DirSign;
		const double Dir = static_cast<double>(DirSign);
		HingeLeft->SetRelativeRotation(FRotator(0.0, -Angle * Dir, 0.0));
		HingeRight->SetRelativeRotation(FRotator(0.0, 180.0 + Angle * Dir, 0.0));
	}
	const bool bSolid = Angle < 3.0;
	if (bSolid != bLeavesSolid)
	{
		bLeavesSolid = bSolid;
		const ECollisionEnabled::Type Mode = bSolid ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision;
		LeafBoxLeft->SetCollisionEnabled(Mode);
		LeafBoxRight->SetCollisionEnabled(Mode);
		if (bSolid && Voice && GetNetMode() != NM_DedicatedServer)
		{
			Voice->TriggerSound(ETNBeachTrapSound::Clack, 0.75f, 0.7f);
		}
	}

	// Interruptor: baja en cada máquina con quien vea encima.
	bool bPressed = false;
	TArray<ACharacter*> Near;
	TNBeachNearby::Gather(GetWorld(), GetActorLocation(), ReachRadius(), Near);
	for (int32 Index = 0; Index < Near.Num() && !bPressed; ++Index)
	{
		bPressed = TNBeachTrapKit::IsFreeTurtle(Near[Index]) && IsOnSwitch(Near[Index]);
	}
	if (bPressed && !bSwitchDown && Voice && GetNetMode() != NM_DedicatedServer)
	{
		Voice->TriggerSoundAt(ETNBeachTrapSound::Plink, GetActorTransform().TransformPosition(SwitchLocal), 1.f, 0.9f);
	}
	bSwitchDown = bPressed;
	SwitchPress = FMath::FInterpTo(SwitchPress, bPressed ? 1.f : 0.f, DeltaSeconds, 18.f);
	if (SwitchPress != ShownSwitchPress)
	{
		ShownSwitchPress = SwitchPress;
		SwitchMesh->SetRelativeLocation(SwitchLocal - FVector(0.0, 0.0, 10.0 * SwitchPress));
	}
	Sparkle.Tick(DeltaSeconds);
}
