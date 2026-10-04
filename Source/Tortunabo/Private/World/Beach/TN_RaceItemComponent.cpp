#include "World/Beach/TN_RaceItemComponent.h"
#include "TN_BeachEnemyKit.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "World/Beach/TN_BeachRaceGenerator.h"
#include "World/Beach/TN_BeachStun.h"
#include "World/Beach/TN_RaceBurstFX.h"
#include "World/Beach/TN_RaceItems.h"
#include "Components/PointLightComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Player/TN_TurtleMovementComponent.h"
#include "Player/TortugaCharacter.h"
#include "Settings/TN_CombatTuning.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto del
// bloque.
namespace TNRaceItemComponentDetail
{
	/** Empujón extra al campo de visión de la tortuga local con el turbo (grados). */
	constexpr float FovKickDegrees = 16.f;
	constexpr float FovKickSpeed = 40.f;

	/** Cada cuánto (s) mira el protector solar a quién toca. */
	constexpr float StarScanPeriod = 0.08f;
	/** Tras derribar o marear a algo, el protector no le vuelve a dar durante estos segundos. */
	constexpr double StarHitCooldown = 2.5;
	/** Empujón del derribo del protector: hacia fuera y hacia arriba (cm/s). */
	constexpr double StarKnockPush = 650.0;
	constexpr double StarKnockLift = 320.0;

	FLinearColor TurboLight() { return FLinearColor(1.f, 0.72f, 0.3f); }
	FLinearColor GoldLight() { return FLinearColor(1.f, 0.85f, 0.25f); }
	FLinearColor StarLight() { return FLinearColor(1.f, 0.92f, 0.5f); }
}

UTN_RaceItemComponent::UTN_RaceItemComponent()
{
	// El tick solo hace falta mientras haya un efecto o algo que apagar; se enciende con ApplyEffects.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

void UTN_RaceItemComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UTN_RaceItemComponent, Effects);
}

UTN_RaceItemComponent* UTN_RaceItemComponent::FindOn(const AActor* Turtle)
{
	return Turtle ? Turtle->FindComponentByClass<UTN_RaceItemComponent>() : nullptr;
}

UTN_RaceItemComponent* UTN_RaceItemComponent::FindOrAddOn(ACharacter* Turtle)
{
	if (UTN_RaceItemComponent* Existing = FindOn(Turtle))
	{
		return Existing;
	}
	if (!Turtle || !Turtle->HasAuthority())
	{
		return nullptr;
	}
	// Componente dinámico replicado: el servidor lo crea y cada cliente recibe el suyo.
	UTN_RaceItemComponent* Comp = NewObject<UTN_RaceItemComponent>(Turtle, UTN_RaceItemComponent::StaticClass(), TEXT("RaceItems"));
	if (!Comp)
	{
		return nullptr;
	}
	Turtle->AddInstanceComponent(Comp);
	Comp->RegisterComponent();
	return Comp;
}

double UTN_RaceItemComponent::Now() const
{
	return TNRaceItems::ServerNow(GetWorld());
}

// ─────────────────────────────────────────────────────────────────────────────
// Estado
// ─────────────────────────────────────────────────────────────────────────────

bool UTN_RaceItemComponent::IsBoosting() const
{
	return Effects.BoostEnd > 0.f && Now() < static_cast<double>(Effects.BoostEnd);
}

bool UTN_RaceItemComponent::IsGoldenBoosting() const
{
	return Effects.bGolden && IsBoosting();
}

bool UTN_RaceItemComponent::HasStar() const
{
	return Effects.StarEnd > 0.f && Now() < static_cast<double>(Effects.StarEnd);
}

float UTN_RaceItemComponent::GetSpeedMultiplier() const
{
	float Multiplier = 1.f;
	if (IsBoosting())
	{
		Multiplier *= FMath::Max(1.f, Effects.BoostMultiplier);
	}
	if (HasStar())
	{
		Multiplier *= TNRaceItems::StarMultiplier;
	}
	return FMath::Min(Multiplier, TNRaceItems::MaxSpeedMultiplier);
}

float UTN_RaceItemComponent::ResolveOwnerBoostMultiplier() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerState* PlayerState = Pawn ? Pawn->GetPlayerState() : nullptr;
	const float RoundTrip = PlayerState ? PlayerState->GetPingInMilliseconds() * 0.001f : 0.f;
	const double SinceRecent = RecentSpeedTime >= 0.0 ? Now() - RecentSpeedTime : -1.0;
	return TNRaceItems::ResolveClaimedBoost(GetSpeedMultiplier(), RecentSpeedMultiplier, SinceRecent, TNRaceItems::BoostGraceSeconds(RoundTrip));
}

void UTN_RaceItemComponent::NoteRecentSpeed()
{
	const AActor* Owner = GetOwner();
	const float Multiplier = GetSpeedMultiplier();
	if (Owner && Owner->HasAuthority() && Multiplier > 1.f)
	{
		RecentSpeedMultiplier = Multiplier;
		RecentSpeedTime = Now();
	}
}

float UTN_RaceItemComponent::GetBoostSecondsLeft() const
{
	return IsBoosting() ? static_cast<float>(static_cast<double>(Effects.BoostEnd) - Now()) : 0.f;
}

float UTN_RaceItemComponent::GetStarSecondsLeft() const
{
	return HasStar() ? static_cast<float>(static_cast<double>(Effects.StarEnd) - Now()) : 0.f;
}

void UTN_RaceItemComponent::GrantBoost(float Multiplier, float Seconds, bool bGolden)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || Seconds <= 0.f)
	{
		return;
	}
	const bool bWasBoosting = IsBoosting();
	const float NewEnd = static_cast<float>(Now() + static_cast<double>(Seconds));
	Effects.BoostEnd = bWasBoosting ? FMath::Max(Effects.BoostEnd, NewEnd) : NewEnd;
	Effects.BoostMultiplier = FMath::Max(1.f, Multiplier);
	Effects.bGolden = bGolden || (bWasBoosting && Effects.bGolden);
	NoteRound();
	// El servidor no recibe OnRep: lo aplica aquí.
	ApplyEffects();
}

void UTN_RaceItemComponent::GrantStar(float Seconds)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || Seconds <= 0.f)
	{
		return;
	}
	const float NewEnd = static_cast<float>(Now() + static_cast<double>(Seconds));
	Effects.StarEnd = HasStar() ? FMath::Max(Effects.StarEnd, NewEnd) : NewEnd;
	StarHitUntil.Reset();
	NoteRound();
	ApplyEffects();
}

void UTN_RaceItemComponent::SetRiding(bool bInRiding)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || Effects.bRiding == bInRiding)
	{
		return;
	}
	Effects.bRiding = bInRiding;
	ApplyEffects();
}

void UTN_RaceItemComponent::NoteRound()
{
	EffectsRound = -1;
	if (!RoundGenerator.IsValid())
	{
		RoundGenerator = ATN_BeachRaceGenerator::Find(this);
	}
	if (const ATN_BeachRaceGenerator* Generator = RoundGenerator.Get())
	{
		EffectsRound = Generator->GetRoundNumber();
	}
}

void UTN_RaceItemComponent::CancelEffects()
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}
	Effects = FTNRaceEffectState();
	ApplyEffects();
}

void UTN_RaceItemComponent::OnRep_Effects()
{
	ApplyEffects();
}

void UTN_RaceItemComponent::ApplyEffects()
{
	// La velocidad no se toca aquí: el movimiento de la tortuga lee el multiplicador en cada movimiento (el dueño lo guarda
	// en él y el servidor lo valida, issue #22). Antes cada máquina lo ponía en MaxWalkSpeed al recibirlo: el servidor
	// aceleraba medio ping antes que el dueño y frenaba antes al acabar, con una corrección de unos 30 cm cada vez.
	if (const ACharacter* Turtle = Cast<ACharacter>(GetOwner()))
	{
		if (UTN_TurtleMovementComponent* TurtleMove = Cast<UTN_TurtleMovementComponent>(Turtle->GetCharacterMovement()))
		{
			TurtleMove->SetRaceItems(this);
		}
	}
	AppliedMultiplier = GetSpeedMultiplier();
	NoteRecentSpeed();
	SetComponentTickEnabled(true);
}

void UTN_RaceItemComponent::RefreshTickState()
{
	const bool bBusy = IsBoosting() || HasStar() || bShownBoost || bShownStar || bFovSaved || AppliedMultiplier > 1.f + KINDA_SMALL_NUMBER
		|| (bEmittersReady && (TNBeachKit::AnyAlive(Streaks) || TNBeachKit::AnyAlive(Dust) || TNBeachKit::AnyAlive(Sparks)));
	if (!bBusy)
	{
		SetComponentTickEnabled(false);
	}
}

void UTN_RaceItemComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// El movimiento la tiene con un puntero débil: sin el componente, sin turbo.
	AppliedMultiplier = 1.f;
	StopVisuals();
	Super::EndPlay(EndPlayReason);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick
// ─────────────────────────────────────────────────────────────────────────────

void UTN_RaceItemComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	const bool bBoost = IsBoosting();
	const bool bStar = HasStar();

	// Los efectos acaban por tiempo, sin que nadie avise: el multiplicador se recalcula en cada máquina.
	if (!FMath::IsNearlyEqual(GetSpeedMultiplier(), AppliedMultiplier))
	{
		ApplyEffects();
	}
	// Servidor: hasta cuándo ha valido el multiplicador (el margen para los movimientos del dueño que aún lo llevan).
	NoteRecentSpeed();
	if (Owner->HasAuthority() && bStar)
	{
		ServerStarContacts(DeltaTime);
	}
	// Los efectos no pasan a la ronda siguiente (el reparto nuevo cambia el número de ronda del generador).
	if (Owner->HasAuthority() && EffectsRound >= 0)
	{
		RoundCheckClock -= DeltaTime;
		if (RoundCheckClock <= 0.f)
		{
			RoundCheckClock = 0.5f;
			const ATN_BeachRaceGenerator* Generator = RoundGenerator.Get();
			if (Generator && Generator->GetRoundNumber() != EffectsRound)
			{
				EffectsRound = -1;
				CancelEffects();
			}
		}
	}
	if (Owner->GetNetMode() != NM_DedicatedServer)
	{
		TickVisuals(DeltaTime, bBoost, bStar);
	}
	RefreshTickState();
}

// ─────────────────────────────────────────────────────────────────────────────
// Protector solar: lo que toca cae
// ─────────────────────────────────────────────────────────────────────────────

void UTN_RaceItemComponent::ServerStarContacts(float DeltaTime)
{
	using namespace TNRaceItemComponentDetail;
	StarScanClock -= DeltaTime;
	if (StarScanClock > 0.f)
	{
		return;
	}
	StarScanClock = StarScanPeriod;
	// Con la carrera parada («¡TIEMPO!», recuento, título del sprint, podio) lo que toca no cae ni se marea (#72).
	if (!ATN_BeachEnemy::IsRaceLive(this))
	{
		return;
	}
	ATortugaCharacter* Self = Cast<ATortugaCharacter>(GetOwner());
	UWorld* World = GetWorld();
	if (!Self || !World || Self->IsDead())
	{
		return;
	}
	const double WorldNow = World->GetTimeSeconds();
	const FVector Center = Self->GetActorLocation();
	const double ContactSq = FMath::Square(static_cast<double>(TNRaceItems::StarContactRadius));

	// Otras tortugas: derribadas con ragdoll, hacia fuera y hacia arriba.
	TArray<ATortugaCharacter*> Racers;
	ATN_BeachEnemy::GatherTurtles(this, Racers);
	for (ATortugaCharacter* Other : Racers)
	{
		if (!Other || Other == Self || !TNRaceItems::CanBeHurt(Other) || !ATN_BeachEnemy::CanBeHit(Other))
		{
			continue;
		}
		const FVector Delta = Other->GetActorLocation() - Center;
		if (Delta.SizeSquared2D() > ContactSq || FMath::Abs(Delta.Z) > 220.0)
		{
			continue;
		}
		const double* Until = StarHitUntil.Find(TWeakObjectPtr<AActor>(Other));
		if (Until && WorldNow < *Until)
		{
			continue;
		}
		StarHitUntil.Add(TWeakObjectPtr<AActor>(Other), WorldNow + StarHitCooldown);
		FVector Away = Delta.GetSafeNormal2D();
		if (Away.IsNearlyZero())
		{
			Away = Self->GetActorForwardVector();
		}
		TNBeach::KnockDownTurtle(Other, UTN_CombatTuning::Get().StarKnockSeconds, Away * StarKnockPush + FVector(0.0, 0.0, StarKnockLift));
	}

	// Enemigos: mareados con pajaritos (los que se dejan marear).
	for (TActorIterator<ATN_BeachEnemy> It(World); It; ++It)
	{
		ATN_BeachEnemy* Enemy = *It;
		if (!IsValid(Enemy) || !Enemy->AcceptsHitStun() || Enemy->IsHitStunned())
		{
			continue;
		}
		FVector CapsuleA = FVector::ZeroVector;
		FVector CapsuleB = FVector::ZeroVector;
		float CapsuleRadius = 0.f;
		if (!Enemy->GetHitCapsule(CapsuleA, CapsuleB, CapsuleRadius))
		{
			continue;
		}
		const FVector Nearest = FMath::ClosestPointOnSegment(Center, CapsuleA, CapsuleB);
		const double Reach = static_cast<double>(CapsuleRadius) + static_cast<double>(TNRaceItems::StarContactRadius) * 0.5;
		if (FVector::DistSquared(Nearest, Center) > FMath::Square(Reach))
		{
			continue;
		}
		const double* Until = StarHitUntil.Find(TWeakObjectPtr<AActor>(Enemy));
		if (Until && WorldNow < *Until)
		{
			continue;
		}
		StarHitUntil.Add(TWeakObjectPtr<AActor>(Enemy), WorldNow + StarHitCooldown);
		Enemy->ApplyHitStun(UTN_CombatTuning::Get().StarEnemyStunSeconds, Self);
	}

	// De vez en cuando, fuera los que ya no existen.
	if (StarHitUntil.Num() > 24)
	{
		for (auto It = StarHitUntil.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid() || WorldNow >= It.Value())
			{
				It.RemoveCurrent();
			}
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Sonidos y ondas para todas las máquinas
// ─────────────────────────────────────────────────────────────────────────────

UTN_RaceItemSynthComponent* UTN_RaceItemComponent::GetSfx()
{
	AActor* Owner = GetOwner();
	if (!Owner || Owner->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	if (!Sfx)
	{
		Sfx = UTN_RaceItemSynthComponent::AttachTo(Owner, Owner->GetActorLocation(), 1800.f, 14000.f);
	}
	return Sfx;
}

void UTN_RaceItemComponent::MulticastCue_Implementation(ETNRaceSound Sound, float Pitch)
{
	if (UTN_RaceItemSynthComponent* Synth = GetSfx())
	{
		Synth->Play(Sound, Pitch, 1.f);
	}
}

void UTN_RaceItemComponent::MulticastWhistle_Implementation(FVector_NetQuantize10 Center, float Radius)
{
	const FVector At(Center);
	ATN_RaceBurstFX::SpawnLocal(GetWorld(), ETNRaceBurst::WhistleWave, At, Radius);
	if (UTN_RaceItemSynthComponent* Synth = GetSfx())
	{
		Synth->Play(ETNRaceSound::Whistle, 1.f, 1.f);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Visual (máquinas con pantalla)
// ─────────────────────────────────────────────────────────────────────────────

void UTN_RaceItemComponent::EnsureVisuals()
{
	if (bEmittersReady)
	{
		return;
	}
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->GetWorld() || Owner->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	using TNAmbientFX::EShape;
	const uint32 Seed = static_cast<uint32>(GetTypeHash(Owner->GetFName())) | 1u;
	// Rayas de velocidad detrás de la tortuga: alargadas, claras y que se apagan deprisa.
	TNAmbientFX::FEmitterDesc StreakDesc = TNBeachKit::MakeDesc(EShape::Streak, FLinearColor(1.f, 0.97f, 0.88f), true, 0.7f, 48, 70.f, 900.f, 0.f, 0.22f, 0.4f, 90.f, 24.f);
	StreakDesc.SpawnRadius = 45.f;
	StreakDesc.SpawnHeight = 90.f;
	StreakDesc.Spread = 0.25f;
	TNBeachKit::InitEmitter(Streaks, Owner, StreakDesc, Seed + 1u);
	// Arena que levantan las patas.
	TNAmbientFX::FEmitterDesc DustDesc = TNBeachKit::MakeDesc(EShape::Puff, FLinearColor(0.88f, 0.78f, 0.58f), true, 0.5f, 36, 34.f, 260.f, -40.f, 0.5f, 0.9f, 40.f, 130.f);
	DustDesc.SpawnRadius = 40.f;
	DustDesc.Spread = 0.8f;
	TNBeachKit::InitEmitter(Dust, Owner, DustDesc, Seed + 2u);
	// Chispas doradas (el coco dorado y el protector solar).
	TNAmbientFX::FEmitterDesc SparkDesc = TNBeachKit::MakeDesc(EShape::Ember, FLinearColor(1.f, 0.85f, 0.35f), true, 0.9f, 60, 55.f, 130.f, -20.f, 0.6f, 1.2f, 20.f, 4.f);
	SparkDesc.SpawnRadius = 90.f;
	SparkDesc.SpawnHeight = 170.f;
	SparkDesc.Spread = 1.f;
	TNBeachKit::InitEmitter(Sparks, Owner, SparkDesc, Seed + 3u);
	bEmittersReady = true;
}

void UTN_RaceItemComponent::StopVisuals()
{
	if (Glow)
	{
		Glow->DestroyComponent();
		Glow = nullptr;
	}
	// Devolver el campo de visión si se ha tocado.
	if (bFovSaved)
	{
		if (ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(GetOwner()))
		{
			Turtle->SetCameraFOVs(SavedFovDefault, SavedFovSprint);
		}
		bFovSaved = false;
		FovKick = 0.f;
	}
}

void UTN_RaceItemComponent::UpdateFov(float DeltaTime, bool bBoost)
{
	using namespace TNRaceItemComponentDetail;
	ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(GetOwner());
	if (!Turtle || !Turtle->IsLocallyControlled())
	{
		return;
	}
	if (bBoost && !bFovSaved)
	{
		SavedFovDefault = Turtle->GetCameraFOVDefault();
		SavedFovSprint = Turtle->GetCameraFOVSprint();
		bFovSaved = true;
		FovKick = 0.f;
	}
	if (!bFovSaved)
	{
		return;
	}
	FovKick = FMath::FInterpConstantTo(FovKick, bBoost ? FovKickDegrees : 0.f, DeltaTime, FovKickSpeed);
	Turtle->SetCameraFOVs(SavedFovDefault + FovKick, SavedFovSprint + FovKick);
	if (!bBoost && FovKick <= 0.01f)
	{
		Turtle->SetCameraFOVs(SavedFovDefault, SavedFovSprint);
		bFovSaved = false;
		FovKick = 0.f;
	}
}

void UTN_RaceItemComponent::TickVisuals(float DeltaTime, bool bBoost, bool bStar)
{
	using namespace TNRaceItemComponentDetail;
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return;
	}
	const bool bGolden = bBoost && Effects.bGolden;

	// Empieza y acaba (en cada máquina, con el reloj del servidor).
	if (bStar != bShownStar && !bStar)
	{
		if (UTN_RaceItemSynthComponent* Synth = GetSfx())
		{
			Synth->Play(ETNRaceSound::StarDown, 1.f, 1.f);
		}
	}
	bShownBoost = bBoost;
	bShownGolden = bGolden;
	bShownStar = bStar;
	UpdateFov(DeltaTime, bBoost);

	FVector View = FVector::ZeroVector;
	const bool bHasView = TNBeachKit::LocalCamera(World, View);
	if (bBoost || bStar || bEmittersReady)
	{
		EnsureVisuals();
	}
	if (bEmittersReady && bHasView)
	{
		const FVector Location = Owner->GetActorLocation();
		const FVector Forward = Owner->GetActorForwardVector().GetSafeNormal2D();
		Streaks.Origin = Location - Forward * 100.0;
		Streaks.Desc.Direction = -Forward;
		Streaks.RateScale = bBoost ? 1.f : 0.f;
		Dust.Origin = Location - FVector(0.0, 0.0, 70.0) - Forward * 30.0;
		Dust.Desc.Direction = (-Forward + FVector(0.0, 0.0, 0.45)).GetSafeNormal();
		Dust.RateScale = bBoost ? 1.f : 0.f;
		Sparks.Origin = Location - FVector(0.0, 0.0, 60.0);
		Sparks.Desc.Direction = FVector::UpVector;
		Sparks.RateScale = (bStar || bGolden) ? 1.f : 0.f;
		TNBeachKit::TickEmitterIfBusy(Streaks, DeltaTime, View);
		TNBeachKit::TickEmitterIfBusy(Dust, DeltaTime, View);
		TNBeachKit::TickEmitterIfBusy(Sparks, DeltaTime, View);
	}

	// Luz cálida que late mientras dura el turbo o el protector.
	const bool bLit = bBoost || bStar;
	if (bLit && !Glow && Owner->GetRootComponent())
	{
		Glow = NewObject<UPointLightComponent>(Owner, NAME_None, RF_Transient);
		Glow->SetMobility(EComponentMobility::Movable);
		Glow->SetupAttachment(Owner->GetRootComponent());
		Glow->SetRelativeLocation(FVector(0.0, 0.0, 70.0));
		Glow->SetCastShadows(false);
		Glow->SetIntensityUnits(ELightUnits::Lumens);
		Glow->SetAttenuationRadius(900.f);
		Glow->RegisterComponent();
	}
	if (Glow)
	{
		if (bLit)
		{
			PulseClock += DeltaTime;
			const float Pulse = 0.75f + 0.25f * FMath::Sin(PulseClock * (bStar ? 13.f : 9.f));
			Glow->SetLightColor(bStar ? StarLight() : (bGolden ? GoldLight() : TurboLight()));
			Glow->SetIntensity((bStar ? 3200.f : 2200.f) * Pulse);
			Glow->SetVisibility(true);
		}
		else
		{
			Glow->SetVisibility(false);
		}
	}
}
