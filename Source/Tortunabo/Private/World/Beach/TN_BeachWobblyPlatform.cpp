#include "World/Beach/TN_BeachWobblyPlatform.h"
#include "World/Beach/TN_BeachTrapSynthComponent.h"
#include "Core/TN_Log.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "TN_BeachTrapKit.h"

/**
 * Geometría del hoyo y de la tabla. El cráter es una superficie de revolución (perfil: fondo, pared de dentro, cresta
 * redondeada y ladera de fuera) en tramos de 15°, sin los tramos de la brecha (lado +Y) y con tapas en sus bordes; la
 * colisión, un casco convexo por tramo. La tabla se construye por trozos recortados a un intervalo de X: entera, o cada
 * mitad (con astillas en el corte) en el espacio de su bisagra.
 */
namespace TNBeachWobblyDetail
{
	using FBuffers = TNBeachTrapKit::FBuffers;
	using FHulls = TNBeachTrapKit::FHulls;

	constexpr int32 RingSegs = 24;
	constexpr double RimWidth = 90.0;
	/** La tabla se apoya esto en la cresta por cada lado. */
	constexpr double RestOverlap = 70.0;
	/** Semiancho de la brecha en el borde del hoyo. */
	constexpr double BreachHalfWidth = 120.0;
	/** Lo más que un cliente adelanta una muestra del servidor (s): con más ping, la tabla se queda ese poco detrás. */
	constexpr float MaxClientLeadSeconds = 0.15f;
	/** Mientras la pose cambia, el servidor vuelve a despertar la réplica cada tanto (s); se duerme NetWakeSeconds después. */
	constexpr double PoseWakeInterval = 1.0;

	struct FCraterDims
	{
		double Rf = 280.0;
		double Rp = 364.0;
		double Ro = 870.0;
		double H = 240.0;
	};

	bool InBreach(int32 Seg, const FCraterDims& D)
	{
		const double Center = 360.0 * (Seg + 0.5) / RingSegs;
		const double Half = FMath::RadiansToDegrees(FMath::Atan(BreachHalfWidth / FMath::Max(1.0, D.Rp)));
		return FMath::Abs(FMath::FindDeltaAngleDegrees(Center, 90.0)) < FMath::Max(Half, 360.0 / RingSegs * 0.5);
	}

	/** Cráter con su brecha, fondo mojado, guijarros y un banderín de juguete; cascos de colisión por tramo. */
	void BuildCrater(FBuffers& B, FHulls& Hulls, const FCraterDims& D, uint32 Seed)
	{
		const TArray<FVector2D> Profile = { FVector2D(D.Rf - 6.0, -3.0), FVector2D(D.Rf, 0.0), FVector2D(D.Rp, D.H), FVector2D(D.Rp + RimWidth * 0.5, D.H + 10.0),
			FVector2D(D.Rp + RimWidth, D.H), FVector2D(D.Ro, 0.0), FVector2D(D.Ro + 25.0, -25.0) };
		const double Jitter[7] = { 0.03, 0.02, 0.0, 0.0, 0.0, 0.02, 0.03 };
		const FLinearColor WallIn = TNPlaygroundKit::Mix(TNBeachTrapKit::SandWet(), TNBeachTrapKit::SandSide(), 0.35);
		const FLinearColor SegColors[6] = { TNBeachTrapKit::SandDeep(), WallIn, TNBeachTrapKit::SandTop(), TNBeachTrapKit::SandTop(), TNBeachTrapKit::SandSide(),
			TNBeachTrapKit::SandSide() };
		const int32 NumP = Profile.Num();

		// Rejilla de puntos (la misma para los tramos vecinos: sin grietas).
		TArray<FVector> Grid;
		Grid.SetNum(NumP * (RingSegs + 1));
		for (int32 k = 0; k <= RingSegs; ++k)
		{
			const double A = TNPlaygroundKit::KitTwoPi * k / RingSegs;
			for (int32 p = 0; p < NumP; ++p)
			{
				const double R = Profile[p].X * (1.0 + Jitter[p] * TNProcMesh::TNProcHashNoise(p, k % RingSegs, Seed));
				Grid[k * NumP + p] = FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, Profile[p].Y);
			}
		}
		for (int32 k = 0; k < RingSegs; ++k)
		{
			const bool bGap = InBreach(k, D);
			if (!bGap)
			{
				const double Am = TNPlaygroundKit::KitTwoPi * (k + 0.5) / RingSegs;
				for (int32 p = 0; p + 1 < NumP; ++p)
				{
					const FVector2D Dir = Profile[p + 1] - Profile[p];
					const FVector Hint(-Dir.Y * FMath::Cos(Am), -Dir.Y * FMath::Sin(Am), Dir.X);
					B.AddQuad(Grid[k * NumP + p], Grid[(k + 1) * NumP + p], Grid[(k + 1) * NumP + p + 1], Grid[k * NumP + p + 1], Hint, SegColors[p]);
				}
				const TArray<FVector2D> Section = { FVector2D(D.Rf, 0.0), FVector2D(D.Rp, D.H), FVector2D(D.Rp + RimWidth, D.H), FVector2D(D.Ro, 0.0),
					FVector2D(D.Ro, -40.0), FVector2D(D.Rf, -40.0) };
				Hulls.Add(TNBeachTrapKit::HullRingSector(FVector::ZeroVector, TNPlaygroundKit::KitTwoPi * k / RingSegs, TNPlaygroundKit::KitTwoPi * (k + 1) / RingSegs,
					Section));
			}
			// Tapa en el borde de la brecha: la sección del montón, mirando hacia el hueco.
			const bool bNextGap = InBreach((k + 1) % RingSegs, D);
			if (bGap != bNextGap)
			{
				const int32 Edge = k + 1;
				const double Ae = TNPlaygroundKit::KitTwoPi * Edge / RingSegs;
				const FVector Tangent(-FMath::Sin(Ae), FMath::Cos(Ae), 0.0);
				const FVector Facing = bNextGap ? Tangent : -Tangent;
				const FVector& P1 = Grid[Edge * NumP + 1];
				for (int32 p = 2; p + 1 <= 5; ++p)
				{
					B.AddTri(P1, Grid[Edge * NumP + p], Grid[Edge * NumP + p + 1], Facing, TNPlaygroundKit::Shade(TNBeachTrapKit::SandSide(), 0.9));
				}
			}
		}

		// Fondo mojado y camino de salida por la brecha.
		constexpr int32 FloorSeg = 24;
		const FLinearColor FloorCol = TNPlaygroundKit::Mix(TNBeachTrapKit::SandDeep(), TNBeachTrapKit::SandWet(), 0.5);
		for (int32 k = 0; k < FloorSeg; ++k)
		{
			const double A0 = TNPlaygroundKit::KitTwoPi * k / FloorSeg;
			const double A1 = TNPlaygroundKit::KitTwoPi * (k + 1) / FloorSeg;
			B.AddTri(FVector(0.0, 0.0, 1.5), FVector(FMath::Cos(A0) * D.Rf, FMath::Sin(A0) * D.Rf, 1.5), FVector(FMath::Cos(A1) * D.Rf, FMath::Sin(A1) * D.Rf, 1.5),
				FVector::UpVector, FloorCol);
		}
		B.AddQuad(FVector(-BreachHalfWidth, D.Rf - 5.0, 1.5), FVector(BreachHalfWidth, D.Rf - 5.0, 1.5), FVector(BreachHalfWidth * 1.4, D.Ro + 20.0, 1.0),
			FVector(-BreachHalfWidth * 1.4, D.Ro + 20.0, 1.0), FVector::UpVector, TNPlaygroundKit::Mix(FloorCol, TNBeachTrapKit::SandSide(), 0.5));

		// Guijarros y una conchita en el fondo.
		for (int32 s = 0; s < 6; ++s)
		{
			const double A = TNPlaygroundKit::KitTwoPi * TNPlaygroundKit::Hash01(s, 1, Seed);
			const double R = D.Rf * FMath::Lerp(0.15, 0.85, TNPlaygroundKit::Hash01(s, 2, Seed));
			TNBeachTrapKit::AddPebble(B, FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 2.0), FMath::Lerp(8.0, 16.0, TNPlaygroundKit::Hash01(s, 3, Seed)), Seed + s,
				TNBeachTrapKit::RockTone(s));
		}
		TNPlaygroundKit::AddShellFan(B, FVector(D.Rf * 0.4, -D.Rf * 0.3, 2.0), FVector::UpVector, FVector::ForwardVector, 22.0,
			TNBeachTrapKit::ShellTone(static_cast<int32>(Seed % 5u)));

		// Banderín de juguete clavado en la cresta (se ve de lejos).
		const double FlagA = FMath::DegreesToRadians(225.0);
		const FVector FlagFoot(FMath::Cos(FlagA) * (D.Rp + RimWidth * 0.5), FMath::Sin(FlagA) * (D.Rp + RimWidth * 0.5), D.H);
		TNPlaygroundKit::AddRod(B, FlagFoot - FVector(0.0, 0.0, 20.0), FlagFoot + FVector(0.0, 0.0, 230.0), 4.0, 6, TNPlaygroundKit::Rgb(0xFFF6E0, 0.2f), FVector::ForwardVector);
		TNPlaygroundKit::AddPennant(B, FlagFoot + FVector(0.0, 0.0, 228.0), FVector(1.0, 0.3, 0.0), 90.0, 55.0, TNPlaygroundKit::ToyColor(static_cast<int32>(Seed % 7u)));
	}

	/** Caja recortada al intervalo [X0, X1] de X (nada si queda vacía). */
	void AddClippedBox(FBuffers& B, const FTransform& Xf, double MinX, double MaxX, double MinY, double MaxY, double MinZ, double MaxZ, double X0, double X1,
		const FLinearColor& Color)
	{
		const double A = FMath::Max(MinX, X0);
		const double C = FMath::Min(MaxX, X1);
		if (C - A < 0.5)
		{
			return;
		}
		TNPlaygroundKit::AddXfBox(B, Xf, FVector((A + C) * 0.5, (MinY + MaxY) * 0.5, (MinZ + MaxZ) * 0.5), FVector((C - A) * 0.5, (MaxY - MinY) * 0.5, (MaxZ - MinZ) * 0.5),
			Color);
	}

	/**
	 * Tabla (vieja, de tres tablones con travesaños) o tapa de nevera (plástico blanco con reborde de color, panel en
	 * relieve, bisagras y cierre) entre X0 y X1 de su espacio (centro en X = 0, cara de abajo en Z = 0), llevada con Xf.
	 * Con bSplinters, astillas o esquirlas en el corte de X = 0.
	 */
	void BuildBoard(FBuffers& B, const FTransform& Xf, double HalfL, double HalfW, double Thick, bool bLid, uint32 Seed, double X0, double X1, bool bSplinters)
	{
		const FVector Up = Xf.TransformVectorNoScale(FVector::UpVector);
		if (!bLid)
		{
			constexpr int32 Planks = 3;
			const double PlankW = 2.0 * HalfW / Planks;
			for (int32 k = 0; k < Planks; ++k)
			{
				const double Y0 = -HalfW + k * PlankW + 1.5;
				const double Y1 = Y0 + PlankW - 3.0;
				const FLinearColor Wood = TNPlaygroundKit::Shade(TNBeachTrapKit::WoodTone(k + static_cast<int32>(Seed % 3u)), 0.9 + 0.2 * TNPlaygroundKit::Hash01(k, 1, Seed));
				AddClippedBox(B, Xf, -HalfL, HalfL, Y0, Y1, 0.0, Thick, X0, X1, Wood);
				// Vetas.
				for (const double Line : { 0.3, 0.7 })
				{
					const double Yl = FMath::Lerp(Y0, Y1, Line);
					AddClippedBox(B, Xf, -HalfL + 12.0, HalfL - 12.0, Yl - 0.8, Yl + 0.8, Thick, Thick + 0.3, X0, X1, TNPlaygroundKit::Shade(Wood, 0.75));
				}
				// Clavos cerca de los extremos.
				for (const double NailX : { -HalfL + 18.0, HalfL - 18.0 })
				{
					const double Yn = (Y0 + Y1) * 0.5;
					AddClippedBox(B, Xf, NailX - 2.0, NailX + 2.0, Yn - 2.0, Yn + 2.0, Thick, Thick + 0.6, X0, X1, TNPlaygroundKit::Rgb(0x4A4F5A, 0.5f));
				}
			}
			// Travesaños por debajo (sobre el hoyo, no sobre la cresta).
			for (const double Bx : { -0.55, 0.55 })
			{
				AddClippedBox(B, Xf, Bx * HalfL - 12.0, Bx * HalfL + 12.0, -HalfW + 6.0, HalfW - 6.0, -9.0, 0.0, X0, X1, TNBeachTrapKit::WoodTone(2));
			}
			// La grieta que avisa (en el centro).
			AddClippedBox(B, Xf, -3.0, 3.0, -HalfW * 0.6, HalfW * 0.3, Thick, Thick + 0.4, X0, X1, TNPlaygroundKit::Rgb(0x3A2616));
		}
		else
		{
			const FLinearColor Plastic = TNPlaygroundKit::Rgb(0xF4F1EA, 0.25f);
			const FLinearColor Band = TNPlaygroundKit::ToyColor((Seed % 2u) == 0u ? 3 : 0, 0.25f);
			AddClippedBox(B, Xf, -HalfL, HalfL, -HalfW, HalfW, Thick * 0.35, Thick, X0, X1, Plastic);
			AddClippedBox(B, Xf, -HalfL - 5.0, HalfL + 5.0, -HalfW - 5.0, HalfW + 5.0, 0.0, Thick * 0.45, X0, X1, Band);
			AddClippedBox(B, Xf, -HalfL * 0.8, HalfL * 0.8, -HalfW * 0.68, HalfW * 0.68, Thick, Thick + 4.0, X0, X1, TNPlaygroundKit::Rgb(0xFFFFFF, 0.3f));
			// Bisagras en el borde +Y y cierre en el -Y.
			for (const double Hx : { -0.55, 0.0, 0.55 })
			{
				AddClippedBox(B, Xf, Hx * HalfL - 22.0, Hx * HalfL + 22.0, HalfW + 2.0, HalfW + 12.0, Thick * 0.3, Thick * 0.9, X0, X1, Band);
			}
			AddClippedBox(B, Xf, -30.0, 30.0, -HalfW - 14.0, -HalfW - 2.0, Thick * 0.2, Thick * 1.1, X0, X1, TNPlaygroundKit::Shade(Band, 0.8));
			// Pegatina de estrella en una punta.
			const double StarX = X0 < 0.0 ? -HalfL * 0.6 : HalfL * 0.6;
			if (StarX > X0 && StarX < X1)
			{
				TNPlaygroundKit::AddStarfish(B, Xf.TransformPosition(FVector(StarX, 0.0, Thick + 4.3)), Up, Xf.TransformVectorNoScale(FVector::ForwardVector), 45.0, 2.0,
					TNPlaygroundKit::Rgb(0xFFCB3D, 0.2f));
			}
			AddClippedBox(B, Xf, -3.0, 3.0, -HalfW * 0.5, HalfW * 0.5, Thick + 4.0, Thick + 4.4, X0, X1, TNPlaygroundKit::Rgb(0x8A8A8A));
		}
		if (bSplinters)
		{
			// Astillas (madera clara recién rota) o esquirlas de plástico que asoman del corte hacia el otro lado.
			const FLinearColor Fresh = bLid ? TNPlaygroundKit::Rgb(0xFFFFFF, 0.25f) : TNPlaygroundKit::Rgb(0xE8C28C);
			const double Dir = X0 < 0.0 ? 1.0 : -1.0;
			const int32 Count = 7;
			for (int32 s = 0; s < Count; ++s)
			{
				const double Yc = FMath::Lerp(-HalfW + 12.0, HalfW - 12.0, (s + 0.5) / Count);
				const double Len = FMath::Lerp(6.0, 30.0, TNPlaygroundKit::Hash01(s, 9, Seed + (X0 < 0.0 ? 1u : 2u)));
				const double Wd = FMath::Lerp(6.0, 14.0, TNPlaygroundKit::Hash01(s, 10, Seed));
				const double Xa = Dir > 0.0 ? 0.0 : -Len;
				TNPlaygroundKit::AddXfBox(B, Xf, FVector(Xa + Len * 0.5, Yc, Thick * 0.5), FVector(Len * 0.5, Wd * 0.5, Thick * 0.35), Fresh);
			}
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// TNWobblyPlatformNet: el muelle de la tabla, el mismo en el servidor y en los clientes
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	/** Un paso semiimplícito del muelle de los dos ejes hacia lo que tiran. */
	void StepSpringAxes(TNWobblyPlatformNet::FPoseState& State, float Dt, float MaxRollDeg)
	{
		using namespace TNWobblyPlatformNet;
		const float RollLimit = MaxRollDeg * RollOvershoot;
		State.RollVel += (-RollStiffness * (State.Roll - State.RollTarget) - RollDamping * State.RollVel) * Dt;
		State.Roll = FMath::Clamp(State.Roll + State.RollVel * Dt, -RollLimit, RollLimit);
		State.PitchVel += (-PitchStiffness * (State.Pitch - State.PitchTarget) - PitchDamping * State.PitchVel) * Dt;
		State.Pitch = FMath::Clamp(State.Pitch + State.PitchVel * Dt, -PitchLimitDeg, PitchLimitDeg);
	}
}

FTNWobblyPoseNet TNWobblyPlatformNet::EncodePose(const FPoseState& State)
{
	FTNWobblyPoseNet Net;
	Net.Roll = QuantizeRoll(State.Roll);
	Net.Pitch = QuantizePitch(State.Pitch);
	Net.RollRate = QuantizeRate(State.RollVel, RollRateStepDeg);
	Net.PitchRate = QuantizeRate(State.PitchVel, PitchRateStepDeg);
	Net.RollTarget = QuantizeRoll(State.RollTarget);
	Net.PitchTarget = QuantizePitch(State.PitchTarget);
	Net.Sag = QuantizeSag(State.Sag);
	return Net;
}

TNWobblyPlatformNet::FPoseState TNWobblyPlatformNet::DecodePose(const FTNWobblyPoseNet& Net)
{
	FPoseState State;
	State.Roll = DequantizeRoll(Net.Roll);
	State.Pitch = DequantizePitch(Net.Pitch);
	State.RollVel = DequantizeRate(Net.RollRate, RollRateStepDeg);
	State.PitchVel = DequantizeRate(Net.PitchRate, PitchRateStepDeg);
	State.RollTarget = DequantizeRoll(Net.RollTarget);
	State.PitchTarget = DequantizePitch(Net.PitchTarget);
	State.Sag = DequantizeSag(Net.Sag);
	return State;
}

void TNWobblyPlatformNet::StepServerPose(FPoseState& State, const FRiderInput& Input, double Now, float DeltaSeconds, float MaxRollDeg)
{
	// Se ladea hacia quien está encima, se mece al andar y los aterrizajes la sacuden.
	const float Rock = Input.Motion * RockDeg * static_cast<float>(FMath::Sin(Now * RockRadPerSec));
	State.RollTarget = FMath::Clamp(Input.SumY * RollLeanDeg + Rock, -MaxRollDeg, MaxRollDeg);
	State.PitchTarget = FMath::Clamp(Input.SumX * PitchLeanDeg, -PitchLeanLimitDeg, PitchLeanLimitDeg);
	State.RollVel += Input.Kick * RollKickDegPerSec * (Input.SumY >= 0.f ? 1.f : -1.f);
	State.PitchVel += Input.Kick * PitchKickDegPerSec * (Input.SumX >= 0.f ? 1.f : -1.f);
	StepSpringAxes(State, FMath::Min(DeltaSeconds, MaxSpringStep), MaxRollDeg);
}

void TNWobblyPlatformNet::AdvancePose(FPoseState& State, float Seconds, float MaxRollDeg)
{
	for (float Left = Seconds; Left > UE_KINDA_SMALL_NUMBER; Left -= MaxSpringStep)
	{
		StepSpringAxes(State, FMath::Min(Left, MaxSpringStep), MaxRollDeg);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// ATN_BeachWobblyPlatform
// ─────────────────────────────────────────────────────────────────────────────

ATN_BeachWobblyPlatform::ATN_BeachWobblyPlatform()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bAlwaysRelevant = true;
	// La pose de la tabla sale hasta 15 veces por segundo mientras cambia (en la ronda, dormida si está quieta).
	SetNetUpdateFrequency(PoseNetFrequency);
	SetMinNetUpdateFrequency(PoseNetFrequency);

	CraterMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CraterMesh"));
	CraterMesh->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureVisual(CraterMesh);

	CraterCollision = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("CraterCollision"));
	CraterCollision->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureSolid(CraterCollision, false);

	BoardPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BoardPivot"));
	BoardPivot->SetupAttachment(GetRootComponent());

	BoardMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BoardMesh"));
	BoardMesh->SetupAttachment(BoardPivot);
	TNBeachTrapKit::ConfigureVisual(BoardMesh);

	// Tabla: base móvil con nombre estable por red (el cliente manda su posición relativa a ella).
	BoardBox = CreateDefaultSubobject<UBoxComponent>(TEXT("BoardBox"));
	BoardBox->SetupAttachment(BoardPivot);
	BoardBox->InitBoxExtent(FVector(400.0, 110.0, 12.0));
	BoardBox->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	BoardBox->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	BoardBox->SetGenerateOverlapEvents(false);
	BoardBox->SetCanEverAffectNavigation(false);
	BoardBox->SetHiddenInGame(true);

	HalfPivotA = CreateDefaultSubobject<USceneComponent>(TEXT("HalfPivotA"));
	HalfPivotA->SetupAttachment(GetRootComponent());
	HalfPivotB = CreateDefaultSubobject<USceneComponent>(TEXT("HalfPivotB"));
	HalfPivotB->SetupAttachment(GetRootComponent());

	HalfMeshA = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HalfMeshA"));
	HalfMeshA->SetupAttachment(HalfPivotA);
	TNBeachTrapKit::ConfigureVisual(HalfMeshA);
	HalfMeshA->SetVisibility(false);
	HalfMeshB = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HalfMeshB"));
	HalfMeshB->SetupAttachment(HalfPivotB);
	TNBeachTrapKit::ConfigureVisual(HalfMeshB);
	HalfMeshB->SetVisibility(false);

	HalfBoxA =CreateDefaultSubobject<UBoxComponent>(TEXT("HalfBoxA"));
	HalfBoxA->SetupAttachment(HalfPivotA);
	HalfBoxB = CreateDefaultSubobject<UBoxComponent>(TEXT("HalfBoxB"));
	HalfBoxB->SetupAttachment(HalfPivotB);
	for (UBoxComponent* Half : { HalfBoxA.Get(), HalfBoxB.Get() })
	{
		Half->InitBoxExtent(FVector(200.0, 110.0, 12.0));
		Half->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
		Half->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Half->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Half->SetGenerateOverlapEvents(false);
		Half->SetCanEverAffectNavigation(false);
		Half->SetHiddenInGame(true);
	}
}

void ATN_BeachWobblyPlatform::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_BeachWobblyPlatform, BrokenAt);
	DOREPLIFETIME(ATN_BeachWobblyPlatform, NetPose);
}

void ATN_BeachWobblyPlatform::ApplyRoundNetProfile()
{
	Super::ApplyRoundNetProfile();
	// La base la deja a 2 Hz (lo quieto que duerme). Despierta, esta se mueve: la pose sale a PoseNetFrequency.
	SetNetUpdateFrequency(PoseNetFrequency);
	SetMinNetUpdateFrequency(PoseNetFrequency);
}

void ATN_BeachWobblyPlatform::PublishPose(double Now, bool bUrgent)
{
	const FTNWobblyPoseNet NewPose = TNWobblyPlatformNet::EncodePose(Pose);
	if (NewPose == NetPose)
	{
		return;
	}
	NetPose = NewPose;
	// Dormida (la red de la ronda), un cambio no sale: ForceNetUpdate la despierta y la vuelve a dormir NetWakeSeconds después
	// del último aviso. Mientras se mueve se avisa cada PoseWakeInterval; entre medias sale sola, a PoseNetFrequency. La
	// sacudida de un aterrizaje sale ya: esperando al siguiente envío, el cliente la vería hasta 67 ms tarde.
	if (bUrgent || Now >= NextNetWake)
	{
		NextNetWake = Now + TNBeachWobblyDetail::PoseWakeInterval;
		ForceNetUpdate();
	}
}

void ATN_BeachWobblyPlatform::OnRep_NetPose()
{
	Pose = TNWobblyPlatformNet::DecodePose(NetPose);
	TNWobblyPlatformNet::AdvancePose(Pose, ClientLeadSeconds(), MaxRollDeg);
	bPoseJustSeeded = true;
}

void ATN_BeachWobblyPlatform::FollowServerPose(float DeltaSeconds)
{
	// La muestra llega al principio del fotograma, ya adelantada a esta hora: moverla otra vez la pondría por delante.
	if (bPoseJustSeeded)
	{
		bPoseJustSeeded = false;
		return;
	}
	TNWobblyPlatformNet::AdvancePose(Pose, DeltaSeconds, MaxRollDeg);
}

float ATN_BeachWobblyPlatform::ClientLeadSeconds() const
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APlayerState* State = PC ? PC->PlayerState.Get() : nullptr;
	if (!State)
	{
		return 0.f;
	}
	// El ping es de ida y vuelta, en milisegundos.
	const float OneWaySeconds = State->GetPingInMilliseconds() * 0.001f * 0.5f;
	return FMath::Clamp(OneWaySeconds, 0.f, TNBeachWobblyDetail::MaxClientLeadSeconds);
}

void ATN_BeachWobblyPlatform::ApplySpec()
{
	using namespace TNBeachWobblyDetail;
	const double Fit = TNBeachTrapKit::FitRadius(Spec.Element, Spec.SizeScale);
	const uint32 Seed = TNBeachTrapKit::SeedOf(Spec.Seed, 37u);
	FCraterDims D;
	D.Ro = FMath::Max(480.0, Fit - 30.0);
	D.H = FMath::Clamp(0.27 * Fit, 180.0, 240.0);
	D.Rp = FMath::Max(170.0, D.Ro - RimWidth - D.H * 1.732);
	D.Rf = D.Rp - 0.36 * D.H;
	RimHeight = D.H;
	PitRadius = D.Rp;
	FloorRadius = D.Rf;
	OuterRadius = D.Ro;
	// Semilla par: tabla vieja; impar: tapa de nevera.
	const bool bLid = (static_cast<uint32>(Spec.Seed) & 1u) != 0u;
	BoardHalfL = D.Rp + RestOverlap;
	BoardHalfW = bLid ? FMath::Min(150.0, D.Rp * 0.8) : 110.0;
	BoardThick = bLid ? 28.0 : 24.0;
	DropAngleDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(D.H / BoardHalfL, 0.0, 0.95)));

	TNBeachTrapKit::FBuffers Crater;
	TNBeachTrapKit::FHulls Hulls;
	BuildCrater(Crater, Hulls, D, Seed);
	TNBeachTrapKit::SetMesh(CraterMesh, this, Crater, TN_ART("Beach.WobblyPlatform.Crater"));
	CraterCollision->SetCollisionConvexMeshes(Hulls);

	// Tabla entera en su bisagra central.
	BoardPivot->SetRelativeLocationAndRotation(FVector(0.0, 0.0, RimHeight), FRotator::ZeroRotator);
	TNBeachTrapKit::FBuffers Board;
	BuildBoard(Board, FTransform::Identity, BoardHalfL, BoardHalfW, BoardThick, bLid, Seed, -BoardHalfL, BoardHalfL, false);
	TNBeachTrapKit::SetMesh(BoardMesh, this, Board, TN_ART("Beach.WobblyPlatform.Board"));
	BoardBox->SetBoxExtent(FVector(BoardHalfL, BoardHalfW, BoardThick * 0.5), false);
	BoardBox->SetRelativeLocation(FVector(0.0, 0.0, BoardThick * 0.5));

	// Mitades, cada una en el espacio de su bisagra (X hacia dentro del hoyo, de 0 en la punta de fuera a BoardHalfL en el corte).
	HalfPivotA->SetRelativeLocationAndRotation(FVector(-PitRadius, 0.0, RimHeight), FRotator::ZeroRotator);
	HalfPivotB->SetRelativeLocationAndRotation(FVector(PitRadius, 0.0, RimHeight), FRotator(0.0, 180.0, 0.0));
	TNBeachTrapKit::FBuffers HalfA;
	BuildBoard(HalfA, FTransform(FVector(BoardHalfL, 0.0, 0.0)), BoardHalfL, BoardHalfW, BoardThick, bLid, Seed, -BoardHalfL, 0.0, true);
	TNBeachTrapKit::SetMesh(HalfMeshA, this, HalfA, TN_ART("Beach.WobblyPlatform.BoardHalf"));
	TNBeachTrapKit::FBuffers HalfB;
	BuildBoard(HalfB, FTransform(FRotator(0.0, 180.0, 0.0), FVector(BoardHalfL, 0.0, 0.0)), BoardHalfL, BoardHalfW, BoardThick, bLid, Seed, 0.0, BoardHalfL, true);
	TNBeachTrapKit::SetMesh(HalfMeshB, this, HalfB, TN_ART("Beach.WobblyPlatform.BoardHalf"));
	for (UBoxComponent* Half : { HalfBoxA.Get(), HalfBoxB.Get() })
	{
		Half->SetBoxExtent(FVector(BoardHalfL * 0.5, BoardHalfW, BoardThick * 0.5), false);
	}

	if (BrokenAt >= 0.f)
	{
		ApplyBroken(false);
	}
	UE_LOG(LogTortunabo, Verbose, TEXT("[Playa] Plataforma %s: hoyo de %.0f cm, cresta a %.0f, %s de %.0f x %.0f."), *GetName(), PitRadius, RimHeight,
		bLid ? TEXT("tapa") : TEXT("tabla"), 2.0 * BoardHalfL, 2.0 * BoardHalfW);
}

void ATN_BeachWobblyPlatform::BeginPlay()
{
	Super::BeginPlay();
	Voice = UTN_BeachTrapSynthComponent::AttachTo(this, GetActorTransform().TransformPosition(FVector(0.0, 0.0, RimHeight)), 600.f, 3000.f);
	Chips.Init(this, ETNTrapBurstShape::Chip, TNPlaygroundKit::Rgb(0xD9AE78), 32);
	Chips.SetMotion(-980.f, 0.6f, 22.f, 10.f, 0.5f, 1.1f);
	Dust.Init(this, ETNTrapBurstShape::Blob, TNBeachTrapKit::SandTop(), 24);
	Dust.SetMotion(-300.f, 2.f, 40.f, 12.f, 0.5f, 0.9f);
}

void ATN_BeachWobblyPlatform::PlayCreak(float Volume, float PitchMul, const FVector& WorldAt)
{
	if (Voice && Volume > 0.f)
	{
		Voice->TriggerSoundAt(ETNBeachTrapSound::Creak, WorldAt, PitchMul, Volume * CreakVolume);
	}
}

void ATN_BeachWobblyPlatform::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = Clock.Advance(GetWorld(), DeltaSeconds);
	if (BrokenAt < 0.f)
	{
		TickBoard(DeltaSeconds, Now);
	}
	else
	{
		TickHalves(Now);
	}
	Chips.Tick(DeltaSeconds);
	Dust.Tick(DeltaSeconds);
	Pop.Tick(DeltaSeconds, GetWorld());
}

void ATN_BeachWobblyPlatform::TickBoard(float DeltaSeconds, double Now)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const FTransform ActorXf = GetActorTransform();
	int32 Count = 0;
	double SumY = 0.0;
	double SumX = 0.0;
	float Kick = 0.f;
	float Motion = 0.f;
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		ACharacter* Walker = *It;
		if (!IsValid(Walker))
		{
			continue;
		}
		const FVector Local = ActorXf.InverseTransformPosition(Walker->GetActorLocation());
		const bool bNear = FMath::Abs(Local.X) < BoardHalfL + 250.0 && FMath::Abs(Local.Y) < BoardHalfW + 300.0 && Local.Z > -150.0 && Local.Z < RimHeight + 800.0;
		FRider* Rider = Riders.Find(Walker);
		if (!bNear && !Rider)
		{
			continue;
		}
		if (!Rider)
		{
			Rider = &Riders.Add(Walker);
		}
		Rider->bNear = bNear;
		const bool bOn = bNear && Walker->GetMovementBase() == BoardBox.Get();
		const UCharacterMovementComponent* Move = Walker->GetCharacterMovement();
		const float Vz = Move ? static_cast<float>(Move->Velocity.Z) : 0.f;
		const float Speed = Move ? static_cast<float>(Move->Velocity.Size2D()) : 0.f;
		if (bOn && !Rider->bOn && Rider->LastVz < -250.f)
		{
			// Aterrizaje: sacudida proporcional a la caída y crujido.
			const float Hit = FMath::Clamp((-Rider->LastVz - 250.f) / 700.f, 0.f, 1.f);
			Kick += 0.4f + Hit;
			PlayCreak(0.55f + 0.45f * Hit, FMath::FRandRange(0.85f, 1.f), Walker->GetActorLocation());
		}
		Rider->bOn = bOn;
		Rider->LastVz = Vz;
		if (bOn)
		{
			const FVector OnBoard = BoardBox->GetComponentTransform().InverseTransformPosition(Walker->GetActorLocation());
			++Count;
			SumY += FMath::Clamp(OnBoard.Y / BoardHalfW, -1.0, 1.0);
			SumX += FMath::Clamp(OnBoard.X / BoardHalfL, -1.0, 1.0);
			Motion = FMath::Max(Motion, FMath::Clamp((Speed - 100.f) / 400.f, 0.f, 1.f));
			Rider->CreakTimer -= DeltaSeconds;
			if (Speed > 150.f && Rider->CreakTimer <= 0.f)
			{
				PlayCreak(0.3f + 0.3f * Motion, FMath::FRandRange(0.9f, 1.1f), Walker->GetActorLocation());
				Rider->CreakTimer = FMath::FRandRange(0.45f, 0.8f);
			}
		}
	}
	for (auto It = Riders.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !It.Value().bNear)
		{
			It.RemoveCurrent();
		}
	}

	// Muelle poco amortiguado: se ladea hacia quien está encima, se mece al andar y los aterrizajes la sacuden. Solo en el
	// servidor, con las tortugas que ve él: la tabla es base de movimiento y tiene que estar igual en todas las máquinas.
	if (HasAuthority())
	{
		const TNWobblyPlatformNet::FRiderInput Input{ static_cast<float>(SumY), static_cast<float>(SumX), Kick, Motion };
		TNWobblyPlatformNet::StepServerPose(Pose, Input, Now, DeltaSeconds, MaxRollDeg);
	}

	// Grieta: sube con BreakRiders encima y baja despacio si se bajan. Cada máquina con las tortugas que ve: en el servidor
	// decide la rotura; en los clientes solo da crujidos, astillas y el temblor de la malla.
	if (Count >= BreakRiders)
	{
		Crack = FMath::Min(1.f, Crack + DeltaSeconds / FMath::Max(0.1f, CrackSeconds));
	}
	else
	{
		Crack = FMath::Max(0.f, Crack - DeltaSeconds * 0.45f);
	}
	const FVector Mid = BoardPivot->GetComponentLocation() + FVector(0.0, 0.0, BoardThick);
	if (Crack > 0.2f)
	{
		CrackCreakTimer -= DeltaSeconds;
		if (CrackCreakTimer <= 0.f)
		{
			PlayCreak(0.6f + 0.5f * Crack, 1.15f + 0.4f * Crack, Mid);
			CrackCreakTimer = FMath::Lerp(0.3f, 0.12f, Crack);
			if (Crack > 0.5f)
			{
				Chips.Burst(Mid, 2, FVector::UpVector, 260.f, 1.2f, 40.f);
			}
		}
	}
	if (HasAuthority())
	{
		Pose.Sag = static_cast<float>(Count * 1.5 + Crack * 6.0);
		PublishPose(Now, Kick > 0.f);
	}
	else
	{
		// Clientes: el muelle del servidor desde su última muestra. Calcularlo con las tortugas que ve este cliente (las demás
		// le llegan con retraso) dejaba la tabla en otro sitio; suavizar hacia la muestra, unos 90 ms detrás.
		FollowServerPose(DeltaSeconds);
	}
	BoardPivot->SetRelativeLocationAndRotation(FVector(0.0, 0.0, RimHeight - static_cast<double>(Pose.Sag)),
		FRotator(Pose.Pitch, 0.f, Pose.Roll));
	// Temblor de la grieta: solo en la malla. Va a 33-41 rad/s con el reloj de cada máquina: en la colisión no coincidiría.
	const float ShakeRoll = Crack * 1.6f * static_cast<float>(FMath::Sin(Now * 41.0));
	const float ShakePitch = Crack * 0.7f * static_cast<float>(FMath::Sin(Now * 33.0 + 1.0));
	BoardMesh->SetRelativeRotation(FRotator(ShakePitch, 0.f, ShakeRoll));

	if (HasAuthority() && Crack >= 1.f)
	{
		BreakBoard();
	}
}

void ATN_BeachWobblyPlatform::BreakBoard()
{
	if (BrokenAt >= 0.f)
	{
		return;
	}
	BrokenAt = static_cast<float>(TNBeachTrapKit::ServerNow(GetWorld()));
	ApplyBroken(true);
	ForceNetUpdate();
	UE_LOG(LogTortunabo, Log, TEXT("[Playa] Plataforma %s partida."), *GetName());
}

void ATN_BeachWobblyPlatform::OnRep_Broken()
{
	if (BrokenAt >= 0.f)
	{
		// Quien llega tarde (relevancia, unirse a media ronda) no oye el crujido de hace rato.
		ApplyBroken(TNBeachTrapKit::ServerNow(GetWorld()) - BrokenAt < 2.0);
	}
}

void ATN_BeachWobblyPlatform::ApplyBroken(bool bWithFX)
{
	if (bBrokenApplied)
	{
		return;
	}
	bBrokenApplied = true;
	BoardBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoardMesh->SetVisibility(false);
	HalfMeshA->SetVisibility(true);
	HalfMeshB->SetVisibility(true);
	TickHalves(Clock.Now() > 0.0 ? Clock.Now() : TNBeachTrapKit::ServerNow(GetWorld()));
	if (bWithFX && GetNetMode() != NM_DedicatedServer)
	{
		const FVector Mid = GetActorTransform().TransformPosition(FVector(0.0, 0.0, RimHeight + BoardThick));
		Chips.Burst(Mid, 18, FVector::UpVector, 600.f, 1.2f, 60.f);
		if (Voice)
		{
			Voice->TriggerSoundAt(ETNBeachTrapSound::Crack, Mid, FMath::FRandRange(0.9f, 1.05f), 1.2f);
		}
		Pop.Show(this, NSLOCTEXT("TNBeach", "PlatformCrack", "¡CRAC!"), FColor(255, 210, 90), Mid + FVector(0.0, 0.0, 130.0), 130.f);
	}
}

void ATN_BeachWobblyPlatform::TickHalves(double Now)
{
	using namespace TNBeachWobblyDetail;
	const double T = FMath::Max(0.0, Now - static_cast<double>(BrokenAt));
	// Primero resbalan hacia dentro lo que se apoyaban en la cresta; luego caen de golpe (con un botecito) hasta el fondo.
	const double Slide = TNPlaygroundKit::Smooth01(0.0, 0.12, T);
	double Fall = T < 0.12 ? 0.0 : FMath::Min(1.0, (T - 0.12) / 0.38);
	Fall *= Fall;
	double Angle = -DropAngleDeg * Fall;
	if (T > 0.5 && T < 0.78)
	{
		Angle += DropAngleDeg * 0.08 * FMath::Sin(TNPlaygroundKit::KitPi * (T - 0.5) / 0.28);
	}
	const double Offset = -RestOverlap * (1.0 - Slide);
	HalfPivotA->SetRelativeRotation(FRotator(Angle, 0.0, 0.0));
	HalfPivotB->SetRelativeRotation(FRotator(Angle, 180.0, 0.0));
	for (UStaticMeshComponent* Half : { HalfMeshA.Get(), HalfMeshB.Get() })
	{
		Half->SetRelativeLocation(FVector(Offset, 0.0, 0.0));
	}
	for (UBoxComponent* Half : { HalfBoxA.Get(), HalfBoxB.Get() })
	{
		Half->SetRelativeLocation(FVector(Offset + BoardHalfL * 0.5, 0.0, BoardThick * 0.5));
	}
	if (!bHalvesLanded && T >= 0.5)
	{
		bHalvesLanded = true;
		if (T < 2.0 && GetNetMode() != NM_DedicatedServer)
		{
			const FVector Bottom = GetActorTransform().TransformPosition(FVector(0.0, 0.0, 10.0));
			Dust.Burst(Bottom, 14, FVector::UpVector, 260.f, 1.4f, static_cast<float>(FloorRadius * 0.4));
			if (Voice)
			{
				Voice->TriggerSoundAt(ETNBeachTrapSound::Thud, Bottom, FMath::FRandRange(0.9f, 1.05f), 1.f);
			}
		}
	}
	if (!bHalvesSettled && T >= 0.8)
	{
		// Ya quietas: rampas del fondo a la cresta.
		bHalvesSettled = true;
		HalfBoxA->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		HalfBoxB->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
}
