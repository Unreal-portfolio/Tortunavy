#include "World/Beach/TN_RaceWhirlpool.h"
#include "TN_BeachEnemyKit.h"
#include "TN_BeachRideKit.h"
#include "TN_RaceItemArtExtra.h"
#include "World/Beach/TN_BeachRaceGenerator.h"
#include "World/Beach/TN_BeachTrapStatusComponent.h"
#include "World/Beach/TN_RaceItemRules.h"
#include "World/Beach/TN_RaceItemSynth.h"
#include "World/Beach/TN_RaceItems.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TN_Log.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Player/TN_TurtleMovementComponent.h"
#include "Player/TortugaCharacter.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto del
// bloque.
namespace TNRaceWhirlpoolDetail
{
	/** Del centro de la cápsula de pie a la espalda que se coloca (fracción de media cápsula), como el pelícano taxi. */
	constexpr float GripDropFraction = 0.75f;
	/** Aparece y se va (s). */
	constexpr float GrowSeconds = 0.4f;
	constexpr float ShrinkSeconds = 0.6f;
	/** El agua: altura sobre la arena (cm), aplastado del embudo y giro (grados por segundo). */
	constexpr double WaterLift = 12.0;
	constexpr double WaterFlatten = 0.25;
	constexpr float WaterSpinDeg = 200.f;
	/** Cada cuánto gorgotea (s). */
	constexpr float GurglePeriod = 1.1f;

	inline float Smooth01(float X)
	{
		const float C = FMath::Clamp(X, 0.f, 1.f);
		return C * C * (3.f - 2.f * C);
	}

	inline float StandHalfHeight(const ACharacter* Turtle)
	{
		const UCapsuleComponent* Capsule = Turtle ? Turtle->GetCapsuleComponent() : nullptr;
		return Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.f;
	}
}

ATN_RaceWhirlpool::ATN_RaceWhirlpool()
{
	// Quieto: ni Mover ni raíz animada. Lo de la atrapada es una fórmula del reloj del servidor.
	bUsesMover = false;
	NetFrequencyNear = 10.f;
	SetNetUpdateFrequency(NetFrequencyNear);
}

void ATN_RaceWhirlpool::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_RaceWhirlpool, Catch);
	DOREPLIFETIME(ATN_RaceWhirlpool, DroppedBy);
	DOREPLIFETIME(ATN_RaceWhirlpool, BornTime);
	DOREPLIFETIME(ATN_RaceWhirlpool, EndTime);
}

bool ATN_RaceWhirlpool::ServerDrop(ATortugaCharacter* Turtle)
{
	using namespace TNRaceItemRules;
	UWorld* World = Turtle ? Turtle->GetWorld() : nullptr;
	if (!World || !Turtle->HasAuthority())
	{
		return false;
	}
	int32 Live = 0;
	for (TActorIterator<ATN_RaceWhirlpool> It(World); It; ++It)
	{
		if (IsValid(*It) && !It->IsActorBeingDestroyed() && !It->bServerEnding)
		{
			++Live;
		}
	}
	if (Live >= MaxWhirlpools)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Remolino: ya hay %d en la playa."), Live);
		return false;
	}
	FVector Forward = Turtle->GetActorForwardVector().GetSafeNormal2D();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::ForwardVector;
	}
	const FVector Behind = Turtle->GetActorLocation() - Forward * static_cast<double>(WhirlDropBehind);
	const float HalfHeight = TNRaceWhirlpoolDetail::StandHalfHeight(Turtle);
	float GroundZ = static_cast<float>(Turtle->GetActorLocation().Z) - HalfHeight;
	TraceGround(Turtle, Behind, GroundZ, nullptr, 400.f, 1500.f);
	const FTransform SpawnXf(FRotator(0.f, Turtle->GetActorRotation().Yaw, 0.f), FVector(Behind.X, Behind.Y, GroundZ));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	ATN_RaceWhirlpool* Whirl = World->SpawnActor<ATN_RaceWhirlpool>(StaticClass(), SpawnXf, Params);
	if (!Whirl)
	{
		return false;
	}
	Whirl->DroppedBy = Turtle;
	Whirl->FinishSpawning(SpawnXf);
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] %s deja un remolino detrás en (%.1f, %.1f) m."), *GetNameSafe(Turtle), Behind.X / 100.0, Behind.Y / 100.0);
	return true;
}

void ATN_RaceWhirlpool::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		BornTime = static_cast<float>(ServerNow(this));
		if (const ATN_BeachRaceGenerator* RaceGenerator = ATN_BeachRaceGenerator::Find(this))
		{
			SpawnRound = RaceGenerator->GetRoundNumber();
		}
	}
}

void ATN_RaceWhirlpool::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority() && Catch.bActive)
	{
		ServerDropCatch(TEXT("el remolino desaparece"));
	}
	if (bDizzyOn)
	{
		TNBeachRideKit::SetDizzyBirds(DizzyVictim.Get(), false);
		bDizzyOn = false;
	}
	// La base la suelta del todo (EndHoldTurtle).
	Super::EndPlay(EndPlayReason);
}

double ATN_RaceWhirlpool::GetMaxHoldSeconds() const
{
	return static_cast<double>(TNRaceItemRules::WhirlPullSeconds) + 2.0;
}

float ATN_RaceWhirlpool::CatchAge() const
{
	return static_cast<float>(ServerNow(this) - static_cast<double>(Catch.StartTime));
}

void ATN_RaceWhirlpool::OnRep_Catch()
{
	if (HasActorBegunPlay())
	{
		SyncHold();
	}
}

void ATN_RaceWhirlpool::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Después de la base (ServerTick decide la suelta) y del movimiento de la tortuga: la atrapada, en la espiral.
	SyncHold();
}

// ─────────────────────────────────────────────────────────────────────────────
// Sujeción (todas las máquinas)
// ─────────────────────────────────────────────────────────────────────────────

void ATN_RaceWhirlpool::SyncHold()
{
	using namespace TNRaceItemRules;
	ATortugaCharacter* Victim = Catch.Victim;
	const float T = CatchAge();
	ATortugaCharacter* Want = nullptr;
	// En el servidor, mientras no la suelte (ServerTick); en los clientes, además, hasta su hora aunque aún no haya llegado la
	// suelta. Una vez soltada, esta máquina no la vuelve a coger.
	if (Catch.bActive && Catch.Serial != LocalDoneSerial && IsValid(Victim) && !Victim->IsDead() && !Victim->IsActorBeingDestroyed()
		&& !Victim->IsInShell() && !Victim->IsKnockedDown() && (HasAuthority() || T < WhirlPullSeconds))
	{
		Want = Victim;
	}
	ATortugaCharacter* Held = GetHeldTurtle();
	if (Held && Held != Want)
	{
		// En un cliente, la suelta en el centro como el servidor (a su hora o al llegar la suelta normal): sin corrección.
		const bool bNormalRelease = !Catch.bActive && Catch.ReleaseTime - Catch.StartTime >= WhirlPullSeconds - 0.05f;
		if (!HasAuthority() && Held == Victim && (T >= WhirlPullSeconds || bNormalRelease))
		{
			PutVictimAtCenter(Held);
		}
		EndHoldTurtle();
		LocalDoneSerial = Catch.Serial;
		Held = nullptr;
	}
	if (!Want)
	{
		return;
	}
	if (!Held)
	{
		BeginHoldTurtle(Want);
	}
	if (GetHeldTurtle() != Want)
	{
		if (HasAuthority())
		{
			ServerDropCatch(TEXT("no se ha podido sujetar"));
		}
		return;
	}
	const float HalfHeight = TNRaceWhirlpoolDetail::StandHalfHeight(Want);
	const float Alpha = FMath::Clamp(T / WhirlPullSeconds, 0.f, 1.f);
	const FVector Offset = WhirlOffset(FVector(Catch.Entry), T, WhirlPullSeconds, WhirlTurnsPerSecond);
	const FVector Grip = GetActorLocation() + Offset
		+ FVector(0.0, 0.0, HalfHeight * (1.f + TNRaceWhirlpoolDetail::GripDropFraction) - WhirlSink * Alpha);
	PlaceHeldTurtle(Grip, WhirlSpinYaw(Catch.EntryYaw, T, WhirlPullSeconds, WhirlTurnsPerSecond));
}

void ATN_RaceWhirlpool::PutVictimAtCenter(ATortugaCharacter* Victim) const
{
	if (!IsValid(Victim))
	{
		return;
	}
	const float HalfHeight = TNRaceWhirlpoolDetail::StandHalfHeight(Victim);
	Victim->SetActorLocationAndRotation(GetActorLocation() + FVector(0.0, 0.0, HalfHeight + 5.f), FRotator(0.f, Victim->GetActorRotation().Yaw, 0.f),
		false, nullptr, ETeleportType::TeleportPhysics);
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor
// ─────────────────────────────────────────────────────────────────────────────

void ATN_RaceWhirlpool::ServerTick(float DeltaSeconds)
{
	using namespace TNRaceItemRules;
	if (bServerEnding)
	{
		return;
	}
	const double Age = ServerNow(this) - static_cast<double>(BornTime);
	const ATN_BeachRaceGenerator* RaceGenerator = SpawnRound >= 0 ? ATN_BeachRaceGenerator::Find(this) : nullptr;
	if (Age >= static_cast<double>(WhirlLifeSeconds) || (RaceGenerator && RaceGenerator->GetRoundNumber() != SpawnRound))
	{
		ServerEnd();
		return;
	}
	if (Catch.bActive)
	{
		ATortugaCharacter* Victim = Catch.Victim;
		if (!IsValid(Victim) || Victim->IsDead() || Victim->IsInShell() || Victim->IsKnockedDown())
		{
			ServerDropCatch(TEXT("ya no se puede sujetar"));
		}
		else if (CatchAge() >= WhirlPullSeconds)
		{
			ServerRelease();
		}
		return;
	}
	ServerTryCatch();
}

void ATN_RaceWhirlpool::ServerTryCatch()
{
	using namespace TNRaceItemRules;
	const double Now = ServerNow(this);
	TArray<ATortugaCharacter*> Racers;
	GatherTurtles(this, Racers);
	const FVector Center = GetActorLocation();
	for (ATortugaCharacter* Other : Racers)
	{
		if (!IsValid(Other))
		{
			continue;
		}
		FWhirlCatchView View;
		const FVector Delta = Other->GetActorLocation() - Center;
		View.Distance2D = static_cast<float>(Delta.Size2D());
		View.HeightGap = static_cast<float>(Delta.Z) - TNRaceWhirlpoolDetail::StandHalfHeight(Other);
		View.bIsOwner = Other == DroppedBy;
		View.WhirlAge = static_cast<float>(Now - static_cast<double>(BornTime));
		if (const double* Released = ReleasedAt.Find(TWeakObjectPtr<ATortugaCharacter>(Other)))
		{
			View.SinceReleased = static_cast<float>(Now - *Released);
		}
		// El protector solar lo atraviesa; ni aturdida, ni derribada, ni en el caparazón, ni en brazos ni en un pico.
		View.bCanBeHit = TNRaceItems::CanBeHurt(Other) && TNRaceItems::CanUseNow(Other);
		View.bRaceLive = IsRaceLive(this);
		if (!CanWhirlCatch(View))
		{
			continue;
		}
		Catch.Victim = Other;
		Catch.Entry = FVector_NetQuantize10(Delta.X, Delta.Y, 0.0);
		Catch.EntryYaw = static_cast<float>(Other->GetActorRotation().Yaw);
		Catch.StartTime = static_cast<float>(Now);
		Catch.ReleaseTime = 0.f;
		Catch.bActive = true;
		++Catch.Serial;
		if (Catch.Serial == 0)
		{
			Catch.Serial = 1;
		}
		SyncHold();
		if (GetHeldTurtle() != Other)
		{
			return;
		}
		ForceNetUpdate();
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] El remolino de %s atrapa a %s."), *GetNameSafe(DroppedBy), *GetNameSafe(Other));
		return;
	}
}

void ATN_RaceWhirlpool::ServerRelease()
{
	using namespace TNRaceItemRules;
	ATortugaCharacter* Victim = Catch.Victim;
	const double Now = ServerNow(this);
	Catch.bActive = false;
	Catch.ReleaseTime = static_cast<float>(Now);
	LocalDoneSerial = Catch.Serial;
	if (ATortugaCharacter* Held = GetHeldTurtle())
	{
		if (Held == Victim)
		{
			PutVictimAtCenter(Held);
		}
		EndHoldTurtle();
	}
	if (IsValid(Victim) && !Victim->IsDead())
	{
		// Sale lanzada por donde la llevaba el agua (la tangente de la espiral al acabar) y mareada.
		const double EndAngle = FMath::Atan2(Catch.Entry.Y, Catch.Entry.X) + 2.0 * PI * WhirlTurnsPerSecond * WhirlPullSeconds;
		const FVector Out(-FMath::Sin(EndAngle), FMath::Cos(EndAngle), 0.0);
		UTN_TurtleMovementComponent::LaunchFromServer(Victim, Out * WhirlFlingOut + FVector(0.0, 0.0, WhirlFlingUp));
		Victim->SetFallImmuneUntilLanded();
		if (UTN_BeachTrapStatusComponent* Status = UTN_BeachTrapStatusComponent::FindOrAddTo(Victim))
		{
			Status->ServerSlow(WhirlDizzySpeedFactor, WhirlDizzySeconds);
		}
		ReleasedAt.Add(TWeakObjectPtr<ATortugaCharacter>(Victim), Now);
		Victim->ForceNetUpdate();
	}
	ForceNetUpdate();
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] El remolino suelta a %s mareada."), *GetNameSafe(Victim));
}

void ATN_RaceWhirlpool::ServerDropCatch(const TCHAR* Reason)
{
	if (!HasAuthority() || !Catch.bActive)
	{
		return;
	}
	ATortugaCharacter* Victim = Catch.Victim;
	const double Now = ServerNow(this);
	Catch.bActive = false;
	Catch.ReleaseTime = static_cast<float>(Now);
	LocalDoneSerial = Catch.Serial;
	if (GetHeldTurtle())
	{
		EndHoldTurtle();
	}
	if (IsValid(Victim))
	{
		ReleasedAt.Add(TWeakObjectPtr<ATortugaCharacter>(Victim), Now);
		if (!Victim->IsDead() && !Victim->IsInShell() && !Victim->IsKnockedDown())
		{
			Victim->SetFallImmuneUntilLanded();
		}
	}
	ForceNetUpdate();
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] El remolino deja a %s (%s)."), *GetNameSafe(Victim), Reason ? Reason : TEXT("sin motivo"));
}

void ATN_RaceWhirlpool::OnHoldAborted(ATortugaCharacter* Turtle)
{
	// Se la quitan (red de seguridad, rescate, gusano): la base la suelta justo después. No se la lanza ni se la marea.
	if (!HasAuthority() || !Catch.bActive || Catch.Victim != Turtle)
	{
		return;
	}
	Catch.bActive = false;
	Catch.ReleaseTime = static_cast<float>(ServerNow(this));
	LocalDoneSerial = Catch.Serial;
	ReleasedAt.Add(TWeakObjectPtr<ATortugaCharacter>(Turtle), ServerNow(this));
	ForceNetUpdate();
}

void ATN_RaceWhirlpool::ServerEnd()
{
	if (!HasAuthority() || bServerEnding)
	{
		return;
	}
	if (Catch.bActive)
	{
		ServerRelease();
	}
	bServerEnding = true;
	EndTime = static_cast<float>(ServerNow(this));
	SetLifeSpan(TNRaceWhirlpoolDetail::ShrinkSeconds + 0.4f);
	ForceNetUpdate();
}

// ─────────────────────────────────────────────────────────────────────────────
// Visual
// ─────────────────────────────────────────────────────────────────────────────

void ATN_RaceWhirlpool::BuildVisuals()
{
	using namespace TNRaceItemArtExtra;
	if (bVisualsBuilt || !bHasScreen)
	{
		return;
	}
	bVisualsBuilt = true;
	USceneComponent* Root = GetRootComponent();
	if (!Root)
	{
		return;
	}
	WaterMesh = TNBeachKit::AddPart(this, Root, GetRidePiece(ERidePiece::WhirlWater), FVector(0.0, 0.0, TNRaceWhirlpoolDetail::WaterLift), false);
	FoamMesh = TNBeachKit::AddPart(this, Root, GetRidePiece(ERidePiece::WhirlFoam), FVector(0.0, 0.0, TNRaceWhirlpoolDetail::WaterLift + 1.0), false);
	using TNAmbientFX::EShape;
	TNAmbientFX::FEmitterDesc SplashDesc = TNBeachKit::MakeDesc(EShape::Drop, FLinearColor(0.85f, 0.95f, 1.f), true, 0.8f, 50, 25.f, 380.f, -900.f, 0.4f, 0.8f, 8.f, 3.f);
	SplashDesc.SpawnRadius = TNRaceItemRules::WhirlRadius;
	SplashDesc.Spread = 0.5f;
	TNBeachKit::InitEmitter(Splash, this, SplashDesc, static_cast<uint32>(GetTypeHash(GetFName())) | 1u);
	Sfx = UTN_RaceItemSynthComponent::AttachTo(this, GetActorLocation(), 1200.f, 6000.f);
	if (Sfx)
	{
		Sfx->Play(ETNRaceSound::Gurgle, 1.f, 1.f);
	}
}

void ATN_RaceWhirlpool::VisualTick(float DeltaSeconds)
{
	using namespace TNRaceWhirlpoolDetail;
	BuildVisuals();
	const double Now = ServerNow(this);
	const float Age = static_cast<float>(Now - static_cast<double>(BornTime));
	const float Ending = EndTime > 0.f ? static_cast<float>(Now - static_cast<double>(EndTime)) : -1.f;
	const float Scale = Smooth01(Age / GrowSeconds) * (Ending >= 0.f ? 1.f - Smooth01(Ending / ShrinkSeconds) : 1.f);
	SpinAngle = FMath::Fmod(SpinAngle + WaterSpinDeg * DeltaSeconds * (Catch.bActive ? 1.6f : 1.f), 360.f);
	if (WaterMesh)
	{
		WaterMesh->SetRelativeRotation(FRotator(0.f, SpinAngle, 0.f));
		WaterMesh->SetRelativeScale3D(FVector(Scale, Scale, FMath::Max(0.01, Scale * WaterFlatten)));
	}
	if (FoamMesh)
	{
		FoamMesh->SetRelativeRotation(FRotator(0.f, -0.5f * SpinAngle, 0.f));
		FoamMesh->SetRelativeScale3D(FVector(Scale, Scale, FMath::Max(0.01f, Scale)));
	}
	Splash.Origin = GetActorLocation() + FVector(0.0, 0.0, WaterLift);
	Splash.Desc.Direction = FVector::UpVector;
	Splash.RateScale = Scale * (Catch.bActive ? 3.f : 1.f);
	FVector View = GetActorLocation();
	if (TNBeachKit::LocalCamera(GetWorld(), View))
	{
		TNBeachKit::TickEmitterIfBusy(Splash, DeltaSeconds, View);
	}
	// Gorgoteo de fondo y el chapuzón al atrapar.
	GurgleTimer -= DeltaSeconds;
	if (Sfx && GurgleTimer <= 0.f && Scale > 0.2f)
	{
		GurgleTimer = GurglePeriod;
		Sfx->Play(ETNRaceSound::Gurgle, Catch.bActive ? 0.8f : 1.f, Catch.bActive ? 1.2f : 0.7f);
	}
	if (Sfx && Catch.bActive && Catch.Serial != CaughtSoundSerial)
	{
		CaughtSoundSerial = Catch.Serial;
		Sfx->Play(ETNRaceSound::Splat, 0.7f, 1.f);
	}
	// Pajaritos del mareo a la que acaba de salir.
	ATortugaCharacter* Released = Catch.Victim;
	const float SinceRelease = (!Catch.bActive && Catch.ReleaseTime > 0.f) ? static_cast<float>(Now - static_cast<double>(Catch.ReleaseTime)) : -1.f;
	if (Released && SinceRelease >= 0.f && SinceRelease < TNRaceItemRules::WhirlDizzySeconds && DizzyShownSerial != Catch.Serial)
	{
		DizzyShownSerial = Catch.Serial;
		DizzyVictim = Released;
		bDizzyOn = true;
		TNBeachRideKit::SetDizzyBirds(Released, true);
	}
	if (bDizzyOn && (Catch.bActive || SinceRelease < 0.f || SinceRelease >= TNRaceItemRules::WhirlDizzySeconds))
	{
		TNBeachRideKit::SetDizzyBirds(DizzyVictim.Get(), false);
		bDizzyOn = false;
	}
}
