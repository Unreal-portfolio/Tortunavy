#include "World/TN_SupplyDrop.h"
#include "TN_LootGlowKit.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace TNSupplyDropDetail
{
	const TCHAR* const ParachuteMeshPath = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	const TCHAR* const PlainMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

	/** Altura (cm) del centro del paracaídas sobre la base de la caja y su escala (esfera de 100 cm aplastada). */
	constexpr float ParachuteLift = 330.f;
	const FVector ParachuteScale(2.8f, 2.8f, 0.9f);

	/** Airdrops vivos de todos los mundos del proceso (en PIE, el del servidor y los de los clientes); se filtran por mundo. */
	TArray<TWeakObjectPtr<ATN_SupplyDrop>>& Registry()
	{
		static TArray<TWeakObjectPtr<ATN_SupplyDrop>> Drops;
		return Drops;
	}

	/** Un aviso o un aterrizaje solo suena si esta máquina lo ve a tiempo (no al entrar tarde). */
	constexpr double FreshSeconds = 1.5;
}

// ── Reglas del vuelo ─────────────────────────────────────────────────────────

ETNAirdropPhase TNAirdropRules::PhaseAt(const FTNAirdropFlight& Flight, double Now)
{
	const double Elapsed = Now - static_cast<double>(Flight.StartTime);
	const double Warn = FMath::Max(0.0, static_cast<double>(Flight.WarnSeconds));
	if (Elapsed < Warn)
	{
		return ETNAirdropPhase::Warning;
	}
	return Elapsed < Warn + FMath::Max(0.0, static_cast<double>(Flight.FallSeconds)) ? ETNAirdropPhase::Falling : ETNAirdropPhase::Landed;
}

float TNAirdropRules::HeightAt(const FTNAirdropFlight& Flight, double Now)
{
	const float Height = FMath::Max(0.f, Flight.Height);
	switch (PhaseAt(Flight, Now))
	{
		case ETNAirdropPhase::Warning:
			return Height;
		case ETNAirdropPhase::Falling:
		{
			const double FallElapsed = Now - static_cast<double>(Flight.StartTime) - FMath::Max(0.0, static_cast<double>(Flight.WarnSeconds));
			const double Alpha = FMath::Clamp(FallElapsed / FMath::Max(0.01, static_cast<double>(Flight.FallSeconds)), 0.0, 1.0);
			return Height * static_cast<float>(1.0 - Alpha);
		}
		default:
			return 0.f;
	}
}

float TNAirdropRules::SecondsToLand(const FTNAirdropFlight& Flight, double Now)
{
	const double LandTime = static_cast<double>(Flight.StartTime) + FMath::Max(0.0, static_cast<double>(Flight.WarnSeconds))
		+ FMath::Max(0.0, static_cast<double>(Flight.FallSeconds));
	return static_cast<float>(FMath::Max(0.0, LandTime - Now));
}

// ── Actor ────────────────────────────────────────────────────────────────────

ATN_SupplyDrop::ATN_SupplyDrop()
{
	SearchSeconds = 2.f;
	DefaultLoot = TNLootRules::AirdropDefaults();
	// El aviso tiene que llegar a todos, estén donde estén.
	bAlwaysRelevant = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ParachuteFinder(TNSupplyDropDetail::ParachuteMeshPath);
	ParachuteMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ParachuteMesh"));
	ParachuteMesh->SetupAttachment(SceneRoot);
	ParachuteMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ParachuteMesh->SetGenerateOverlapEvents(false);
	ParachuteMesh->SetCanEverAffectNavigation(false);
	ParachuteMesh->SetRelativeLocation(FVector(0.f, 0.f, TNSupplyDropDetail::ParachuteLift));
	ParachuteMesh->SetRelativeScale3D(TNSupplyDropDetail::ParachuteScale);
	ParachuteMesh->SetCastShadow(true);
	if (ParachuteFinder.Succeeded())
	{
		ParachuteMesh->SetStaticMesh(ParachuteFinder.Object);
	}
}

void ATN_SupplyDrop::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ATN_SupplyDrop, Flight, COND_InitialOnly);
}

ATN_SupplyDrop* ATN_SupplyDrop::ServerLaunch(UWorld* World, TSubclassOf<ATN_SupplyDrop> Class, const FVector& Landing, float Height,
	float WarnSeconds, float FallSeconds)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}
	UClass* DropClass = Class ? Class.Get() : StaticClass();
	FTNAirdropFlight Flight;
	Flight.Landing = Landing;
	Flight.Height = FMath::Max(0.f, Height);
	Flight.WarnSeconds = FMath::Max(0.f, WarnSeconds);
	Flight.FallSeconds = FMath::Max(0.f, FallSeconds);
	const AGameStateBase* GameState = World->GetGameState();
	Flight.StartTime = static_cast<float>(GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds());

	const FTransform Start(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), Landing + FVector(0.f, 0.f, Flight.Height));
	ATN_SupplyDrop* Drop = World->SpawnActorDeferred<ATN_SupplyDrop>(DropClass, Start, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Drop)
	{
		UE_LOG(LogTNLoot, Warning, TEXT("No se ha podido lanzar el airdrop en %s."), *Landing.ToCompactString());
		return nullptr;
	}
	// Antes de terminar de crearlo: el vuelo sale en la primera réplica.
	Drop->Flight = Flight;
	Drop->FinishSpawning(Start);
	UE_LOG(LogTNLoot, Log, TEXT("Airdrop %s: aterriza en %s dentro de %.1f s."), *Drop->GetName(), *Landing.ToCompactString(),
		Flight.WarnSeconds + Flight.FallSeconds);
	return Drop;
}

const ATN_SupplyDrop* ATN_SupplyDrop::FindIncoming(const UWorld* World)
{
	const ATN_SupplyDrop* Best = nullptr;
	float BestSeconds = TNumericLimits<float>::Max();
	for (const TWeakObjectPtr<ATN_SupplyDrop>& Weak : TNSupplyDropDetail::Registry())
	{
		const ATN_SupplyDrop* Drop = Weak.Get();
		if (!Drop || Drop->GetWorld() != World || Drop->GetPhase() == ETNAirdropPhase::Landed)
		{
			continue;
		}
		const float Seconds = Drop->GetSecondsToLand();
		if (Seconds < BestSeconds)
		{
			Best = Drop;
			BestSeconds = Seconds;
		}
	}
	return Best;
}

ETNAirdropPhase ATN_SupplyDrop::GetPhase() const
{
	return TNAirdropRules::PhaseAt(Flight, ServerNow());
}

float ATN_SupplyDrop::GetSecondsToLand() const
{
	return TNAirdropRules::SecondsToLand(Flight, ServerNow());
}

void ATN_SupplyDrop::BeginPlay()
{
	Super::BeginPlay();
	TNSupplyDropDetail::Registry().RemoveAll([](const TWeakObjectPtr<ATN_SupplyDrop>& Weak) { return !Weak.IsValid(); });
	TNSupplyDropDetail::Registry().AddUnique(this);
	if (GetNetMode() != NM_DedicatedServer && ParachuteMesh && ParachuteMesh->GetStaticMesh())
	{
		if (UMaterialInterface* Plain = LoadObject<UMaterialInterface>(nullptr, TNSupplyDropDetail::PlainMaterialPath, nullptr, LOAD_NoWarn))
		{
			UMaterialInstanceDynamic* Cloth = UMaterialInstanceDynamic::Create(Plain, ParachuteMesh);
			Cloth->SetVectorParameterValue(TEXT("Color"), ParachuteColor);
			ParachuteMesh->SetMaterial(0, Cloth);
		}
	}
	UpdateFlight(0.f);
}

void ATN_SupplyDrop::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	TNSupplyDropDetail::Registry().RemoveAll([this](const TWeakObjectPtr<ATN_SupplyDrop>& Weak) { return !Weak.IsValid() || Weak.Get() == this; });
	Super::EndPlay(EndPlayReason);
}

void ATN_SupplyDrop::Tick(float DeltaSeconds)
{
	UpdateFlight(DeltaSeconds);
	Super::Tick(DeltaSeconds);
}

bool ATN_SupplyDrop::WantsFrameTick() const
{
	// En el aire se mueve a cada fotograma; en el suelo el haz está quieto y basta el tick lento del rebuscable.
	return Super::WantsFrameTick() || GetPhase() != ETNAirdropPhase::Landed;
}

void ATN_SupplyDrop::UpdateFlight(float DeltaSeconds)
{
	const double Now = ServerNow();
	const ETNAirdropPhase Phase = TNAirdropRules::PhaseAt(Flight, Now);
	const FVector Landing = Flight.Landing;
	const FVector Wanted = Landing + FVector(0.f, 0.f, TNAirdropRules::HeightAt(Flight, Now));
	if (!GetActorLocation().Equals(Wanted, 0.5))
	{
		SetActorLocation(Wanted);
	}
	if (static_cast<uint8>(Phase) != AppliedPhase)
	{
		// Recién cambiada (no al entrar tarde): suenan el aviso y el aterrizaje.
		const double PhaseStart = static_cast<double>(Flight.StartTime)
			+ (Phase == ETNAirdropPhase::Warning ? 0.0 : static_cast<double>(Flight.WarnSeconds))
			+ (Phase == ETNAirdropPhase::Landed ? static_cast<double>(Flight.FallSeconds) : 0.0);
		ApplyPhase(Phase, Now - PhaseStart < TNSupplyDropDetail::FreshSeconds);
	}
	TickWarningMarkers(DeltaSeconds);
}

void ATN_SupplyDrop::ApplyPhase(ETNAirdropPhase Phase, bool bFresh)
{
	AppliedPhase = static_cast<uint8>(Phase);
	const bool bVisible = Phase != ETNAirdropPhase::Warning;
	const bool bLanded = Phase == ETNAirdropPhase::Landed;
	if (CrateMesh)
	{
		CrateMesh->SetVisibility(bVisible);
		CrateMesh->SetCollisionEnabled(bLanded ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}
	if (LidMesh)
	{
		LidMesh->SetVisibility(bVisible);
	}
	if (ParachuteMesh)
	{
		ParachuteMesh->SetVisibility(Phase == ETNAirdropPhase::Falling);
	}
	if (!bFresh || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const FVector Landing = Flight.Landing;
	if (Phase == ETNAirdropPhase::Warning && IncomingSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, IncomingSound, Landing);
	}
	if (bLanded)
	{
		if (LandSound)
		{
			UGameplayStatics::SpawnSoundAtLocation(this, LandSound, Landing);
		}
		if (LandFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, LandFX, Landing);
		}
		// Polvo y chispas del rebuscable al tocar el suelo.
		EmitSparkles(Landing + FVector(0.f, 0.f, 30.f), 14, FVector::UpVector, 3.f);
	}
}

void ATN_SupplyDrop::EnsureWarningMarkers()
{
	if (WarningRing || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	UStaticMesh* RingAsset = TNLootGlow::RingMesh();
	UStaticMesh* BeamAsset = TNLootGlow::BeamMesh();
	auto MakeMarker = [this](UStaticMesh* Asset) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Marker = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		Marker->SetupAttachment(SceneRoot);
		Marker->SetAbsolute(true, true, true);
		Marker->SetStaticMesh(Asset);
		Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Marker->SetGenerateOverlapEvents(false);
		Marker->SetCanEverAffectNavigation(false);
		Marker->SetCastShadow(false);
		Marker->SetReceivesDecals(false);
		Marker->RegisterComponent();
		return Marker;
	};
	// Sin las mallas (faltan los materiales del mapa procedural) no hay marcas: queda el cartel del HUD.
	WarningRing = MakeMarker(RingAsset);
	WarningBeam = MakeMarker(BeamAsset);
	WarningRing->SetVisibility(RingAsset != nullptr);
	WarningBeam->SetVisibility(BeamAsset != nullptr);
}

void ATN_SupplyDrop::TickWarningMarkers(float DeltaSeconds)
{
	if (GetNetMode() == NM_DedicatedServer || !GetWorld() || !GetWorld()->IsGameWorld())
	{
		return;
	}
	// El anillo, mientras está en el aire (en el suelo sale el del rebuscable); el haz, hasta que alguien la abre.
	const bool bWantRing = GetPhase() != ETNAirdropPhase::Landed;
	const bool bWantBeam = !IsOpened();
	if (!WarningRing && !bWantRing && !bWantBeam)
	{
		return;
	}
	EnsureWarningMarkers();
	WarningClock += DeltaSeconds;
	const FVector Landing = Flight.Landing;
	if (WarningRing)
	{
		const bool bShow = bWantRing && WarningRing->GetStaticMesh() != nullptr;
		if (WarningRing->IsVisible() != bShow)
		{
			WarningRing->SetVisibility(bShow);
		}
		if (bShow)
		{
			// Late deprisa: «aquí cae algo».
			const float Pulse = 0.08f * FMath::Abs(FMath::Sin(WarningClock * 4.f));
			WarningRing->SetWorldTransform(TNLootGlow::RingPose(Landing, FQuat::Identity, WarningRingRadius, WarningClock * 3.f, 1.f, Pulse));
		}
	}
	if (WarningBeam)
	{
		const bool bShow = bWantBeam && WarningBeam->GetStaticMesh() != nullptr;
		if (WarningBeam->IsVisible() != bShow)
		{
			WarningBeam->SetVisibility(bShow);
		}
		if (bShow)
		{
			const float Radius = WarningRingRadius * 0.35f / TNLootGlow::BeamUnitRadius;
			WarningBeam->SetWorldTransform(FTransform(FQuat::Identity, Landing, FVector(Radius, Radius, BeamHeight / TNLootGlow::BeamUnitHeight)));
		}
	}
}
