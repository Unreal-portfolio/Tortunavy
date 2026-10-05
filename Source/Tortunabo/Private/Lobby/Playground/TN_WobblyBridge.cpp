#include "Lobby/Playground/TN_WobblyBridge.h"
#include "Lobby/Playground/TN_PlaygroundSynthComponent.h"
#include "Art/TN_Art.h"
#include "Core/TN_Log.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Math/RotationMatrix.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "TN_PlaygroundMeshKit.h"

/**
 * Medidas, vaivén y geometría del puente. El tablero se describe en función de S (0..1 de un extremo a otro): la cara
 * de arriba cuelga en parábola (Sag) y se mueve con tres modos (vaivén lateral con su balanceo, rebote vertical y una
 * onda que viaja a lo largo), todos multiplicados por la agitación y por sin(pi*S), que los anula en los extremos.
 */
namespace TNWobblyBridgeDetail
{
	using FBuffers = TNPlaygroundKit::FBuffers;

	constexpr double PlankThick = 7.0;
	constexpr double PlankGap = 7.0;
	constexpr double TargetPitch = 32.0;
	/** Media altura de las cajas de colisión: más gruesas que el tablón (que no se cuele nadie), con la cara de arriba igual. */
	constexpr double BoxHalfZ = 6.0;
	/** Pasamanos sobre el centro de cada tablón. */
	constexpr double RailHeight = 86.0;
	constexpr double PostRadius = 8.5;
	constexpr double PostAbove = 110.0;
	constexpr double PostInset = 12.0;
	constexpr double TowerDepth = 140.0;
	constexpr double TowerSide = 30.0;
	constexpr double StepRun = 45.0;
	constexpr double MaxStepRise = 40.0;
	/** Amplitudes con agitación 1 y Wobble 1 (la agitación llega a ~2,4 con varias tortugas corriendo y saltando). */
	constexpr double SwayAmp = 24.0;
	constexpr double BounceAmp = 9.0;
	constexpr double RippleAmp = 6.0;
	constexpr double RollAmpDeg = 22.0;
	constexpr double MaxRollDeg = 58.0;
	constexpr double DipDepth = 13.0;
	constexpr double DipWidth = 85.0;
	constexpr double SwayHz = 0.52;
	constexpr double BounceHz = 0.9;
	constexpr double RippleHz = 0.75;
	using TNWobblyBridgeNet::MaxExcitation;
	/** Rapidez (1/s) con que un cliente sigue la agitación del servidor entre actualizaciones (llegan a 10 Hz). */
	constexpr float ClientExcitationInterpSpeed = 12.f;

	const uint32 WoodHex[3] = { 0xC8925A, 0xB98049, 0xD6A36B };

	/** Mezcla de bits entera (elige tablones de forma igual en todas las máquinas). */
	uint32 MixBits(uint32 X)
	{
		X ^= X >> 16;
		X *= 0x7feb352dU;
		X ^= X >> 15;
		X *= 0x846ca68bU;
		X ^= X >> 16;
		return X;
	}

	/** Medidas que necesitan los constructores de malla. */
	struct FBridgeDims
	{
		double Span = 900.0;
		double Width = 110.0;
		double Height = 220.0;
		double Depth = 25.0;
		bool bTowers = true;
		bool bSteps = true;
	};

	/** Anclajes de las cuerdas en los postes (espacio del actor): End = -1 o +1, Side = -1 o +1. */
	FVector FootAnchor(const FBridgeDims& D, double End, double Side)
	{
		return FVector(End * (D.Span * 0.5 + PostInset), Side * (D.Width * 0.5 + PostInset - 2.0), D.Height - 4.0);
	}

	FVector RailAnchor(const FBridgeDims& D, double End, double Side)
	{
		return FVector(End * (D.Span * 0.5 + PostInset), Side * (D.Width * 0.5 + PostInset - 2.0), D.Height + RailHeight + 2.0);
	}

	/** Bloque de arena con la tapa más clara, marcas de cubo en los lados y colisión. */
	void AddSandBlock(FBuffers& B, TArray<TArray<FVector>>& Hulls, const FVector& Center, const FVector& Half)
	{
		const FLinearColor SandSide = TNPlaygroundKit::Rgb(0xE8C889);
		const FLinearColor SandTop = TNPlaygroundKit::Rgb(0xF6E0AE);
		const FLinearColor SandMark = TNPlaygroundKit::Rgb(0xD2AC6A);
		TNPlaygroundKit::AddAxisBox(B, Center, Half, SandSide);
		TNPlaygroundKit::AddAxisBox(B, Center + FVector(0.0, 0.0, Half.Z + 1.2), FVector(Half.X - 6.0, Half.Y - 6.0, 1.2), SandTop);
		for (double Z = 55.0; Z < Half.Z * 2.0 - 25.0; Z += 70.0)
		{
			TNPlaygroundKit::AddAxisBox(B, FVector(Center.X, Center.Y, Center.Z - Half.Z + Z), FVector(Half.X + 3.0, Half.Y + 3.0, 5.0), SandMark);
		}
		Hulls.Add(TNPlaygroundKit::HullAxisBox(Center, Half));
	}

	/** Postes, torres de arena, escaleras y adornos (estáticos) con su colisión convexa. */
	void BuildFrame(FBuffers& B, TArray<TArray<FVector>>& Hulls, const FBridgeDims& D)
	{
		const double HalfSpan = D.Span * 0.5;
		const double HalfW = D.Width * 0.5;
		const FLinearColor PostWood = TNPlaygroundKit::Rgb(0x9C6A3C);
		const FLinearColor PostCap = TNPlaygroundKit::Rgb(0x7A4E2B);
		const FLinearColor RopeTone = TNPlaygroundKit::Rgb(0xE9D2A2);
		const FLinearColor ShellPink = TNPlaygroundKit::Rgb(0xFFB4A2, 0.2f);
		const FLinearColor StarCoral = TNPlaygroundKit::Rgb(0xFF8A70);

		for (const double End : { -1.0, 1.0 })
		{
			const double FootZ = D.bTowers ? D.Height : 0.0;
			if (D.bTowers)
			{
				// Torre de arena bajo el extremo: la cima, a ras del tablero.
				const double TowerHalfY = HalfW + TowerSide;
				const FVector TowerCenter(End * (HalfSpan + TowerDepth * 0.5), 0.0, D.Height * 0.5);
				AddSandBlock(B, Hulls, TowerCenter, FVector(TowerDepth * 0.5, TowerHalfY, D.Height * 0.5));
				// Almenas en los dos costados (fuera del paso del tablero).
				for (const double SideY : { -1.0, 1.0 })
				{
					for (const double Along : { -0.25, 0.25 })
					{
						const FVector Merlon(TowerCenter.X + Along * TowerDepth, SideY * (TowerHalfY - 12.0), D.Height + 14.0);
						const FVector MerlonHalf(15.0, 11.0, 14.0);
						TNPlaygroundKit::AddAxisBox(B, Merlon, MerlonHalf, TNPlaygroundKit::Rgb(0xEFD29A));
						Hulls.Add(TNPlaygroundKit::HullAxisBox(Merlon, MerlonHalf));
					}
					// Estrella de mar en el costado de la torre.
					TNPlaygroundKit::AddStarfish(B, FVector(TowerCenter.X, SideY * (TowerHalfY + 0.5), D.Height * 0.55), FVector(0.0, SideY, 0.0),
						FVector(0.0, 0.0, 1.0), 20.0, 3.0, StarCoral);
				}
				// Concha en la cara de fuera (encima de la escalera o, sin ella, en la pared).
				TNPlaygroundKit::AddShellFan(B, FVector(End * (HalfSpan + TowerDepth + 0.5), 0.0, D.Height - 34.0), FVector(End, 0.0, 0.0),
					FVector::UpVector, 24.0, ShellPink);

				if (D.bSteps)
				{
					// Peldaños de arena hacia fuera: el último, en el suelo.
					const int32 Count = FMath::Max(2, FMath::CeilToInt32(D.Height / MaxStepRise));
					const double Rise = D.Height / Count;
					for (int32 k = 0; k + 1 < Count; ++k)
					{
						const double Top = D.Height - (k + 1) * Rise;
						const double X0 = HalfSpan + TowerDepth + k * StepRun;
						AddSandBlock(B, Hulls, FVector(End * (X0 + StepRun * 0.5), 0.0, Top * 0.5), FVector(StepRun * 0.5, HalfW + 5.0, Top * 0.5));
					}
				}
			}

			// Cuatro postes (dos por extremo) con cuerda enrollada en los anclajes, bola de remate y banderín.
			for (const double SideY : { -1.0, 1.0 })
			{
				const FVector Foot(End * (HalfSpan + PostInset), SideY * (HalfW + PostInset), FootZ);
				const FVector Head(Foot.X, Foot.Y, D.Height + PostAbove);
				TNPlaygroundKit::AddFrustum(B, Foot, Head, PostRadius, PostRadius * 0.88, 12, PostWood, PostCap, false, true);
				for (const double WrapZ : { D.Height - 4.0, D.Height + RailHeight + 2.0 })
				{
					TNPlaygroundKit::AddFrustum(B, FVector(Foot.X, Foot.Y, WrapZ - 5.0), FVector(Foot.X, Foot.Y, WrapZ + 5.0), PostRadius + 2.2, PostRadius + 2.2,
						12, RopeTone, RopeTone, true, true);
				}
				const int32 CapTint = (End > 0.0 ? 1 : 0) + (SideY > 0.0 ? 2 : 0);
				TNPlaygroundKit::AddBall(B, Head + FVector(0.0, 0.0, 8.0), 11.0, 12, TNPlaygroundKit::ToyColor(CapTint, 0.15f));
				TNPlaygroundKit::AddRod(B, Head + FVector(0.0, 0.0, 16.0), Head + FVector(0.0, 0.0, 70.0), 1.6, 5, PostCap, FVector::ForwardVector);
				TNPlaygroundKit::AddPennant(B, Head + FVector(0.0, 0.0, 68.0), FVector(End, 0.0, 0.0), 42.0, 26.0, TNPlaygroundKit::ToyColor(CapTint + 3));
				Hulls.Add(TNPlaygroundKit::HullCylinder(Foot, Head.Z - Foot.Z, PostRadius, PostRadius, 8));
			}
		}
	}

	/** Tablón en su espacio: tabla con dos vetas, dos clavos por lado y, si está roto, astillas en el lado que falta. */
	void AddPlank(FBuffers& B, const FTransform& Xf, const FTNBridgePlank& Info, double Depth, int32 Index)
	{
		const FLinearColor Wood = TNPlaygroundKit::Shade(TNPlaygroundKit::Rgb(WoodHex[Index % 3]), Info.Tone);
		const FLinearColor Grain = TNPlaygroundKit::Shade(Wood, 0.8);
		const FLinearColor Nail = TNPlaygroundKit::Rgb(0x4A4F5A, 0.5f);
		const double Ht = PlankThick * 0.5;
		TNPlaygroundKit::AddXfBox(B, Xf, FVector(0.0, Info.CenterY, 0.0), FVector(Depth * 0.5, Info.HalfLength, Ht), Wood);
		const double Y0 = Info.CenterY - Info.HalfLength + 4.0;
		const double Y1 = Info.CenterY + Info.HalfLength - 4.0;
		const FVector Up = Xf.TransformVectorNoScale(FVector::UpVector);
		for (const double Line : { -0.26, 0.3 })
		{
			const double X = Line * Depth;
			B.AddQuad(Xf.TransformPosition(FVector(X - 0.6, Y0, Ht + 0.15)), Xf.TransformPosition(FVector(X + 0.6, Y0, Ht + 0.15)),
				Xf.TransformPosition(FVector(X + 0.6, Y1, Ht + 0.15)), Xf.TransformPosition(FVector(X - 0.6, Y1, Ht + 0.15)), Up, Grain);
		}
		for (const double NailY : { Y0 + 3.0, Y1 - 3.0 })
		{
			if (Info.bBroken && (NailY - Info.CenterY) * Info.BrokenSide > 0.0)
			{
				continue;
			}
			for (const double NailX : { -0.25 * Depth, 0.25 * Depth })
			{
				B.AddQuad(Xf.TransformPosition(FVector(NailX - 1.3, NailY - 1.3, Ht + 0.2)), Xf.TransformPosition(FVector(NailX + 1.3, NailY - 1.3, Ht + 0.2)),
					Xf.TransformPosition(FVector(NailX + 1.3, NailY + 1.3, Ht + 0.2)), Xf.TransformPosition(FVector(NailX - 1.3, NailY + 1.3, Ht + 0.2)), Up, Nail);
			}
		}
		if (Info.bBroken)
		{
			// Astillas de madera clara (recién rota) de largos distintos.
			const FLinearColor Fresh = TNPlaygroundKit::Rgb(0xE8C28C);
			const double BreakY = Info.CenterY + Info.BrokenSide * Info.HalfLength;
			const double Lengths[3] = { 6.0, 14.0, 3.0 };
			for (int32 s = 0; s < 3; ++s)
			{
				const double X = (-1.0 + (2.0 * s + 1.0) / 3.0) * Depth * 0.5;
				const double Len = Lengths[s];
				TNPlaygroundKit::AddXfBox(B, Xf, FVector(X, BreakY + Info.BrokenSide * Len * 0.5, -0.8), FVector(Depth / 6.0 - 0.6, Len * 0.5, Ht - 1.2), Fresh);
			}
		}
	}

	/** Tablones y cuerdas (pies, pasamanos y péndolas) para una pose; misma topología con la misma configuración. */
	void BuildDeck(FBuffers& B, const TArray<FTransform>& Pose, const TArray<FTNBridgePlank>& PlankInfo, const FBridgeDims& D)
	{
		const int32 Count = FMath::Min(Pose.Num(), PlankInfo.Num());
		for (int32 i = 0; i < Count; ++i)
		{
			if (!PlankInfo[i].bMissing)
			{
				AddPlank(B, Pose[i], PlankInfo[i], D.Depth, i);
			}
		}
		const double HalfW = D.Width * 0.5;
		const FLinearColor RopeA = TNPlaygroundKit::Rgb(0xE9D2A2);
		const FLinearColor RopeB = TNPlaygroundKit::Rgb(0xCDAE7B);
		for (const double Side : { -1.0, 1.0 })
		{
			TArray<FVector> Foot;
			TArray<FVector> Rail;
			Foot.Reserve(Count + 2);
			Rail.Reserve(Count + 2);
			Foot.Add(FootAnchor(D, -1.0, Side));
			Rail.Add(RailAnchor(D, -1.0, Side));
			for (int32 i = 0; i < Count; ++i)
			{
				Foot.Add(Pose[i].TransformPosition(FVector(0.0, Side * (HalfW - 8.0), -PlankThick * 0.5 - 2.5)));
				Rail.Add(Pose[i].TransformPosition(FVector(0.0, Side * (HalfW + 5.0), RailHeight)));
			}
			Foot.Add(FootAnchor(D, 1.0, Side));
			Rail.Add(RailAnchor(D, 1.0, Side));
			// Trenzado: el color cambia en cada nudo.
			TArray<FLinearColor> Braid;
			Braid.Reserve(Rail.Num());
			for (int32 i = 0; i < Rail.Num(); ++i)
			{
				Braid.Add((i % 2 == 0) ? RopeA : RopeB);
			}
			const TArray<double> FootRadii = { 2.6 };
			const TArray<double> RailRadii = { 3.0 };
			TNPlaygroundKit::AddTube(B, Foot, FootRadii, 5, Braid, FVector::UpVector, false);
			TNPlaygroundKit::AddTube(B, Rail, RailRadii, 5, Braid, FVector::UpVector, false);
			// Péndolas: del canto del tablón al pasamanos, cada dos tablones.
			for (int32 i = 0; i < Count; i += 2)
			{
				if (PlankInfo[i].bMissing)
				{
					continue;
				}
				const FVector Low = Pose[i].TransformPosition(FVector(0.0, Side * (HalfW + 1.0), PlankThick * 0.5));
				const FVector High = Pose[i].TransformPosition(FVector(0.0, Side * (HalfW + 5.0), RailHeight));
				TNPlaygroundKit::AddRod(B, Low, High, 1.5, 4, RopeB, FVector::ForwardVector);
			}
		}
	}

	// ── Arte (Docs/Arte_Assets.md) ───────────────────────────────────────────
	// La pieza de arte del marco se modela con las medidas por defecto de ATN_WobblyBridge y se estira a las de cada copia.
	constexpr double RefSpanLength = 900.0;
	constexpr double RefDeckWidth = 110.0;
	constexpr double RefDeckHeight = 220.0;

	/** Pieza de arte del marco según los extremos: con torres y escaleras, con torres sin escaleras o solo postes. */
	FName FrameArtSlot(const FBridgeDims& D)
	{
		if (!D.bTowers)
		{
			return TN_ART("Lobby.Playground.Bridge.FramePosts");
		}
		return D.bSteps ? TN_ART("Lobby.Playground.Bridge.Frame") : TN_ART("Lobby.Playground.Bridge.FrameNoStairs");
	}

	/** Pitch de los tablones (profundidad) para el largo y el número de tablones. */
	double DepthFor(double Span, int32 Count)
	{
		return FMath::Max(12.0, Span / FMath::Max(1, Count) - PlankGap);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// ATN_WobblyBridge
// ─────────────────────────────────────────────────────────────────────────────

ATN_WobblyBridge::ATN_WobblyBridge()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	// La agitación (un byte) sale hasta 10 veces por segundo, solo cuando cambia; la configuración casi nunca cambia.
	SetNetUpdateFrequency(10.f);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	FrameMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrameMesh"));
	FrameMesh->SetupAttachment(SceneRoot);
	FrameMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FrameMesh->SetCanEverAffectNavigation(false);

	DeckPreview = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DeckPreview"));
	DeckPreview->SetupAttachment(SceneRoot);
	DeckPreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DeckPreview->SetCanEverAffectNavigation(false);

	// Colisión simple (convexa) de lo que no se mueve; bloquea también la cámara, como el castillo.
	FrameCollision = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FrameCollision"));
	FrameCollision->SetupAttachment(SceneRoot);
	FrameCollision->bUseComplexAsSimpleCollision = false;
	FrameCollision->bUseAsyncCooking = false;
	FrameCollision->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);

	// Cajas de los tablones: subobjetos por defecto (con nombre estable por red: el cliente puede enviar su posición
	// relativa al tablón que pisa). Se esconden y se apagan las que no se usan.
	PlankBoxes.Reserve(MaxPlanks);
	for (int32 i = 0; i < MaxPlanks; ++i)
	{
		UBoxComponent* Slat = CreateDefaultSubobject<UBoxComponent>(*FString::Printf(TEXT("Plank%02d"), i));
		Slat->SetupAttachment(SceneRoot);
		Slat->InitBoxExtent(FVector(13.0, 55.0, TNWobblyBridgeDetail::BoxHalfZ));
		Slat->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
		Slat->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Slat->SetGenerateOverlapEvents(false);
		Slat->SetCanEverAffectNavigation(false);
		Slat->SetHiddenInGame(true);
		Slat->SetVisibility(false);
		PlankBoxes.Add(Slat);
	}
}

void ATN_WobblyBridge::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_WobblyBridge, SpanLength);
	DOREPLIFETIME(ATN_WobblyBridge, DeckHeight);
	DOREPLIFETIME(ATN_WobblyBridge, DeckWidth);
	DOREPLIFETIME(ATN_WobblyBridge, Sag);
	DOREPLIFETIME(ATN_WobblyBridge, Wobble);
	DOREPLIFETIME(ATN_WobblyBridge, IdleWobble);
	DOREPLIFETIME(ATN_WobblyBridge, RunBoost);
	DOREPLIFETIME(ATN_WobblyBridge, MissingPlanks);
	DOREPLIFETIME(ATN_WobblyBridge, BrokenPlanks);
	DOREPLIFETIME(ATN_WobblyBridge, DamageSeed);
	DOREPLIFETIME(ATN_WobblyBridge, bSandTowers);
	DOREPLIFETIME(ATN_WobblyBridge, bStairs);
	DOREPLIFETIME(ATN_WobblyBridge, NetExcitation);
}

void ATN_WobblyBridge::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	BuildAll(false);
}

void ATN_WobblyBridge::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		NetExcitation = TNWobblyBridgeNet::QuantizeExcitation(Excitation);
	}
	BuildAll(false);
	BuildRuntimeDeck();
	if (GetNetMode() != NM_DedicatedServer)
	{
		Voice = UTN_PlaygroundSynthComponent::AttachTo(this, GetActorTransform().TransformPosition(FVector(0.0, 0.0, DeckHeight - Sag)),
			static_cast<float>(SpanLength * 0.5), 2600.f);
	}
	UE_LOG(LogTortunabo, Verbose, TEXT("[Parque] Puente %s listo: %d tablones en %.0f cm a %.0f cm de alto."), *GetName(), NumPlanks, SpanLength, DeckHeight);
}

void ATN_WobblyBridge::OnRep_Config()
{
	BuildAll(false);
}

void ATN_WobblyBridge::RebuildBridge()
{
	BuildAll(true);
}

float ATN_WobblyBridge::GetTotalLength() const
{
	double Total = SpanLength + 2.0 * (TNWobblyBridgeDetail::PostInset + TNWobblyBridgeDetail::PostRadius);
	if (bSandTowers)
	{
		Total = SpanLength + 2.0 * TNWobblyBridgeDetail::TowerDepth;
		if (bStairs)
		{
			const int32 Count = FMath::Max(2, FMath::CeilToInt32(DeckHeight / TNWobblyBridgeDetail::MaxStepRise));
			Total += 2.0 * (Count - 1) * TNWobblyBridgeDetail::StepRun;
		}
	}
	return static_cast<float>(Total);
}

FVector ATN_WobblyBridge::GetDeckEnd(bool bStart) const
{
	return FVector((bStart ? -0.5 : 0.5) * SpanLength, 0.0, DeckHeight);
}

uint32 ATN_WobblyBridge::ConfigHash() const
{
	uint32 Acc = GetTypeHash(SpanLength);
	Acc = HashCombine(Acc, GetTypeHash(DeckHeight));
	Acc = HashCombine(Acc, GetTypeHash(DeckWidth));
	Acc = HashCombine(Acc, GetTypeHash(Sag));
	Acc = HashCombine(Acc, GetTypeHash(MissingPlanks));
	Acc = HashCombine(Acc, GetTypeHash(BrokenPlanks));
	Acc = HashCombine(Acc, GetTypeHash(DamageSeed));
	Acc = HashCombine(Acc, GetTypeHash(bSandTowers));
	Acc = HashCombine(Acc, GetTypeHash(bStairs));
	return Acc;
}

void ATN_WobblyBridge::LayoutPlanks()
{
	using namespace TNWobblyBridgeDetail;
	NumPlanks = FMath::Clamp(FMath::RoundToInt32(SpanLength / TargetPitch), 10, MaxPlanks);
	PlankDepth = DepthFor(SpanLength, NumPlanks);
	const uint32 Seed = static_cast<uint32>(DamageSeed) * 0x9E3779B9u + 0x2545F491u;
	Planks.SetNum(NumPlanks);
	for (int32 i = 0; i < NumPlanks; ++i)
	{
		FTNBridgePlank& Info = Planks[i];
		Info = FTNBridgePlank();
		Info.HalfLength = DeckWidth * 0.5 * (0.97 + 0.05 * TNPlaygroundKit::Hash01(i, 1, Seed));
		Info.CenterY = 0.0;
		Info.Tone = static_cast<float>(0.9 + 0.2 * TNPlaygroundKit::Hash01(i, 2, Seed));
		Info.YawJitter = 3.2 * (TNPlaygroundKit::Hash01(i, 3, Seed) - 0.5);
		Info.LiftJitter = 1.2 * (TNPlaygroundKit::Hash01(i, 4, Seed) - 0.5);
	}
	// Huecos: lejos de los extremos y nunca dos seguidos (la tortuga, de 80 cm de ancho, no cabe por uno solo).
	const int32 Usable = NumPlanks - 6;
	int32 Placed = 0;
	for (int32 Attempt = 0; Attempt < 200 && Placed < MissingPlanks && Usable > 0; ++Attempt)
	{
		const int32 Idx = 3 + static_cast<int32>(MixBits(Seed + 0x51u * Attempt) % static_cast<uint32>(Usable));
		if (Planks[Idx].bMissing || Planks[Idx - 1].bMissing || Planks[Idx + 1].bMissing)
		{
			continue;
		}
		Planks[Idx].bMissing = true;
		++Placed;
	}
	// Rotos: queda algo más de la mitad, pegada a un lado.
	Placed = 0;
	for (int32 Attempt = 0; Attempt < 200 && Placed < BrokenPlanks && Usable > 0; ++Attempt)
	{
		const int32 Idx = 3 + static_cast<int32>(MixBits(Seed + 0x9Du * Attempt + 7u) % static_cast<uint32>(Usable));
		FTNBridgePlank& Info = Planks[Idx];
		if (Info.bMissing || Info.bBroken)
		{
			continue;
		}
		Info.bBroken = true;
		Info.BrokenSide = (MixBits(Seed + Idx) & 1u) ? 1.0 : -1.0;
		const double Full = Info.HalfLength;
		Info.HalfLength = Full * 0.6;
		Info.CenterY = -Info.BrokenSide * (Full - Info.HalfLength);
		++Placed;
	}
}

FVector ATN_WobblyBridge::DeckPoint(double S, double WobbleTime, double Agitation, bool bDips) const
{
	using namespace TNWobblyBridgeDetail;
	const double X = (S - 0.5) * SpanLength;
	const double U = FMath::Sin(TNPlaygroundKit::KitPi * S);
	const double E = Agitation * Wobble;
	// Vaivén lateral (modo principal, máximo en el centro, y un segundo modo en «S» más rápido).
	const double Swing = TNPlaygroundKit::KitTwoPi * SwayHz * WobbleTime;
	const double Y = SwayAmp * E * (U * FMath::Sin(Swing + 0.6) + 0.35 * FMath::Sin(TNPlaygroundKit::KitTwoPi * S) * FMath::Sin(1.7 * Swing + 2.1));
	double Z = DeckHeight - Sag * 4.0 * S * (1.0 - S);
	Z += BounceAmp * E * U * FMath::Sin(TNPlaygroundKit::KitTwoPi * BounceHz * WobbleTime + 1.3);
	Z += RippleAmp * E * U * FMath::Sin(TNPlaygroundKit::KitTwoPi * (1.3 * S - RippleHz * WobbleTime));
	if (bDips)
	{
		// Cada tortuga hunde los tablones que pisa (más en el centro que junto a los postes).
		const double Depth = DipDepth * FMath::Clamp(static_cast<double>(Wobble), 0.5, 1.5) * (0.3 + 0.7 * U);
		for (const FVector2D& Dip : Dips)
		{
			Z -= Depth * Dip.Y * FMath::Exp(-FMath::Square((X - Dip.X) / DipWidth));
		}
	}
	return FVector(X, Y, Z);
}

double ATN_WobblyBridge::DeckRollDeg(double S, double WobbleTime, double Agitation) const
{
	using namespace TNWobblyBridgeDetail;
	const double U = FMath::Sin(TNPlaygroundKit::KitPi * S);
	const double E = Agitation * Wobble;
	const double Swing = TNPlaygroundKit::KitTwoPi * SwayHz * WobbleTime;
	// El tablero se ladea con el vaivén (casi en fase, como un péndulo colgado de los pasamanos).
	const double Roll = RollAmpDeg * E * (U * FMath::Sin(Swing + 0.3) + 0.3 * FMath::Sin(TNPlaygroundKit::KitTwoPi * S) * FMath::Sin(1.7 * Swing + 1.8));
	return FMath::Clamp(Roll, -MaxRollDeg, MaxRollDeg);
}

FTransform ATN_WobblyBridge::PlankTransform(int32 Index, double WobbleTime, double Agitation, bool bDips) const
{
	using namespace TNWobblyBridgeDetail;
	const int32 Count = FMath::Max(1, NumPlanks);
	const double S = (Index + 0.5) / Count;
	const double Half = 0.5 / Count;
	const FVector Top = DeckPoint(S, WobbleTime, Agitation, bDips);
	FVector Dir = DeckPoint(FMath::Min(1.0, S + Half), WobbleTime, Agitation, bDips) - DeckPoint(FMath::Max(0.0, S - Half), WobbleTime, Agitation, bDips);
	if (!Dir.Normalize())
	{
		Dir = FVector::ForwardVector;
	}
	const FTNBridgePlank* Info = Planks.IsValidIndex(Index) ? &Planks[Index] : nullptr;
	const double Yaw = Info ? Info->YawJitter : 0.0;
	const double Lift = Info ? Info->LiftJitter : 0.0;
	const FQuat Along = FRotationMatrix::MakeFromXZ(Dir, FVector::UpVector).ToQuat();
	const FQuat Q = Along * FQuat(FVector::UpVector, FMath::DegreesToRadians(Yaw)) * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(DeckRollDeg(S, WobbleTime, Agitation)));
	return FTransform(Q, Top + FVector(0.0, 0.0, Lift) - Q.GetUpVector() * (PlankThick * 0.5));
}

void ATN_WobblyBridge::BuildAll(bool bForce)
{
	using namespace TNWobblyBridgeDetail;
	const uint32 NewHash = ConfigHash();
	if (!bForce && NewHash == BuiltHash && FrameMesh->GetStaticMesh())
	{
		return;
	}
	BuiltHash = NewHash;

	LayoutPlanks();
	FBridgeDims Dims;
	Dims.Span = SpanLength;
	Dims.Width = DeckWidth;
	Dims.Height = DeckHeight;
	Dims.Depth = PlankDepth;
	Dims.bTowers = bSandTowers;
	Dims.bSteps = bSandTowers && bStairs;
	UMaterialInterface* Mat = TNPlaygroundKit::VertexColorMaterial();

	// Marco estático y su colisión convexa.
	FBuffers Frame;
	TArray<TArray<FVector>> Hulls;
	BuildFrame(Frame, Hulls, Dims);
	// Marco: pieza de arte. El tablero con las cuerdas se deforma en cada fotograma y se queda como está.
	TNArt::SetMesh(FrameMesh, TNPlaygroundKit::BuildMesh(this, Frame, Mat), FrameArtSlot(Dims));
	TNPlaygroundKit::ScaleArt(FrameMesh, FVector(SpanLength / RefSpanLength, DeckWidth / RefDeckWidth, DeckHeight / RefDeckHeight));
	FrameCollision->SetCollisionConvexMeshes(Hulls);

	// Pose de reposo, cajas de los tablones (medidas y sitio) y velocidades a cero.
	PlankPose.SetNum(NumPlanks);
	VisualPose.SetNum(NumPlanks);
	PrevPlankWorld.SetNum(NumPlanks);
	Dips.Reset();
	const FTransform ActorXf = GetActorTransform();
	for (int32 i = 0; i < MaxPlanks; ++i)
	{
		UBoxComponent* Slat = PlankBoxes.IsValidIndex(i) ? PlankBoxes[i].Get() : nullptr;
		if (!Slat)
		{
			continue;
		}
		const bool bUsed = i < NumPlanks && !Planks[i].bMissing;
		Slat->SetCollisionEnabled(bUsed ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		if (i < NumPlanks)
		{
			PlankPose[i] = PlankTransform(i, 0.0, 0.0, false);
			VisualPose[i] = PlankPose[i];
			const FTransform BoxXf = FTransform(FVector(0.0, Planks[i].CenterY, PlankThick * 0.5 - BoxHalfZ)) * PlankPose[i];
			Slat->SetBoxExtent(FVector(PlankDepth * 0.5 + 1.0, Planks[i].HalfLength, BoxHalfZ), false);
			Slat->SetRelativeLocationAndRotation(BoxXf.GetLocation(), BoxXf.GetRotation());
			Slat->ComponentVelocity = FVector::ZeroVector;
			PrevPlankWorld[i] = ActorXf.TransformPosition(BoxXf.GetLocation());
		}
	}

	// Tablero: en reposo en el editor; en juego, la malla procedural que se mueve.
	const UWorld* World = GetWorld();
	if (World && World->IsGameWorld())
	{
		// El servidor dedicado no carga los componentes sin colisión (UPrimitiveComponent::NeedsLoadForServer, #658).
		if (DeckPreview)
		{
			DeckPreview->SetStaticMesh(nullptr);
			DeckPreview->SetVisibility(false);
		}
		if (DeckMesh)
		{
			BuildRuntimeDeck();
		}
	}
	else
	{
		FBuffers Deck;
		BuildDeck(Deck, PlankPose, Planks, Dims);
		if (DeckPreview)
		{
			DeckPreview->SetStaticMesh(TNPlaygroundKit::BuildMesh(this, Deck, Mat));
			DeckPreview->SetVisibility(true);
		}
	}
}

void ATN_WobblyBridge::BuildRuntimeDeck()
{
	using namespace TNWobblyBridgeDetail;
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || GetNetMode() == NM_DedicatedServer || NumPlanks <= 0)
	{
		return;
	}
	if (!DeckMesh)
	{
		DeckMesh = NewObject<UProceduralMeshComponent>(this, NAME_None, RF_Transient);
		DeckMesh->SetupAttachment(SceneRoot);
		DeckMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		DeckMesh->SetCanEverAffectNavigation(false);
		DeckMesh->bUseAsyncCooking = true;
		DeckMesh->RegisterComponent();
	}
	FBridgeDims Dims;
	Dims.Span = SpanLength;
	Dims.Width = DeckWidth;
	Dims.Height = DeckHeight;
	Dims.Depth = PlankDepth;
	Dims.bTowers = bSandTowers;
	Dims.bSteps = bSandTowers && bStairs;
	FBuffers Deck;
	BuildDeck(Deck, VisualPose, Planks, Dims);
	const TArray<FProcMeshTangent> NoTangents;
	DeckMesh->ClearAllMeshSections();
	DeckMesh->CreateMeshSection_LinearColor(0, Deck.Verts, Deck.Tris, Deck.Normals, Deck.UVs, Deck.Colors, NoTangents, false);
	DeckMesh->SetMaterial(0, TNPlaygroundKit::VertexColorMaterial());
}

void ATN_WobblyBridge::UpdateDeckMesh()
{
	using namespace TNWobblyBridgeDetail;
	if (!DeckMesh || DeckMesh->GetNumSections() == 0)
	{
		return;
	}
	FBridgeDims Dims;
	Dims.Span = SpanLength;
	Dims.Width = DeckWidth;
	Dims.Height = DeckHeight;
	Dims.Depth = PlankDepth;
	Dims.bTowers = bSandTowers;
	Dims.bSteps = bSandTowers && bStairs;
	FBuffers Deck;
	if (const FProcMeshSection* Section = DeckMesh->GetProcMeshSection(0))
	{
		// Mismo tamaño que la sección creada: sin realojar los arrays en cada fotograma.
		Deck.Verts.Reserve(Section->ProcVertexBuffer.Num());
		Deck.Normals.Reserve(Section->ProcVertexBuffer.Num());
		Deck.UVs.Reserve(Section->ProcVertexBuffer.Num());
		Deck.Colors.Reserve(Section->ProcVertexBuffer.Num());
		Deck.Tris.Reserve(Section->ProcIndexBuffer.Num());
	}
	// La malla sí lleva los hundimientos bajo cada tortuga (la colisión no: ver MovePlanks).
	for (int32 i = 0; i < NumPlanks && i < VisualPose.Num(); ++i)
	{
		VisualPose[i] = PlankTransform(i, PoseClock, Excitation, true);
	}
	BuildDeck(Deck, VisualPose, Planks, Dims);
	// Solo posiciones y normales: colores, UV y triángulos se quedan los de la creación (misma topología).
	const TArray<FVector2D> KeepUVs;
	const TArray<FColor> KeepColors;
	const TArray<FProcMeshTangent> KeepTangents;
	DeckMesh->UpdateMeshSection(0, Deck.Verts, Deck.Normals, KeepUVs, KeepColors, KeepTangents);
}

double ATN_WobblyBridge::AdvanceClock(float DeltaSeconds)
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
	// Avanza con el fotograma y se acerca poco a poco a la hora del servidor: sin saltos cuando esta se corrige.
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

void ATN_WobblyBridge::PlayCreak(float Volume, const FVector& WorldAt)
{
	if (!Voice || Volume <= 0.f)
	{
		return;
	}
	Voice->SetWorldLocation(WorldAt);
	Voice->TriggerSound(ETNPlaygroundSound::Creak, FMath::FRandRange(0.85f, 1.2f), Volume * CreakVolume);
}

void ATN_WobblyBridge::UpdateRiders(float DeltaSeconds)
{
	using namespace TNWobblyBridgeDetail;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const FTransform ActorXf = GetActorTransform();
	double Target = IdleWobble;
	float Kick = 0.f;

	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		ACharacter* Walker = *It;
		if (!IsValid(Walker))
		{
			continue;
		}
		const FVector Local = ActorXf.InverseTransformPosition(Walker->GetActorLocation());
		const bool bNear = FMath::Abs(Local.X) < SpanLength * 0.5 + 200.0 && FMath::Abs(Local.Y) < DeckWidth + 250.0
			&& Local.Z > DeckHeight - Sag - 400.0 && Local.Z < DeckHeight + 500.0;
		FTNBridgeRider* Rider = Riders.Find(Walker);
		if (!bNear && !Rider)
		{
			continue;
		}
		if (!Rider)
		{
			Rider = &Riders.Add(Walker);
		}
		const UPrimitiveComponent* Base = Walker->GetMovementBase();
		const bool bOn = bNear && Base && Base->GetOwner() == this && Base != FrameCollision.Get();
		const UCharacterMovementComponent* Move = Walker->GetCharacterMovement();
		const float Vz = Move ? static_cast<float>(Move->Velocity.Z) : 0.f;
		const float Speed = Move ? static_cast<float>(Move->Velocity.Size2D()) : 0.f;
		if (bOn && !Rider->bOnDeck && Rider->LastVz < -220.f)
		{
			// Aterrizaje: sacudida proporcional a la caída y crujido.
			const float Hit = 0.25f + 0.3f * FMath::Clamp((-Rider->LastVz - 220.f) / 700.f, 0.f, 1.f);
			Kick += Hit;
			PlayCreak(0.5f + Hit, Walker->GetActorLocation());
		}
		Rider->bOnDeck = bOn;
		Rider->LastVz = Vz;
		Rider->AlongX = static_cast<float>(Local.X);
		Rider->Dip = FMath::FInterpConstantTo(Rider->Dip, bOn ? 1.f : 0.f, DeltaSeconds, 5.f);
		if (bOn)
		{
			const float RunFrac = FMath::Clamp((Speed - 150.f) / 450.f, 0.f, 1.f);
			Target += 0.22 + RunBoost * 0.6 * RunFrac;
			Rider->CreakTimer -= DeltaSeconds;
			if (Speed > 200.f && Rider->CreakTimer <= 0.f)
			{
				PlayCreak(0.35f + 0.4f * RunFrac, Walker->GetActorLocation());
				Rider->CreakTimer = FMath::Lerp(0.8f, 0.35f, RunFrac) + FMath::FRandRange(0.f, 0.15f);
			}
		}
	}

	// Limpieza: quien ya no existe o se ha ido del todo.
	for (auto It = Riders.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || (!It.Value().bOnDeck && It.Value().Dip <= 0.f))
		{
			It.RemoveCurrent();
		}
	}
	Dips.Reset();
	for (const TPair<TWeakObjectPtr<ACharacter>, FTNBridgeRider>& Pair : Riders)
	{
		if (Pair.Value.Dip > 0.01f)
		{
			Dips.Add(FVector2D(Pair.Value.AlongX, Pair.Value.Dip));
		}
	}

	if (HasAuthority())
	{
		// Agitación (servidor): sube deprisa, baja despacio; los aterrizajes la disparan al momento.
		const double Goal = FMath::Min(Target, static_cast<double>(MaxExcitation));
		const double Rate = Goal > Excitation ? 1.6 : 0.45;
		Excitation += static_cast<float>((Goal - Excitation) * FMath::Min(1.0, DeltaSeconds * Rate));
		Excitation = FMath::Min(MaxExcitation, Excitation + Kick);
		// Se replica cuando cambia el byte (hasta 10 Hz).
		NetExcitation = TNWobblyBridgeNet::QuantizeExcitation(Excitation);
	}
	else
	{
		// Clientes: la del servidor, suavizada entre actualizaciones. Calcularla aquí con las tortugas que ve este cliente
		// (las demás llegan con retraso) movía sus tablones a otro sitio que en el servidor.
		Excitation = FMath::FInterpTo(Excitation, TNWobblyBridgeNet::DequantizeExcitation(NetExcitation), DeltaSeconds, ClientExcitationInterpSpeed);
	}

	// Crujido suelto de vez en cuando si se mueve bastante.
	IdleCreakTimer -= DeltaSeconds;
	if (IdleCreakTimer <= 0.f)
	{
		IdleCreakTimer = FMath::FRandRange(3.f, 6.5f);
		if (Excitation > 0.45f)
		{
			PlayCreak(0.25f + 0.15f * Excitation, ActorXf.TransformPosition(FVector(0.0, 0.0, DeckHeight - Sag)));
		}
	}
}

void ATN_WobblyBridge::MovePlanks(double WobbleTime, float DeltaSeconds)
{
	using namespace TNWobblyBridgeDetail;
	const FTransform ActorXf = GetActorTransform();
	PoseClock = WobbleTime;
	for (int32 i = 0; i < NumPlanks && i < PlankPose.Num(); ++i)
	{
		// Sin hundimientos: dependen de dónde ve cada máquina a las tortugas y harían los tablones distintos en cada una.
		PlankPose[i] = PlankTransform(i, WobbleTime, Excitation, false);
		UBoxComponent* Slat = PlankBoxes.IsValidIndex(i) ? PlankBoxes[i].Get() : nullptr;
		if (!Slat || Planks[i].bMissing)
		{
			continue;
		}
		const FTransform BoxXf = FTransform(FVector(0.0, Planks[i].CenterY, PlankThick * 0.5 - BoxHalfZ)) * PlankPose[i];
		// Sin barrido: el CharacterMovement de quien lo pisa sigue a su base en su propio movimiento (el tick de este
		// actor va antes: SetBase lo pone como prerrequisito).
		Slat->SetRelativeLocationAndRotation(BoxXf.GetLocation(), BoxXf.GetRotation());
		const FVector WorldPos = ActorXf.TransformPosition(BoxXf.GetLocation());
		// Velocidad de la base: al saltar desde el puente se hereda su vaivén.
		Slat->ComponentVelocity = DeltaSeconds > 1e-4f ? (WorldPos - PrevPlankWorld[i]) / DeltaSeconds : FVector::ZeroVector;
		PrevPlankWorld[i] = WorldPos;
	}
}

void ATN_WobblyBridge::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (NumPlanks <= 0 || PlankPose.Num() != NumPlanks)
	{
		return;
	}
	const double WobbleTime = AdvanceClock(DeltaSeconds);
	UpdateRiders(DeltaSeconds);
	MovePlanks(WobbleTime, DeltaSeconds);
	if (DeckMesh && DeckMesh->WasRecentlyRendered(0.3f))
	{
		UpdateDeckMesh();
	}
}
