#include "World/Beach/TN_BeachSpadeRamp.h"
#include "World/Beach/TN_BeachNearby.h"
#include "World/Beach/TN_BeachTrapSynthComponent.h"
#include "Core/TN_Log.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Math/RotationMatrix.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "TN_BeachTrapKit.h"

/**
 * Geometría de la pala (en su espacio: punta de la hoja en X = 0, mango hacia +X, cara de abajo en Z = 0) y de lo que la
 * sostiene: la piedra del fulcro del balancín, o el montículo de arena y la roca del puente con su charco en medio.
 */
namespace TNBeachSpadeDetail
{
	using FBuffers = TNBeachTrapKit::FBuffers;
	using FHulls = TNBeachTrapKit::FHulls;

	constexpr double BladeThick = 14.0;
	constexpr double ShaftWidth = 84.0;
	constexpr double ShaftThick = 34.0;
	constexpr double GripRadius = 26.0;
	constexpr double GripHalfLen = 115.0;
	/** Duración de una vuelta completa del balancín (golpe, espera y vuelta). */
	constexpr double FlipTotal = 2.2;
	constexpr double FlipSlam = 0.16;

	struct FSpadeDims
	{
		double Length = 1120.0;
		double BladeL = 400.0;
		double BladeW = 320.0;
		double NeckEnd = 480.0;
		double GripX = 1090.0;
		bool bTBar = true;
	};

	/** Hoja con esquinas redondas, rebordes bajos a los lados, cuello, mango y empuñadura (en T o redonda). */
	void BuildSpade(FBuffers& B, const FSpadeDims& Dims, uint32 Seed)
	{
		const FLinearColor BladeCol = TNPlaygroundKit::ToyColor(static_cast<int32>(Seed % 7u), 0.3f);
		const FLinearColor ShaftCol = TNPlaygroundKit::ToyColor(static_cast<int32>(Seed % 7u) + 2, 0.3f);
		const FLinearColor Light = TNPlaygroundKit::Mix(ShaftCol, TNPlaygroundKit::Rgb(0xFFFFFF, 0.3f), 0.35);
		const double HalfW = Dims.BladeW * 0.5;
		const double Corner = FMath::Min(70.0, HalfW * 0.5);

		// Hoja: contorno con las esquinas de la punta redondeadas.
		TArray<FVector2D> Outline;
		Outline.Add(FVector2D(Dims.BladeL, -HalfW));
		for (int32 i = 0; i <= 5; ++i)
		{
			const double A = FMath::DegreesToRadians(270.0 - 90.0 * i / 5.0);
			Outline.Add(FVector2D(Corner + Corner * FMath::Cos(A), -HalfW + Corner + Corner * FMath::Sin(A)));
		}
		for (int32 i = 0; i <= 5; ++i)
		{
			const double A = FMath::DegreesToRadians(180.0 - 90.0 * i / 5.0);
			Outline.Add(FVector2D(Corner + Corner * FMath::Cos(A), HalfW - Corner + Corner * FMath::Sin(A)));
		}
		Outline.Add(FVector2D(Dims.BladeL, HalfW));
		TNPlaygroundKit::AddSlab(B, FVector(0.0, 0.0, BladeThick * 0.5), FVector::ForwardVector, FVector::RightVector, FVector::UpVector, Outline, BladeThick, BladeCol);
		// Rebordes bajos a los lados de la hoja (solo dibujo) y una estría en medio.
		for (const double Side : { -1.0, 1.0 })
		{
			TNPlaygroundKit::AddAxisBox(B, FVector((Corner + Dims.BladeL) * 0.5, Side * (HalfW - 5.0), BladeThick + 6.0), FVector((Dims.BladeL - Corner) * 0.5, 5.0, 6.0),
				TNPlaygroundKit::Shade(BladeCol, 0.9));
		}
		TNPlaygroundKit::AddAxisBox(B, FVector(Dims.BladeL * 0.55, 0.0, BladeThick + 0.4), FVector(Dims.BladeL * 0.35, 4.0, 0.5), TNPlaygroundKit::Shade(BladeCol, 0.8));

		// Cuello: trapecio de la hoja al mango.
		const TArray<FVector2D> Neck = { FVector2D(Dims.BladeL - 12.0, -HalfW * 0.45), FVector2D(Dims.NeckEnd, -ShaftWidth * 0.5), FVector2D(Dims.NeckEnd, ShaftWidth * 0.5),
			FVector2D(Dims.BladeL - 12.0, HalfW * 0.45) };
		TNPlaygroundKit::AddSlab(B, FVector(0.0, 0.0, ShaftThick * 0.45), FVector::ForwardVector, FVector::RightVector, FVector::UpVector, Neck, ShaftThick * 0.9, BladeCol);

		// Mango con una franja clara por encima.
		const double ShaftEnd = Dims.GripX;
		TNPlaygroundKit::AddAxisBox(B, FVector((Dims.NeckEnd + ShaftEnd) * 0.5, 0.0, ShaftThick * 0.5), FVector((ShaftEnd - Dims.NeckEnd) * 0.5, ShaftWidth * 0.5, ShaftThick * 0.5),
			ShaftCol);
		TNPlaygroundKit::AddAxisBox(B, FVector((Dims.NeckEnd + ShaftEnd) * 0.5, 0.0, ShaftThick + 0.4), FVector((ShaftEnd - Dims.NeckEnd) * 0.5 - 10.0, ShaftWidth * 0.18, 0.5), Light);

		// Empuñadura.
		if (Dims.bTBar)
		{
			TNPlaygroundKit::AddFrustum(B, FVector(Dims.GripX, -GripHalfLen, 30.0), FVector(Dims.GripX, GripHalfLen, 30.0), GripRadius, GripRadius, 14, BladeCol,
				TNPlaygroundKit::Shade(BladeCol, 1.1), true, true);
			TNPlaygroundKit::AddFrustum(B, FVector(Dims.GripX - 40.0, 0.0, ShaftThick * 0.5), FVector(Dims.GripX, 0.0, ShaftThick * 0.5), ShaftWidth * 0.5 + 4.0,
				ShaftWidth * 0.5 + 4.0, 14, BladeCol, BladeCol, true, false);
		}
		else
		{
			TNPlaygroundKit::AddFrustum(B, FVector(Dims.GripX, 0.0, 0.0), FVector(Dims.GripX, 0.0, ShaftThick), ShaftWidth * 0.5, ShaftWidth * 0.5, 16, ShaftCol, Light, true, true);
		}
	}

	/** Cajas de colisión en el espacio de la pala (centro y semiejes). */
	void SpadeBoxes(const FSpadeDims& Dims, FVector& BladeC, FVector& BladeH, FVector& ShaftC, FVector& ShaftH, FVector& GripC, FVector& GripH)
	{
		BladeC = FVector(Dims.BladeL * 0.5, 0.0, BladeThick * 0.5);
		BladeH = FVector(Dims.BladeL * 0.5, Dims.BladeW * 0.5, BladeThick * 0.5);
		ShaftC = FVector((Dims.BladeL + Dims.GripX) * 0.5, 0.0, ShaftThick * 0.5);
		ShaftH = FVector((Dims.GripX - Dims.BladeL) * 0.5, ShaftWidth * 0.5, ShaftThick * 0.5);
		if (Dims.bTBar)
		{
			GripC = FVector(Dims.GripX, 0.0, 30.0);
			GripH = FVector(GripRadius + 2.0, GripHalfLen, GripRadius);
		}
		else
		{
			GripC = FVector(Dims.GripX, 0.0, ShaftThick * 0.5);
			GripH = FVector(ShaftWidth * 0.5, ShaftWidth * 0.5, ShaftThick * 0.5);
		}
	}

	/** Roca de caras planas con la cima plana (perfil de revolución con temblor) y su casco. */
	void AddFlatRock(FBuffers& B, FHulls& Hulls, const FVector& Base, double BaseR, double TopR, double Height, uint32 Seed)
	{
		const TArray<FVector2D> Profile = { FVector2D(0.0, Height), FVector2D(TopR, Height), FVector2D(TopR + 25.0, Height - 35.0), FVector2D(BaseR, 40.0),
			FVector2D(BaseR + 15.0, -30.0) };
		const TArray<FLinearColor> Colors = { TNBeachTrapKit::RockTone(0), TNBeachTrapKit::RockTone(2), TNBeachTrapKit::RockTone(1), TNBeachTrapKit::RockTone(0) };
		TNBeachTrapKit::AddLatheProfile(B, Base, Profile, Colors, 11, 0.06, Seed);
		Hulls.Add(TNPlaygroundKit::HullCylinder(Base - FVector(0.0, 0.0, 30.0), Height + 30.0, BaseR, TopR, 12));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// ATN_BeachSpadeRamp
// ─────────────────────────────────────────────────────────────────────────────

ATN_BeachSpadeRamp::ATN_BeachSpadeRamp()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(2.f);

	BaseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BaseMesh"));
	BaseMesh->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureVisual(BaseMesh);

	BaseCollision = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BaseCollision"));
	BaseCollision->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureSolid(BaseCollision, false);

	SpadePivot = CreateDefaultSubobject<USceneComponent>(TEXT("SpadePivot"));
	SpadePivot->SetupAttachment(GetRootComponent());
	SpadeFrame = CreateDefaultSubobject<USceneComponent>(TEXT("SpadeFrame"));
	SpadeFrame->SetupAttachment(SpadePivot);

	SpadeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SpadeMesh"));
	SpadeMesh->SetupAttachment(SpadeFrame);
	TNBeachTrapKit::ConfigureVisual(SpadeMesh);

	// Cajas de la pala: bases móviles con nombre estable por red.
	BladeBox = CreateDefaultSubobject<UBoxComponent>(TEXT("BladeBox"));
	ShaftBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ShaftBox"));
	GripBox = CreateDefaultSubobject<UBoxComponent>(TEXT("GripBox"));
	for (UBoxComponent* Box : { BladeBox.Get(), ShaftBox.Get(), GripBox.Get() })
	{
		Box->SetupAttachment(SpadeFrame);
		Box->InitBoxExtent(FVector(50.0));
		Box->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
		Box->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Box->SetGenerateOverlapEvents(false);
		Box->SetCanEverAffectNavigation(false);
		Box->SetHiddenInGame(true);
	}
}

void ATN_BeachSpadeRamp::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_BeachSpadeRamp, FlipAt);
}

void ATN_BeachSpadeRamp::ApplySpec()
{
	using namespace TNBeachSpadeDetail;
	const double Fit = TNBeachTrapKit::FitRadius(Spec.Element, Spec.SizeScale);
	const uint32 Seed = TNBeachTrapKit::SeedOf(Spec.Seed, 53u);
	bSeesaw = (static_cast<uint32>(Spec.Seed) & 1u) == 0u;

	FSpadeDims Dims;
	Dims.Length = FMath::Clamp(2.0 * Fit - 280.0, 700.0, 1150.0);
	Dims.BladeL = 0.36 * Dims.Length;
	Dims.BladeW = FMath::Clamp(0.3 * Dims.Length, 240.0, 340.0);
	Dims.NeckEnd = Dims.BladeL + 0.07 * Dims.Length;
	Dims.bTBar = bSeesaw;
	Dims.GripX = bSeesaw ? Dims.Length - 30.0 : Dims.Length - ShaftWidth * 0.5;
	SpadeLength = Dims.Length;
	BladeLength = Dims.BladeL;

	TNBeachTrapKit::FBuffers Spade;
	BuildSpade(Spade, Dims, Seed);
	TNBeachTrapKit::SetMesh(SpadeMesh, this, Spade, TN_ART("Beach.SpadeRamp.Spade"));
	FVector BladeC, BladeH, ShaftC, ShaftH, GripC, GripH;
	SpadeBoxes(Dims, BladeC, BladeH, ShaftC, ShaftH, GripC, GripH);
	BladeBox->SetBoxExtent(BladeH, false);
	BladeBox->SetRelativeLocation(BladeC);
	ShaftBox->SetBoxExtent(ShaftH, false);
	ShaftBox->SetRelativeLocation(ShaftC);
	GripBox->SetBoxExtent(GripH, false);
	GripBox->SetRelativeLocation(GripC);

	TNBeachTrapKit::FBuffers Base;
	TNBeachTrapKit::FHulls Hulls;
	if (bSeesaw)
	{
		// Piedra del fulcro y pala con la hoja en la arena y el mango en alto.
		const double FulcrumH = FMath::Clamp(0.21 * Dims.Length, 110.0, 150.0);
		FulcrumSpadeX = Dims.BladeL + 0.08 * Dims.Length;
		RestDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp((FulcrumH - 3.0) / FulcrumSpadeX, 0.0, 0.8)));
		FlippedDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp((FulcrumH + 1.0) / (Dims.GripX - FulcrumSpadeX), 0.0, 0.8)));
		const double FulcrumX = -0.5 * (Dims.Length - 2.0 * FulcrumSpadeX) * FMath::Cos(FMath::DegreesToRadians(RestDeg));
		const double RockR = FMath::Clamp(0.75 * FulcrumH, 85.0, 110.0);
		// TNProcAddBoulder: cima en -25 + 1,026·alto + 0,16·radio; se deja 5 cm por debajo del punto de apoyo.
		TNProcMesh::TNProcAddBoulder(Base, FVector(FulcrumX, 0.0, 0.0), RockR, (FulcrumH + 20.0 - 0.161 * RockR) / 1.026, Seed, TNBeachTrapKit::RockTone(1));
		Hulls.Add(TNPlaygroundKit::HullCylinder(FVector(FulcrumX, 0.0, -25.0), FulcrumH + 20.0, RockR * 0.95, RockR * 0.35, 10));
		// Arena removida donde golpean la hoja y el mango.
		for (const double End : { -1.0, 1.0 })
		{
			const double Reach = End < 0.0 ? FulcrumSpadeX * FMath::Cos(FMath::DegreesToRadians(RestDeg))
				: (Dims.GripX - FulcrumSpadeX) * FMath::Cos(FMath::DegreesToRadians(FlippedDeg));
			TNPlaygroundKit::AddDisc(Base, FVector(FulcrumX + End * Reach, 0.0, 1.0), FVector::UpVector, End < 0.0 ? Dims.BladeW * 0.5 : 120.0, 14, TNBeachTrapKit::SandWet());
		}
		SpadePivot->SetRelativeLocationAndRotation(FVector(FulcrumX, 0.0, FulcrumH), FRotator(RestDeg, 0.0, 0.0));
		SpadeFrame->SetRelativeLocationAndRotation(FVector(-FulcrumSpadeX, 0.0, 0.0), FRotator::ZeroRotator);
	}
	else
	{
		// Montículo de arena (mango) y roca alta (hoja asomando por fuera), con un charco en medio.
		const double MoundR = FMath::Clamp(0.36 * Fit, 170.0, 250.0);
		const double MoundTop = 0.36 * MoundR;
		const double MoundH = FMath::Clamp(0.4 * MoundR, 70.0, 100.0);
		const double RockBaseR = FMath::Clamp(0.24 * Fit, 120.0, 170.0);
		const double RockTopR = RockBaseR * 0.76;
		const double RockH = FMath::Clamp(0.4 * Fit, 220.0, 280.0);
		const double MoundX = -(Fit - MoundR - 20.0);
		// La pala toca la roca en el borde -X de su cima (el resto de la cima queda por debajo) y el 70 % de la hoja asoma
		// por fuera del borde +X.
		const double ContactS = FMath::Max(200.0, Dims.GripX - (0.7 * Dims.BladeL + 2.0 * RockTopR));
		const double Slope = FMath::Asin(FMath::Clamp((RockH - MoundH) / ContactS, 0.0, 0.6));
		const double ContactX = MoundX + ContactS * FMath::Cos(Slope);
		const double RockX = ContactX + 0.95 * RockTopR;

		const TArray<FVector2D> MoundProfile = { FVector2D(0.0, MoundH + 5.0), FVector2D(MoundTop, MoundH), FVector2D(MoundR, 0.0), FVector2D(MoundR + 20.0, -25.0) };
		const TArray<FLinearColor> MoundColors = { TNBeachTrapKit::SandTop(), TNBeachTrapKit::SandSide(), TNBeachTrapKit::SandSide() };
		TNBeachTrapKit::AddLatheProfile(Base, FVector(MoundX, 0.0, 0.0), MoundProfile, MoundColors, 16, 0.04, Seed);
		Hulls.Add(TNPlaygroundKit::HullCylinder(FVector(MoundX, 0.0, -25.0), MoundH + 25.0, MoundR + 20.0, MoundTop, 14));
		AddFlatRock(Base, Hulls, FVector(RockX, 0.0, 0.0), RockBaseR, RockTopR, RockH, Seed + 7u);
		const double PoolX = 0.5 * (MoundX + MoundR + RockX - RockBaseR);
		TNPlaygroundKit::AddDisc(Base, FVector(PoolX, 0.0, 1.0), FVector::UpVector, FMath::Max(60.0, 0.5 * (RockX - RockBaseR - MoundX - MoundR) + 50.0), 18,
			TNBeachTrapKit::SandWet());
		TNPlaygroundKit::AddDisc(Base, FVector(PoolX, 0.0, 2.5), FVector::UpVector, FMath::Max(40.0, 0.5 * (RockX - RockBaseR - MoundX - MoundR)), 18,
			TNPlaygroundKit::Rgb(0x6EC6E8, 0.6f));

		// La pala: el mango sobre el montículo, apoyada en la roca y la hoja hacia +X.
		const FVector Grip(MoundX, 0.0, MoundH + 2.0);
		const FVector Dir(FMath::Cos(Slope), 0.0, FMath::Sin(Slope));
		const FVector Tip = Grip + Dir * Dims.GripX;
		const FRotator FrameRot = FRotationMatrix::MakeFromXZ(-Dir, FVector::UpVector).Rotator();
		FulcrumSpadeX = Dims.GripX;
		RestDeg = 0.0;
		FlippedDeg = 0.0;
		SpadePivot->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
		SpadeFrame->SetRelativeLocationAndRotation(Tip, FrameRot);
	}
	TNBeachTrapKit::SetMesh(BaseMesh, this, Base, TN_ART("Beach.SpadeRamp.Base"));
	BaseCollision->SetCollisionConvexMeshes(Hulls);
	UE_LOG(LogTortunabo, Verbose, TEXT("[Playa] Pala %s: %s, %.0f cm."), *GetName(), bSeesaw ? TEXT("balancín") : TEXT("puente-trampolín"), SpadeLength);
}

void ATN_BeachSpadeRamp::BeginPlay()
{
	Super::BeginPlay();
	Voice = UTN_BeachTrapSynthComponent::AttachTo(this, SpadeFrame->GetComponentLocation(), 700.f, 3200.f);
	Dust.Init(this, ETNTrapBurstShape::Blob, TNBeachTrapKit::SandTop(), 24);
	Dust.SetMotion(-400.f, 2.f, 38.f, 10.f, 0.4f, 0.8f);
}

void ATN_BeachSpadeRamp::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (auto It = Tracks.CreateIterator(); It; ++It)
	{
		if (ACharacter* Rider = It.Key().Get())
		{
			Unbind(Rider, It.Value());
		}
	}
	Tracks.Reset();
	Super::EndPlay(EndPlayReason);
}

// ─────────────────────────────────────────────────────────────────────────────
// Consultas
// ─────────────────────────────────────────────────────────────────────────────

bool ATN_BeachSpadeRamp::IsOnSpade(const ACharacter* Character) const
{
	const UPrimitiveComponent* Floor = Character ? Character->GetMovementBase() : nullptr;
	return Floor && (Floor == BladeBox.Get() || Floor == ShaftBox.Get() || Floor == GripBox.Get());
}

bool ATN_BeachSpadeRamp::IsInTipZone(double SpadeX) const
{
	// Balancín: el último 20 % del mango (arriba). Puente: el 30 % de la punta de la hoja (asomando por fuera de la roca).
	return bSeesaw ? SpadeX > SpadeLength * 0.8 : (SpadeX >= -20.0 && SpadeX < BladeLength * 0.3);
}

double ATN_BeachSpadeRamp::SeesawAngle(double T) const
{
	using namespace TNBeachSpadeDetail;
	if (!bSeesaw || T < 0.0 || T >= FlipTotal)
	{
		return RestDeg;
	}
	if (T < FlipSlam)
	{
		// Golpe: acelera hasta dar en la arena.
		const double U = T / FlipSlam;
		return FMath::Lerp(RestDeg, -FlippedDeg, U * U);
	}
	if (T < 0.9)
	{
		// Botecito al golpear y quieta con el mango en la arena.
		const double Bounce = T < 0.3 ? 2.5 * FMath::Sin(TNPlaygroundKit::KitPi * (T - FlipSlam) / (0.3 - FlipSlam)) : 0.0;
		return -FlippedDeg + Bounce;
	}
	// Vuelve sola: la hoja pesa más.
	return FMath::Lerp(-FlippedDeg, RestDeg, TNPlaygroundKit::Smooth01(0.9, FlipTotal - 0.05, T));
}

bool ATN_BeachSpadeRamp::IsSeesawIdle(double ServerTime) const
{
	return !bSeesaw || FlipAt < 0.f || ServerTime - static_cast<double>(FlipAt) >= TNBeachSpadeDetail::FlipTotal;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tortugas
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachSpadeRamp::Unbind(ACharacter* Rider, FRiderTrack& Track)
{
	if (Rider && Track.bBound)
	{
		Rider->MovementModeChangedDelegate.RemoveDynamic(this, &ATN_BeachSpadeRamp::OnRiderModeChanged);
	}
	Track.bBound = false;
}

void ATN_BeachSpadeRamp::TrackRiders(double ServerTime)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double WorldNow = World->GetTimeSeconds();
	const FTransform ActorXf = GetActorTransform();
	const FTransform FrameXf = SpadeFrame->GetComponentTransform();
	const double Reach = SpadeLength * 0.5 + 500.0;
	const bool bIdle = IsSeesawIdle(ServerTime);
	bool bFlipRequested = false;
	// Solo quien está al alcance en planta; a los demás que se seguían se les suelta como a cualquiera que se aleja.
	TArray<ACharacter*> Near;
	TNBeachNearby::Gather(World, ActorXf.GetLocation(), Reach * ActorXf.GetMaximumAxisScale(), Near);
	for (auto It = Tracks.CreateIterator(); It; ++It)
	{
		ACharacter* Tracked = It.Key().Get();
		if (Tracked && !Near.Contains(Tracked))
		{
			Unbind(Tracked, It.Value());
			It.RemoveCurrent();
		}
	}
	for (ACharacter* Walker : Near)
	{
		if (!IsValid(Walker))
		{
			continue;
		}
		const FVector Local = ActorXf.InverseTransformPosition(Walker->GetActorLocation());
		const bool bNear = FVector2D(Local.X, Local.Y).Size() < Reach && Local.Z > -200.0 && Local.Z < 1200.0;
		FRiderTrack* Track = Tracks.Find(Walker);
		if (!bNear)
		{
			if (Track)
			{
				Unbind(Walker, *Track);
				Tracks.Remove(Walker);
			}
			continue;
		}
		if (!Track)
		{
			Track = &Tracks.Add(Walker);
		}
		if (!Track->bBound && TNBeachTrapKit::SimulatesMovement(Walker))
		{
			Walker->MovementModeChangedDelegate.AddUniqueDynamic(this, &ATN_BeachSpadeRamp::OnRiderModeChanged);
			Track->bBound = true;
		}
		const bool bOn = IsOnSpade(Walker);
		Track->SpadeX = bOn ? FrameXf.InverseTransformPosition(Walker->GetActorLocation()).X : -1.0;
		if (bOn && bIdle && IsInTipZone(Track->SpadeX))
		{
			Track->LastTipTime = WorldNow;
		}
		if (bOn && !Track->bOn && Track->LastVz < -300.f && GetNetMode() != NM_DedicatedServer && Voice)
		{
			Voice->TriggerSoundAt(ETNBeachTrapSound::Thud, Walker->GetActorLocation(), FMath::FRandRange(1.2f, 1.4f), 0.45f);
		}
		// Balancín: cae de un salto sobre la mitad del mango.
		if (bSeesaw && bIdle && HasAuthority() && bOn && !Track->bOn && Track->LastVz < -FlipImpactSpeed && Track->SpadeX > FulcrumSpadeX + 60.0
			&& TNBeachTrapKit::IsFreeTurtle(Walker))
		{
			bFlipRequested = true;
		}
		const UCharacterMovementComponent* Move = Walker->GetCharacterMovement();
		Track->bOn = bOn;
		Track->LastVz = Move ? static_cast<float>(Move->Velocity.Z) : 0.f;
	}
	for (auto It = Tracks.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	if (bFlipRequested)
	{
		FlipSeesaw();
	}
}

void ATN_BeachSpadeRamp::FlipSeesaw()
{
	UWorld* World = GetWorld();
	FlipAt = static_cast<float>(TNBeachTrapKit::ServerNow(World));
	bSlamPending = true;
	const FVector Fwd = GetActorForwardVector().GetSafeNormal2D();
	TArray<APawn*> Victims;
	for (auto It = Tracks.CreateIterator(); It; ++It)
	{
		ACharacter* Victim = It.Key().Get();
		const FRiderTrack& Track = It.Value();
		if (!Victim || !Track.bOn || Track.SpadeX >= FulcrumSpadeX - 40.0 || !TNBeachTrapKit::IsFreeTurtle(Victim))
		{
			continue;
		}
		Victim->LaunchCharacter(Fwd * CatapultForward + FVector::UpVector * CatapultUp, true, true);
		if (ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Victim))
		{
			Turtle->SetFallImmuneUntilLanded();
		}
		Victims.Add(Victim);
	}
	MulticastCatapult(Victims);
	ForceNetUpdate();
	UE_LOG(LogTortunabo, Verbose, TEXT("[Playa] Pala %s: vuelta del balancín, %d lanzadas."), *GetName(), Victims.Num());
}

void ATN_BeachSpadeRamp::MulticastCatapult_Implementation(const TArray<APawn*>& Victims)
{
	if (GetNetMode() != NM_Client)
	{
		return;
	}
	const FVector Fwd = GetActorForwardVector().GetSafeNormal2D();
	for (APawn* Victim : Victims)
	{
		ACharacter* Flyer = Cast<ACharacter>(Victim);
		if (!Flyer || !Flyer->IsLocallyControlled())
		{
			continue;
		}
		// El cliente de la víctima aplica el mismo lanzamiento: la corrección del servidor queda pequeña.
		Flyer->LaunchCharacter(Fwd * CatapultForward + FVector::UpVector * CatapultUp, true, true);
		if (ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Flyer))
		{
			Turtle->SetFallImmuneUntilLanded();
		}
	}
}

void ATN_BeachSpadeRamp::OnRiderModeChanged(ACharacter* Rider, EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	// Llega dentro del movimiento (servidor y cliente dueño): el lanzamiento se aplica en ese mismo paso en los dos.
	if (!Rider || !TNBeachTrapKit::SimulatesMovement(Rider) || PrevMovementMode != MOVE_Walking)
	{
		return;
	}
	UCharacterMovementComponent* Move = Rider->GetCharacterMovement();
	FRiderTrack* Track = Tracks.Find(Rider);
	UWorld* World = GetWorld();
	if (!Move || !Track || !World || !Move->IsFalling() || Move->Velocity.Z < 100.0)
	{
		return;
	}
	if (World->GetTimeSeconds() - Track->LastTipTime > 0.25 || !TNBeachTrapKit::IsFreeTurtle(Rider))
	{
		return;
	}
	Track->LastTipTime = -10.0;
	const FVector Fwd = GetActorForwardVector().GetSafeNormal2D();
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Fwd);
	const FVector Lateral = Side * (FVector::DotProduct(Move->Velocity, Side) * 0.5);
	Rider->LaunchCharacter(Fwd * TipForward + Lateral + FVector::UpVector * TipUp, true, true);
	if (ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Rider))
	{
		Turtle->SetFallImmuneUntilLanded();
	}
	if (GetNetMode() != NM_Client)
	{
		MulticastTipFX(Rider);
		ForceNetUpdate();
	}
	else if (Rider->IsLocallyControlled())
	{
		PlayTipFX(Rider->GetActorLocation());
	}
}

void ATN_BeachSpadeRamp::MulticastTipFX_Implementation(APawn* Jumper)
{
	if (GetNetMode() == NM_Client && Jumper && Jumper->IsLocallyControlled())
	{
		return;
	}
	PlayTipFX(Jumper ? Jumper->GetActorLocation() : SpadeFrame->GetComponentLocation());
}

void ATN_BeachSpadeRamp::PlayTipFX(const FVector& WorldAt)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (Voice)
	{
		Voice->TriggerSoundAt(ETNBeachTrapSound::Twang, WorldAt, FMath::FRandRange(0.95f, 1.15f), 1.f);
	}
	Dust.Burst(WorldAt - FVector(0.0, 0.0, 60.0), 5, FVector::UpVector, 200.f, 1.2f, 30.f);
}

void ATN_BeachSpadeRamp::OnRep_Flip()
{
	if (FlipAt >= 0.f && TNBeachTrapKit::ServerNow(GetWorld()) - FlipAt < 1.0)
	{
		bSlamPending = true;
	}
}

void ATN_BeachSpadeRamp::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = Clock.Advance(GetWorld(), DeltaSeconds);
	if (bSeesaw)
	{
		const double T = FlipAt >= 0.f ? Now - static_cast<double>(FlipAt) : -1.0;
		SpadePivot->SetRelativeRotation(FRotator(SeesawAngle(T), 0.0, 0.0));
		if (bSlamPending && T >= TNBeachSpadeDetail::FlipSlam)
		{
			bSlamPending = false;
			if (GetNetMode() != NM_DedicatedServer)
			{
				const FVector GripAt = SpadeFrame->GetComponentTransform().TransformPosition(FVector(SpadeLength - 40.0, 0.0, 0.0));
				const FVector BladeAt = SpadeFrame->GetComponentTransform().TransformPosition(FVector(BladeLength * 0.5, 0.0, 20.0));
				Dust.Burst(GripAt, 12, FVector::UpVector, 320.f, 1.4f, 60.f);
				if (Voice)
				{
					Voice->TriggerSoundAt(ETNBeachTrapSound::Thud, GripAt, FMath::FRandRange(0.8f, 0.95f), 1.2f);
					Voice->TriggerSoundAt(ETNBeachTrapSound::Twang, BladeAt, FMath::FRandRange(0.75f, 0.85f), 0.9f);
				}
			}
		}
	}
	TrackRiders(TNBeachTrapKit::ServerNow(GetWorld()));
	Dust.Tick(DeltaSeconds);
}
