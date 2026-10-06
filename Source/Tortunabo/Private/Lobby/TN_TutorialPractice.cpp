#include "Lobby/TN_TutorialPractice.h"
#include "Lobby/TN_TutorialCourse.h"
#include "Art/TN_Art.h"
#include "../World/ProcMap/TN_ProcMapFaunaMeshes.h"
#include "../World/ProcMap/TN_ProcMapRuntimeMesh.h"
#include "Playground/TN_PlaygroundMeshKit.h"
#include "Core/TN_InventoryTypes.h"
#include "Core/TN_Log.h"
#include "World/TN_InkProjectile.h"
#include "World/TN_ThrowableItemActor.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

namespace TNTutorialDummyDetail
{
	/** El cangrejo de la fauna (~22 cm de ancho) a este tamaño: más de un metro de pinza a pinza. */
	constexpr float CrabScale = 5.f;
	/** Caja de colisión (semiejes, cm) y su altura. */
	const FVector BoxHalf(48.0, 66.0, 30.0);
	constexpr double BoxZ = 30.0;
	/** Periodo del vaivén (rad/s) y cuánto tarda en volver a él tras el mareo. */
	constexpr double SwayRate = 0.9;
	constexpr float RecoverSeconds = 0.6f;

	/** Estrellitas del mareo: tres estrellas doradas en corro sobre el caparazón. */
	void BuildStars(TNPlaygroundKit::FBuffers& B)
	{
		for (int32 k = 0; k < 3; ++k)
		{
			const double A = TNPlaygroundKit::KitTwoPi * k / 3.0;
			const FVector C(52.0 * FMath::Cos(A), 52.0 * FMath::Sin(A), 0.0);
			TNPlaygroundKit::AddStarfish(B, C, FVector(FMath::Cos(A), FMath::Sin(A), 0.0), FVector::UpVector, 13.0, 4.0, TNPlaygroundKit::Rgb(0xFFD23F, 0.3f));
			TNPlaygroundKit::AddStarfish(B, C, -FVector(FMath::Cos(A), FMath::Sin(A), 0.0), FVector::UpVector, 13.0, 4.0, TNPlaygroundKit::Rgb(0xFFD23F, 0.3f));
		}
	}

	/** Pieza de arte de cada parte del cangrejo (Docs/Arte_Assets.md): cada una se mueve por su cuenta. */
	FName PartSlot(TNFauna::ETNFaunaBone Bone)
	{
		using EB = TNFauna::ETNFaunaBone;
		switch (Bone)
		{
			case EB::Body:  return TN_ART("Lobby.Tutorial.DummyCrab.Shell");
			case EB::LegFL: return TN_ART("Lobby.Tutorial.DummyCrab.LegsLeft");
			case EB::LegFR: return TN_ART("Lobby.Tutorial.DummyCrab.LegsRight");
			case EB::ClawL: return TN_ART("Lobby.Tutorial.DummyCrab.ClawLeft");
			case EB::ClawR: return TN_ART("Lobby.Tutorial.DummyCrab.ClawRight");
			default:        return NAME_None;
		}
	}

	UMaterialInterface* LoadMaterial(const TCHAR* Path)
	{
		UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, Path, nullptr, LOAD_NoWarn);
		return Mat ? Mat : TNPlaygroundKit::VertexColorMaterial();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// ATN_TutorialDummy
// ─────────────────────────────────────────────────────────────────────────────

void ATN_TutorialDummy::SetCourse(ATN_TutorialCourse* InCourse)
{
	Course = InCourse;
}

ATN_TutorialDummy::ATN_TutorialDummy()
{
	using namespace TNTutorialDummyDetail;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = true;
	SetReplicatingMovement(false);
	SetCanBeDamaged(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	// Bloquea como cualquier cosa sólida: la bola rebota en ella (y su golpe lo marea) y la tortuga no la atraviesa.
	Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	Body->SetupAttachment(SceneRoot);
	Body->InitBoxExtent(BoxHalf);
	Body->SetRelativeLocation(FVector(0.0, 0.0, BoxZ));
	Body->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Body->SetCanEverAffectNavigation(false);
	Body->SetHiddenInGame(true);
}

void ATN_TutorialDummy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_TutorialDummy, StunnedUntil);
}

void ATN_TutorialDummy::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() != NM_DedicatedServer)
	{
		BuildCrab();
	}
}

void ATN_TutorialDummy::BuildCrab()
{
	using namespace TNTutorialDummyDetail;
	using namespace TNFauna;
	TArray<FTNFaunaPart> PartsData;
	FTNFaunaRig Rig;
	TNFaunaBuildSpecies(ETNFaunaSpecies::Crab, PartsData, Rig);
	BodyZ = static_cast<float>(Rig.BodyZ);
	UMaterialInterface* Solid = LoadMaterial(TEXT("/Game/ProcMap/Materials/M_ProcFoliage.M_ProcFoliage"));
	UMaterialInterface* Glow = LoadMaterial(TEXT("/Game/ProcMap/Materials/M_ProcGlow.M_ProcGlow"));
	for (FTNFaunaPart& Part : PartsData)
	{
		UStaticMesh* Mesh = TNProcRuntimeMesh::MakeStaticMesh(this, Part.Mesh, Part.bGlow ? Glow : Solid, false, 0.f, 1.f, 0.f);
		if (!Mesh)
		{
			continue;
		}
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		Comp->SetStaticMesh(Mesh);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCanEverAffectNavigation(false);
		Comp->bEvaluateWorldPositionOffset = false;
		Comp->SetupAttachment(SceneRoot);
		Comp->RegisterComponent();
		TNArt::ApplyToComponent(Comp, TNTutorialDummyDetail::PartSlot(Part.Bone));
		Meshes.Add(Mesh);
		Parts.Add(Comp);
		PartBone.Add(static_cast<uint8>(Part.Bone));
		PartPivot.Add(Part.Pivot);
	}
	TNPlaygroundKit::FBuffers StarBuffers;
	BuildStars(StarBuffers);
	if (UStaticMesh* StarMesh = TNPlaygroundKit::BuildMesh(this, StarBuffers, Glow))
	{
		Stars = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		Stars->SetStaticMesh(StarMesh);
		Stars->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Stars->SetCastShadow(false);
		Stars->SetupAttachment(SceneRoot);
		Stars->RegisterComponent();
		Stars->SetVisibility(false);
		TNArt::ApplyToComponent(Stars, TN_ART("Lobby.Tutorial.DummyCrab.Stars"));
		Meshes.Add(StarMesh);
	}
	Animate(0.f);
}

double ATN_TutorialDummy::ServerNow() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

float ATN_TutorialDummy::SwayAt(double Now) const
{
	return SwayDistance * static_cast<float>(FMath::Sin(Now * TNTutorialDummyDetail::SwayRate));
}

bool ATN_TutorialDummy::IsStunned() const
{
	return StunnedUntil >= 0.f && ServerNow() < static_cast<double>(StunnedUntil);
}

void ATN_TutorialDummy::ApplyStun(float Duration)
{
	if (!HasAuthority())
	{
		return;
	}
	const double Now = ServerNow();
	StunnedUntil = FMath::Max(StunnedUntil, static_cast<float>(Now + FMath::Max(StunSeconds, Duration)));
	ForceNetUpdate();
	// Quién lo ha lanzado: lo que acaba de darle (la bola o la tinta que tenga más cerca).
	APawn* Thrower = nullptr;
	double Best = 500.0;
	const FVector Center = Body ? Body->GetComponentLocation() : GetActorLocation();
	auto Consider = [&Thrower, &Best, &Center](const AActor* Projectile)
	{
		APawn* By = Projectile ? Projectile->GetInstigator() : nullptr;
		const double Dist = Projectile ? FVector::Dist(Projectile->GetActorLocation(), Center) : 1e9;
		if (By && Dist < Best)
		{
			Best = Dist;
			Thrower = By;
		}
	};
	for (TActorIterator<ATN_ThrowableItemActor> It(GetWorld()); It; ++It) { Consider(*It); }
	for (TActorIterator<ATN_InkProjectile> It(GetWorld()); It; ++It) { Consider(*It); }
	if (ATN_TutorialCourse* CourseActor = Course.Get())
	{
		CourseActor->NotifyDummyHit(Thrower);
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Tutorial] Cangrejo de prácticas mareado (%s)."), *GetNameSafe(Thrower));
}

void ATN_TutorialDummy::ApplyBlind(float Duration)
{
	// La tinta también lo marea (como a los enemigos de la playa).
	ApplyStun(Duration);
}

void ATN_TutorialDummy::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Animate(DeltaSeconds);
}

void ATN_TutorialDummy::Animate(float DeltaSeconds)
{
	using namespace TNTutorialDummyDetail;
	using namespace TNFauna;
	AnimClock += DeltaSeconds;
	const double Now = ServerNow();
	const bool bStunned = IsStunned();
	float Sway = SwayAt(Now);
	if (bStunned && !bWasStunned)
	{
		FrozenSway = Body ? static_cast<float>(Body->GetRelativeLocation().Y) : Sway;
	}
	if (bStunned)
	{
		Sway = FrozenSway;
	}
	else if (StunnedUntil >= 0.f && Now - StunnedUntil < RecoverSeconds)
	{
		// Vuelve poco a poco a su vaivén.
		const float T = static_cast<float>((Now - StunnedUntil) / RecoverSeconds);
		Sway = FMath::Lerp(FrozenSway, Sway, FMath::SmoothStep(0.f, 1.f, T));
	}
	bWasStunned = bStunned;
	const float PrevSway = Body ? static_cast<float>(Body->GetRelativeLocation().Y) : Sway;
	if (Body)
	{
		Body->SetRelativeLocation(FVector(0.0, Sway, BoxZ));
	}
	if (Parts.Num() == 0)
	{
		return;
	}

	const float Speed = DeltaSeconds > 0.f ? FMath::Abs(Sway - PrevSway) / DeltaSeconds : 0.f;
	const float Move = FMath::Clamp(Speed / 60.f, 0.f, 1.f);
	const float Gait = AnimClock * 9.f;
	const FTransform Root(FRotator::ZeroRotator, FVector(0.0, Sway, 0.0), FVector(CrabScale));
	FRotator BodyRot = FRotator::ZeroRotator;
	FVector BodyOff(0.0, 0.0, 0.3 * FMath::Sin(AnimClock * 3.f) + 0.25 * Move * FMath::Abs(FMath::Sin(Gait)));
	if (bStunned)
	{
		BodyRot.Roll = 7.f * FMath::Sin(AnimClock * 9.f);
		BodyRot.Pitch = -5.f;
	}
	const FTransform BodyFrame = FTransform(BodyRot, FVector(0.0, 0.0, BodyZ) + BodyOff) * Root;
	for (int32 p = 0; p < Parts.Num(); ++p)
	{
		const ETNFaunaBone Bone = static_cast<ETNFaunaBone>(PartBone[p]);
		FRotator Rot = FRotator::ZeroRotator;
		switch (Bone)
		{
			case ETNFaunaBone::Body:
				Parts[p]->SetRelativeTransform(BodyFrame);
				continue;
			case ETNFaunaBone::LegFL:
			case ETNFaunaBone::LegFR:
			{
				const float Side = Bone == ETNFaunaBone::LegFL ? 1.f : -1.f;
				Rot.Roll = Side * (bStunned ? 18.f : 12.f * Move * FMath::Sin(Gait + (Side > 0.f ? 0.f : UE_PI)));
				break;
			}
			case ETNFaunaBone::ClawL:
			case ETNFaunaBone::ClawR:
			{
				const float Snap = FMath::Max(0.f, FMath::Sin(AnimClock * 2.3f + (Bone == ETNFaunaBone::ClawL ? 0.f : 1.4f)));
				Rot.Pitch = bStunned ? -28.f : 8.f + 26.f * Snap * Snap;
				break;
			}
			default:
				break;
		}
		Parts[p]->SetRelativeTransform(FTransform(Rot, PartPivot[p]) * BodyFrame);
	}
	if (Stars)
	{
		Stars->SetVisibility(bStunned);
		if (bStunned)
		{
			Stars->SetRelativeLocationAndRotation(FVector(0.0, Sway, 95.0 + 6.0 * FMath::Sin(AnimClock * 4.f)),
				FRotator(0.f, FMath::Fmod(AnimClock * 220.f, 360.f), 0.f));
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// ATN_TutorialSearchSpot
// ─────────────────────────────────────────────────────────────────────────────

ATN_TutorialSearchSpot::ATN_TutorialSearchSpot()
{
	PromptText = NSLOCTEXT("TNTutorial", "MoundPrompt", "Mantén para rebuscar en el montículo");
	SearchSeconds = 1.3f;
	LootChance = 1.f;
	bRepeatable = true;
	RepeatCooldown = 1.2f;
	MaxLootLying = 2;
}

float ATN_TutorialSearchSpot::GetLootWeight(FName RowName, const FTN_InventoryItem& Row) const
{
	// Siempre la bola: es la que se lanza al cangrejo en la estación siguiente.
	static const FName BallId(TEXT("ThrowableBall"));
	return (RowName == BallId || Row.ItemId == BallId) ? 1.f : 0.f;
}

bool ATN_TutorialSearchSpot::PickLoot(FTN_InventoryItem& OutItem, const APawn* /*Searcher*/) const
{
	// La tabla del coop (ATN_ProcSearchSpot::PickLoot) suma sus objetos con peso propio: aquí saldría la bola solo a veces.
	return PickCatalogItem(GetLootTable(), [this](FName RowName, const FTN_InventoryItem& Row) { return GetLootWeight(RowName, Row); },
		OutItem);
}

// ─────────────────────────────────────────────────────────────────────────────
// ATN_TutorialCatapult
// ─────────────────────────────────────────────────────────────────────────────

ATN_TutorialCatapult::ATN_TutorialCatapult()
{
	// Se recarga (la usan todos los que pasan) y cae en la pradera del otro lado del cañón (~28 m, con poco desvío).
	bSingleUse = false;
	ReloadSeconds = 3.f;
	WarnSeconds = 0.8f;
	LaunchSpeed = 1600.f;
	LaunchPitch = 44.f;
	DeviationDeg = 2.f;
	SetNetCullDistanceSquared(FMath::Square(9000.f));
}

void ATN_TutorialCatapult::SetupTutorialSpec(int32 InSeed, float InSizeScale)
{
	Spec.Element = ETNBeachElement::Catapult;
	Spec.Seed = InSeed;
	Spec.SizeScale = InSizeScale;
	Spec.Extent = 0.f;
	Spec.Flags = 0;
}
