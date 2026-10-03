#include "World/TN_ScoreShellBurst.h"
#include "Multiplayer/TN_LocalViews.h"
#include "World/TN_ScoreShells.h"
#include "Audio/TN_ScoreShellSynthComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "ProcMap/TN_ProcMapAmbientFX.h"

namespace TNShellBurstDetail
{
	/** Más lejos de la cámara local (cm) no se crea el estallido. */
	constexpr float MaxViewDistance = 15000.f;

	/** Cuánto crece todo con el tamaño: pequeña, normal, grande y reina. */
	constexpr float SizeScale[4] = { 0.55f, 1.f, 1.35f, 1.75f };
	constexpr int32 SparkCount[4] = { 7, 14, 24, 36 };
	constexpr float FlashLumensByTier[4] = { 350.f, 1400.f, 3800.f, 8000.f };
	constexpr float FlashRadiusByTier[4] = { 280.f, 450.f, 750.f, 1050.f };
	/** Vida del actor (s): lo que dura el «¡plin!» más un margen. */
	constexpr float LifeByTier[4] = { 1.2f, 1.4f, 2.3f, 3.1f };
}

ATN_ScoreShellBurst::ATN_ScoreShellBurst()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	SetCanBeDamaged(false);
	BurstRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BurstRoot"));
	SetRootComponent(BurstRoot);
}

ATN_ScoreShellBurst* ATN_ScoreShellBurst::SpawnAt(UWorld* World, const FVector& Location, uint8 Tier)
{
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	// Lejos de todas las cámaras locales (con la pantalla partida, de la más cercana), nada.
	FVector CameraAt = Location;
	if (TNLocalViews::ClosestCamera(World, Location, CameraAt)
		&& FVector::DistSquared(CameraAt, Location) > FMath::Square(TNShellBurstDetail::MaxViewDistance))
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	ATN_ScoreShellBurst* Burst = World->SpawnActor<ATN_ScoreShellBurst>(ATN_ScoreShellBurst::StaticClass(), FTransform(Location), Params);
	if (Burst)
	{
		Burst->StartBurst(Tier);
	}
	return Burst;
}

void ATN_ScoreShellBurst::StartBurst(uint8 InTier)
{
	const int32 T = FMath::Clamp<int32>(InTier, 0, 3);
	const TNScoreShells::ETier ShellTier = TNScoreShells::TierFromIndex(T);
	const float K = TNShellBurstDetail::SizeScale[T];
	const FLinearColor Glow = TNScoreShells::GlowColor(ShellTier);
	const FVector Where = GetActorLocation();
	Life = TNShellBurstDetail::LifeByTier[T];

	// Destello: una bola blanda que se abre deprisa.
	{
		TNAmbientFX::FEmitterDesc D;
		D.Shape = TNAmbientFX::EShape::Puff;
		D.bSoft = true;
		D.bCloud = true;
		D.Color = FMath::Lerp(Glow, FLinearColor::White, 0.45f);
		D.Alpha = 0.85f;
		D.MaxParticles = 1;
		D.Rate = 0.f;
		D.SpawnRadius = 0.f;
		D.Speed = 0.f;
		D.SpeedJitter = 0.f;
		D.Gravity = 0.f;
		D.Drag = 0.f;
		D.LifeMin = 0.25f;
		D.LifeMax = 0.25f;
		D.SizeStart = 45.f * K;
		D.SizeEnd = 230.f * K;
		D.WakeDistance = TNShellBurstDetail::MaxViewDistance + 2000.f;
		const int32 Index = TNAmbientFX::AddEmitter(this, D, Where);
		if (TNAmbientFX::FEmitter* E = TNAmbientFX::GetEmitter(this, Index)) { TNAmbientFX::Burst(*E, 1); }
	}
	// Anillo que se abre en horizontal a la altura de la concha.
	{
		TNAmbientFX::FEmitterDesc D;
		D.Shape = TNAmbientFX::EShape::Ring;
		D.bSoft = true;
		D.Color = Glow;
		D.Alpha = 0.7f;
		D.MaxParticles = 1;
		D.Rate = 0.f;
		D.SpawnRadius = 0.f;
		D.Speed = 0.f;
		D.SpeedJitter = 0.f;
		D.Gravity = 0.f;
		D.Drag = 0.f;
		D.LifeMin = 0.45f;
		D.LifeMax = 0.45f;
		D.SizeStart = 60.f * K;
		D.SizeEnd = 330.f * K;
		D.WakeDistance = TNShellBurstDetail::MaxViewDistance + 2000.f;
		const int32 Index = TNAmbientFX::AddEmitter(this, D, Where);
		if (TNAmbientFX::FEmitter* E = TNAmbientFX::GetEmitter(this, Index)) { TNAmbientFX::Burst(*E, 1); }
	}
	// Chispas que saltan hacia arriba en abanico y caen.
	{
		TNAmbientFX::FEmitterDesc D;
		D.Shape = TNAmbientFX::EShape::Flake;
		D.bSoft = true;
		D.Color = FMath::Lerp(Glow, FLinearColor::White, 0.25f);
		D.Alpha = 0.95f;
		D.MaxParticles = TNShellBurstDetail::SparkCount[T];
		D.Rate = 0.f;
		D.SpawnRadius = 20.f * K;
		D.SpawnHeight = 30.f * K;
		D.Direction = FVector::UpVector;
		D.Speed = 420.f * FMath::Sqrt(K);
		D.SpeedJitter = 0.45f;
		D.Spread = 2.2f;
		D.Gravity = -700.f;
		D.Drag = 1.2f;
		D.LifeMin = 0.45f;
		D.LifeMax = 0.9f;
		D.SizeStart = 15.f * K;
		D.SizeEnd = 4.f;
		D.WakeDistance = TNShellBurstDetail::MaxViewDistance + 2000.f;
		const int32 Index = TNAmbientFX::AddEmitter(this, D, Where);
		if (TNAmbientFX::FEmitter* E = TNAmbientFX::GetEmitter(this, Index)) { TNAmbientFX::Burst(*E, TNShellBurstDetail::SparkCount[T]); }
	}
	// Grandes y reinas: estrellitas que suben despacio.
	if (ShellTier == TNScoreShells::ETier::Big || ShellTier == TNScoreShells::ETier::Grand)
	{
		TNAmbientFX::FEmitterDesc D;
		D.Shape = TNAmbientFX::EShape::Ember;
		D.bSoft = true;
		D.Color = FMath::Lerp(Glow, FLinearColor(1.f, 0.95f, 0.6f), 0.35f);
		D.Alpha = 0.9f;
		D.MaxParticles = ShellTier == TNScoreShells::ETier::Grand ? 22 : 12;
		D.Rate = 0.f;
		D.SpawnRadius = 60.f * K;
		D.SpawnHeight = 60.f;
		D.Speed = 60.f;
		D.Spread = 1.f;
		D.Gravity = 0.f;
		D.Buoyancy = 140.f;
		D.Drag = 0.6f;
		D.LifeMin = 1.f;
		D.LifeMax = 1.7f;
		D.SizeStart = 14.f;
		D.SizeEnd = 3.f;
		D.WakeDistance = TNShellBurstDetail::MaxViewDistance + 2000.f;
		const int32 Index = TNAmbientFX::AddEmitter(this, D, Where);
		if (TNAmbientFX::FEmitter* E = TNAmbientFX::GetEmitter(this, Index)) { TNAmbientFX::Burst(*E, D.MaxParticles); }
	}

	// Fogonazo de luz sin sombras que se apaga en 0,35 s.
	FlashLumens = TNShellBurstDetail::FlashLumensByTier[T];
	Flash = NewObject<UPointLightComponent>(this, NAME_None, RF_Transient);
	Flash->SetupAttachment(BurstRoot);
	Flash->SetIntensityUnits(ELightUnits::Lumens);
	Flash->SetIntensity(FlashLumens);
	Flash->SetAttenuationRadius(TNShellBurstDetail::FlashRadiusByTier[T]);
	Flash->SetLightColor(Glow);
	Flash->SetCastShadows(false);
	Flash->RegisterComponent();

	// «¡Plin!» en el sitio, más lejos cuanto más grande; pleno a unos metros (la cámara va detrás de la tortuga).
	if (UTN_ScoreShellSynthComponent* Synth = UTN_ScoreShellSynthComponent::Attach3D(this, Where, 200.f + 500.f * K, 2600.f * K))
	{
		Synth->TriggerSound(ETNScoreShellSound::Plin, static_cast<uint8>(T), 0.f, 1.f);
	}
}

void ATN_ScoreShellBurst::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	TNAmbientFX::TickOwner(this, DeltaSeconds);
	if (Flash)
	{
		const float Fade = FMath::Max(0.f, 1.f - Age / 0.35f);
		Flash->SetIntensity(FlashLumens * Fade * Fade);
		if (Fade <= 0.f && Flash->IsVisible()) { Flash->SetVisibility(false); }
	}
	if (Age >= Life)
	{
		Destroy();
	}
}

void ATN_ScoreShellBurst::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	TNAmbientFX::RemoveOwner(this);
	Super::EndPlay(EndPlayReason);
}
