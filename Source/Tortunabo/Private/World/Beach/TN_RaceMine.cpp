#include "World/Beach/TN_RaceMine.h"
#include "Game/TN_SurvivalHits.h"
#include "World/Beach/TN_BeachCameraShake.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "World/Beach/TN_BeachMineSynth.h"
#include "World/Beach/TN_BeachRaceGenerator.h"
#include "World/Beach/TN_BeachStun.h"
#include "World/Beach/TN_RaceItems.h"
#include "World/Beach/TN_RaceMineFlight.h"
#include "TN_BeachEnemyKit.h"
#include "TN_RaceItemArt.h"
#include "../../Lobby/Playground/TN_PlaygroundMeshKit.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TN_Log.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Player/TortugaCharacter.h"
#include "Settings/TN_CombatTuning.h"
#include "Templates/Function.h"
#include "UObject/Package.h"

/**
 * Ajustes y piezas de la mina de arena lanzable. Con nombre (no anónimo): en la compilación por bloques (unity) los nombres
 * de un espacio anónimo se ven en el resto del bloque.
 */
namespace TNRaceMineDetail
{
	using FMineBuffers = TNPlaygroundKit::FBuffers;

	// ── Fases (Phase, replicada) ──
	constexpr uint8 PhaseFlying = 0;
	constexpr uint8 PhaseSettled = 1;
	constexpr uint8 PhaseArmed = 2;
	constexpr uint8 PhaseFuse = 3;
	constexpr uint8 PhaseExploded = 4;

	// ── Lanzamiento y vuelo ──
	/** Tope de minas a la vez en el mundo. */
	constexpr int32 MaxInWorld = 12;
	/** Velocidad de salida (cm/s) y sitio de salida respecto a la tortuga (cm, hacia delante y hacia arriba). */
	constexpr double ThrowSpeed = 1500.0;
	constexpr double SpawnForward = 120.0;
	constexpr double SpawnUp = 60.0;
	/** Radio con el que toca el suelo (cm). El resto del vuelo (gravedad, rebote, rodar) está en TN_RaceMineFlight.h. */
	using TNRaceMineFlight::MineRadius;
	/** Si a los 6 s aún no ha parado, se queda donde esté. */
	constexpr double MaxFlightSeconds = 6.0;

	// ── Armada y disparo ──
	constexpr float ArmDelaySeconds = 0.9f;
	/** Quien la lanzó no la dispara hasta pasados estos segundos de estar armada. */
	constexpr float OwnerGraceSeconds = 1.5f;
	/** Explota sola a los 10 s de armada. */
	constexpr float SelfDestructSeconds = 10.f;
	constexpr float FuseSeconds = 0.35f;
	/** Cada cuánto (s) mira quién anda cerca. */
	constexpr float ScanPeriod = 0.06f;
	/** Una tortuga la dispara a menos de esto en planta y en vertical (cm). */
	constexpr double TriggerRadius = 300.0;
	constexpr double TriggerHeight = 220.0;
	/** Un enemigo la dispara a menos de su radio más esto (cm) de su eje. */
	constexpr double EnemyTriggerMargin = 250.0;

	// ── Explosión ──
	/** Radio (cm) en planta y altura (cm) en que aturde en bola. Quien la disparó, con el doble de radio. */
	constexpr double BlastRadius = 550.0;
	constexpr double BlastHeight = 450.0;
	constexpr double CulpritReachFactor = 2.0;
	/** Lanzamiento de las aturdidas: hacia fuera (cm/s), hacia arriba (cm/s) y un poco hacia atrás en la carrera (cm/s). */
	constexpr double LaunchOut = 420.0;
	constexpr double LaunchUp = 950.0;
	constexpr double LaunchBackBias = 150.0;
	/** Enemigos a menos de esto de su cuerpo (cm) quedan mareados (segundos en UTN_CombatTuning). */
	constexpr double EnemyBlastGap = 1300.0;
	/** Segundos que sigue el actor tras explotar (cráter, humo, sonidos). */
	constexpr float LingerSeconds = 3.5f;

	// ── Aspecto ──
	/** La malla del arte mide unos 30 cm: se escala a esto. */
	constexpr double ArtScale = 2.6;
	/** Del centro a la base de la malla de respaldo (cm). */
	constexpr double FallbackRestHalf = 14.0;
	/** Vueltas en el aire (grados/s) y rapidez con que se posa. */
	constexpr double TumblePitchRate = 540.0;
	constexpr double TumbleYawRate = 230.0;
	constexpr float RestBlendSpeed = 8.f;
	/** Latido de la luz: sin armar va de ArmBlinkStartHz a ArmBlinkEndHz; armada da un destello corto por segundo (más deprisa
	 * los últimos WarnSeconds) y en la mecha late a FuseBlinkHz. */
	constexpr double ArmBlinkStartHz = 3.0;
	constexpr double ArmBlinkEndHz = 14.0;
	constexpr double ArmedBlinkHz = 1.0;
	constexpr double ArmedFlashDuty = 0.14;
	constexpr double WarnBlinkHz = 4.0;
	constexpr double WarnSeconds = 3.0;
	constexpr double FuseBlinkHz = 12.0;
	/** Altura del halo sobre el centro (cm). */
	constexpr double HaloLift = 24.0;
	/** Luz roja del piloto: solo si la cámara está a menos de LedLightRange (cm). */
	constexpr float LedLightIntensity = 3000.f;
	constexpr float LedLightRadius = 420.f;
	constexpr float LedLightRange = 4000.f;
	/** Fogonazo: segundos de luz, intensidad, alcance y tamaño de la bola (radio 100 cm por este factor). */
	constexpr float FlashLightSeconds = 0.3f;
	constexpr float FlashLightIntensity = 70000.f;
	constexpr float FlashLightRadius = 1800.f;
	constexpr double FlashScale = 3.0;
	/** Cráter: altura sobre la arena (cm). */
	constexpr double CraterLift = 5.0;

	/** Claves de las mallas compartidas. */
	constexpr int32 KeyFallbackMine = 1;
	constexpr int32 KeyCrater = 2;
	constexpr int32 KeyFlash = 3;

	/**
	 * Malla compartida por clave: se construye la primera vez (en el paquete transitorio) y la mantienen viva los componentes
	 * que la usan; si el recolector la suelta entre rondas, se vuelve a construir.
	 */
	UStaticMesh* SharedMesh(int32 Key, TFunctionRef<void(FMineBuffers&)> Build)
	{
		static TMap<int32, TWeakObjectPtr<UStaticMesh>> Cache;
		TWeakObjectPtr<UStaticMesh>& Entry = Cache.FindOrAdd(Key);
		if (UStaticMesh* Found = Entry.Get())
		{
			return Found;
		}
		FMineBuffers Buffers;
		Build(Buffers);
		UStaticMesh* Mesh = Buffers.IsEmpty() ? nullptr : TNPlaygroundKit::BuildMesh(GetTransientPackage(), Buffers, TNPlaygroundKit::VertexColorMaterial());
		Entry = Mesh;
		return Mesh;
	}

	/** Mina de respaldo (si el arte no da malla): plato verde oliva de 78 cm con franja amarilla, plato de presión y pincho. */
	void BuildFallbackMine(FMineBuffers& Buffers)
	{
		const FLinearColor Body = TNPlaygroundKit::Rgb(0x5E6B38, 0.1f);
		const FLinearColor BodyDark = TNPlaygroundKit::Rgb(0x46512A, 0.1f);
		const FLinearColor Plate = TNPlaygroundKit::Rgb(0x6E7B44, 0.1f);
		const FLinearColor Band = TNPlaygroundKit::Rgb(0xE8D27A, 0.1f);
		const FLinearColor Prong = TNPlaygroundKit::Rgb(0x9AA0A6, 0.7f);
		TNPlaygroundKit::AddFrustum(Buffers, FVector(0.0, 0.0, -14.0), FVector(0.0, 0.0, 10.0), 39.0, 36.0, 18, Body, BodyDark, true, false);
		TNPlaygroundKit::AddFrustum(Buffers, FVector(0.0, 0.0, -5.0), FVector(0.0, 0.0, 1.0), 39.5, 39.5, 18, Band, Band, false, false);
		TNPlaygroundKit::AddFrustum(Buffers, FVector(0.0, 0.0, 10.0), FVector(0.0, 0.0, 16.0), 26.0, 22.0, 14, Plate, Plate, false, true);
		TNPlaygroundKit::AddFrustum(Buffers, FVector(0.0, 0.0, 15.0), FVector(0.0, 0.0, 20.0), 8.0, 7.0, 8, BodyDark, BodyDark, false, true);
		TNPlaygroundKit::AddRod(Buffers, FVector(0.0, 0.0, 19.0), FVector(0.0, 0.0, 34.0), 3.0, 6, Prong, FVector::ForwardVector);
		TNPlaygroundKit::AddBall(Buffers, FVector(0.0, 0.0, 35.0), 4.5, 6, Prong);
	}

	/** Cráter chamuscado: disco oscuro de 1,1 m con dos coronas de arena quemada. Plano, mirando arriba. */
	void BuildCraterDisc(FMineBuffers& Buffers)
	{
		const FVector Up = FVector::UpVector;
		TNPlaygroundKit::AddDisc(Buffers, FVector::ZeroVector, Up, 110.0, 18, TNPlaygroundKit::Rgb(0x2E2822));
		TNPlaygroundKit::AddAnnulus(Buffers, FVector(0.0, 0.0, -1.0), Up, 110.0, 165.0, 18, TNPlaygroundKit::Rgb(0x4A3E32));
		TNPlaygroundKit::AddAnnulus(Buffers, FVector(0.0, 0.0, -2.0), Up, 165.0, 195.0, 18, TNPlaygroundKit::Rgb(0x8A7050));
	}

	/** Bola del fogonazo (radio 100 cm). */
	void BuildFlashBall(FMineBuffers& Buffers)
	{
		TNPlaygroundKit::AddBall(Buffers, FVector::ZeroVector, 100.0, 10, TNPlaygroundKit::Rgb(0xFFF0A0));
	}

	/** Halo rojo translúcido del piloto (esfera de 40 cm de radio con opacidad en el alfa del vértice). */
	UStaticMesh* HaloBallMesh()
	{
		return TNBeachKit::CachedMesh(TEXT("RaceMine.Halo"), [](TNProcMesh::FTNProcMeshBuffers& Buffers)
		{
			TNPlaygroundKit::AddBall(Buffers, FVector::ZeroVector, 40.0, 10, FLinearColor(1.f, 0.16f, 0.08f, 0.6f));
		}, TNBeachKit::EBeachMeshMat::SoftVertexAlpha);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Lanzamiento
// ─────────────────────────────────────────────────────────────────────────────

ATN_RaceMine::ATN_RaceMine()
{
}

void ATN_RaceMine::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_RaceMine, Phase);
	DOREPLIFETIME(ATN_RaceMine, PhaseStartTime);
}

bool ATN_RaceMine::ServerThrow(ATortugaCharacter* Turtle, const FVector& Direction)
{
	using namespace TNRaceMineDetail;
	UWorld* World = Turtle ? Turtle->GetWorld() : nullptr;
	if (!World || !Turtle->HasAuthority())
	{
		return false;
	}
	if (CountOf(World, StaticClass()) >= MaxInWorld)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] %s no puede lanzar una mina: ya hay demasiadas en el mundo."), *GetNameSafe(Turtle));
		return false;
	}
	FVector Aim = Direction.GetSafeNormal();
	if (Aim.IsNearlyZero())
	{
		Aim = Turtle->GetActorForwardVector();
	}
	FVector Flat = Aim.GetSafeNormal2D();
	if (Flat.IsNearlyZero())
	{
		Flat = Turtle->GetActorForwardVector().GetSafeNormal2D();
		if (Flat.IsNearlyZero())
		{
			Flat = FVector::ForwardVector;
		}
	}
	const FVector Start = Turtle->GetActorLocation() + Flat * SpawnForward + FVector(0.0, 0.0, SpawnUp);
	// Al punto del centro de la pantalla, con la gravedad de la mina.
	if (Turtle->UsesCameraThrowAim())
	{
		Aim = Turtle->GetThrowDirectionToCrosshair(Start, Turtle->GetTurtleAimRotation(), static_cast<float>(ThrowSpeed), static_cast<float>(TNRaceMineFlight::GravityCm));
	}
	const FTransform SpawnXf(FRotator(0.0, Flat.Rotation().Yaw, 0.0), Start);
	ATN_RaceMine* Mine = World->SpawnActorDeferred<ATN_RaceMine>(StaticClass(), SpawnXf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Mine)
	{
		return false;
	}
	// Antes de FinishSpawning: el actor se replica con quien lo lanzó.
	Mine->SetOwnerTurtle(Turtle);
	Mine->Pos = Start;
	Mine->Vel = Aim * ThrowSpeed;
	Mine->FinishSpawning(SpawnXf);
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] %s lanza una mina."), *GetNameSafe(Turtle));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor
// ─────────────────────────────────────────────────────────────────────────────

void ATN_RaceMine::SetPhase(uint8 NewPhase)
{
	Phase = NewPhase;
	PhaseClock = 0.f;
	PhaseStartTime = static_cast<float>(ServerNow());
	ForceNetUpdate();
	// El servidor no recibe OnRep: lo aplica aquí.
	OnRep_Phase();
}

void ATN_RaceMine::ServerTick(float DeltaSeconds)
{
	using namespace TNRaceMineDetail;
	switch (Phase)
	{
	case PhaseFlying:
		StepFlight(DeltaSeconds);
		break;
	case PhaseSettled:
		// Con la carrera parada («¡TIEMPO!», recuento, podio) el reloj de la mina no corre.
		if (ATN_BeachEnemy::IsRaceLive(this))
		{
			PhaseClock += DeltaSeconds;
			if (PhaseClock >= ArmDelaySeconds)
			{
				SetPhase(PhaseArmed);
			}
		}
		break;
	case PhaseArmed:
		if (ATN_BeachEnemy::IsRaceLive(this))
		{
			PhaseClock += DeltaSeconds;
			if (PhaseClock >= SelfDestructSeconds)
			{
				StartFuse(nullptr);
				break;
			}
			ScanClock -= DeltaSeconds;
			if (ScanClock <= 0.f)
			{
				ScanClock = ScanPeriod;
				ATortugaCharacter* Culprit = nullptr;
				if (ScanForTrigger(Culprit))
				{
					StartFuse(Culprit);
				}
			}
		}
		break;
	case PhaseFuse:
		PhaseClock += DeltaSeconds;
		if (PhaseClock >= FuseSeconds)
		{
			Explode();
		}
		break;
	default:
		break;
	}
}

void ATN_RaceMine::StepFlight(float DeltaSeconds)
{
	using namespace TNRaceMineDetail;
	TNRaceMineFlight::FState State;
	State.Pos = Pos;
	State.Vel = Vel;
	State.Bounces = Bounces;
	State.bSliding = bSliding;
	const TNRaceMineFlight::FStepResult Result = TNRaceMineFlight::Step(State, static_cast<double>(DeltaSeconds),
		[this](const FVector& Where, double FallbackZ)
		{
			return static_cast<double>(GroundHeightAt(Where, static_cast<float>(FallbackZ)));
		});
	Pos = State.Pos;
	Vel = State.Vel;
	Bounces = State.Bounces;
	bSliding = State.bSliding;
	if (Result.bAtRest)
	{
		Settle(Pos);
		return;
	}
	SetActorLocation(Pos);
	if (GetAge() > MaxFlightSeconds)
	{
		Settle(FVector(Pos.X, Pos.Y, Result.GroundZ));
	}
}

void ATN_RaceMine::Settle(const FVector& Where)
{
	Vel = FVector::ZeroVector;
	Pos = Where;
	bSliding = false;
	SetActorLocation(Pos);
	SetPhase(TNRaceMineDetail::PhaseSettled);
}

bool ATN_RaceMine::ScanForTrigger(ATortugaCharacter*& OutWho) const
{
	using namespace TNRaceMineDetail;
	OutWho = nullptr;
	const FVector Center = GetActorLocation();
	const bool bOwnerGrace = PhaseClock < OwnerGraceSeconds;
	const ATortugaCharacter* Thrower = GetOwnerTurtle();

	// Tortugas vivas, no invulnerables (y que un golpe pueda alcanzar): la de quien la lanzó, solo pasado el margen.
	TArray<ATortugaCharacter*> Racers;
	ATN_BeachEnemy::GatherTurtles(this, Racers);
	for (ATortugaCharacter* Candidate : Racers)
	{
		if (!Candidate || Candidate->IsDead() || (bOwnerGrace && Candidate == Thrower))
		{
			continue;
		}
		if (!TNRaceItems::CanBeHurt(Candidate) || !ATN_BeachEnemy::CanBeHit(Candidate))
		{
			continue;
		}
		const FVector Rel = Candidate->GetActorLocation() - Center;
		if (Rel.Size2D() < TriggerRadius && FMath::Abs(Rel.Z) < TriggerHeight)
		{
			OutWho = Candidate;
			return true;
		}
	}

	// Enemigos que se dejan marear (los quads no: la mina se malgastaría).
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	for (TActorIterator<ATN_BeachEnemy> It(World); It; ++It)
	{
		const ATN_BeachEnemy* Enemy = *It;
		if (!IsValid(Enemy) || !Enemy->AcceptsHitStun())
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
		if (FVector::DistSquared(Nearest, Center) < FMath::Square(static_cast<double>(CapsuleRadius) + EnemyTriggerMargin))
		{
			return true;
		}
	}
	return false;
}

void ATN_RaceMine::StartFuse(ATortugaCharacter* Who)
{
	TriggerTurtle = Who;
	SetPhase(TNRaceMineDetail::PhaseFuse);
}

FVector ATN_RaceMine::GetBackDirection() const
{
	FVector Sea = FVector::ZeroVector;
	if (const ATN_BeachRaceGenerator* Generator = GetGenerator())
	{
		Sea = Generator->GetSeaDirection();
	}
	Sea.Z = 0.0;
	if (Sea.IsNearlyZero())
	{
		Sea = FVector::ForwardVector;
	}
	return -Sea.GetSafeNormal();
}

void ATN_RaceMine::Explode()
{
	using namespace TNRaceMineDetail;
	UWorld* World = GetWorld();
	if (!World)
	{
		ServerFinish(0.2f);
		return;
	}
	const FVector Center = GetActorLocation();
	SetPhase(PhaseExploded);
	const FVector Back = GetBackDirection();
	ATortugaCharacter* Thrower = GetOwnerTurtle();
	const ATortugaCharacter* Culprit = TriggerTurtle.Get();
	int32 Stunned = 0;
	int32 Dizzy = 0;

	// Tortugas: en bola, lanzadas hacia fuera y hacia arriba (un poco hacia atrás en la carrera: a quien va delante no le
	// da un acelerón). Quien la disparó, aunque se haya alejado algo en la mecha.
	TArray<ATortugaCharacter*> Racers;
	ATN_BeachEnemy::GatherTurtles(this, Racers);
	for (ATortugaCharacter* Victim : Racers)
	{
		if (!Victim || Victim->IsDead() || !TNRaceItems::CanBeHurt(Victim) || TNRaceItems::IsRiding(Victim) || !ATN_BeachEnemy::CanBeHit(Victim))
		{
			continue;
		}
		const FVector Rel = Victim->GetActorLocation() - Center;
		const double Reach = Victim == Culprit ? BlastRadius * CulpritReachFactor : BlastRadius;
		if (Rel.Size2D() > Reach || FMath::Abs(Rel.Z) > BlastHeight)
		{
			continue;
		}
		FVector Away = Rel.GetSafeNormal2D();
		if (Away.IsNearlyZero())
		{
			// Justo encima: hacia atrás.
			Away = Back;
		}
		// En Supervivencia, la explosión elimina (#735). A quien la lanzó solo si la ha pisado ella (pasado OwnerGraceSeconds);
		// si solo la pilla la onda, la lanza como siempre.
		if ((Victim != Thrower || Victim == Culprit) && TNSurvivalHits::KillInSurvival(Victim, this))
		{
			++Stunned;
			continue;
		}
		const FVector Launch = Away * LaunchOut + Back * LaunchBackBias + FVector(0.0, 0.0, LaunchUp);
		TNBeach::StunTurtle(Victim, UTN_CombatTuning::Get().MineStunSeconds, Launch);
		++Stunned;
	}

	// Enemigos: mareados con pajaritos si su cuerpo queda cerca.
	for (TActorIterator<ATN_BeachEnemy> It(World); It; ++It)
	{
		ATN_BeachEnemy* Enemy = *It;
		if (!IsValid(Enemy) || !Enemy->AcceptsHitStun())
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
		if (FVector::Dist(Nearest, Center) - static_cast<double>(CapsuleRadius) > EnemyBlastGap)
		{
			continue;
		}
		Enemy->ApplyHitStun(UTN_CombatTuning::Get().MineEnemyStunSeconds, Thrower);
		++Dizzy;
	}
	TriggerTurtle.Reset();
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Mina %s explota: %d tortugas aturdidas, %d enemigos mareados."), *GetName(), Stunned, Dizzy);

	MulticastExplode(FVector_NetQuantize10(Center));
	ServerFinish(LingerSeconds);
}

// ─────────────────────────────────────────────────────────────────────────────
// Réplica y multicast
// ─────────────────────────────────────────────────────────────────────────────

void ATN_RaceMine::OnRep_Phase()
{
	using namespace TNRaceMineDetail;
	// Antes de BeginPlay (la primera réplica) no hay nada que animar: VisualTick lo lee todo de Phase.
	if (!bHasScreen || !HasActorBegunPlay())
	{
		return;
	}
	const double Since = ServerNow() - static_cast<double>(PhaseStartTime);
	// Quien llega tarde ve el estado, sin oír lo que ya pasó.
	const bool bFresh = Since < 0.6;
	switch (Phase)
	{
	case PhaseSettled:
		if (bFresh)
		{
			PlayLandFX();
		}
		break;
	case PhaseArmed:
		if (bFresh)
		{
			if (UTN_BeachMineSynthComponent* Beeper = GetMineVoice())
			{
				Beeper->Play(ETNBeachMineSound::Beep, 1.6f, 0.9f);
			}
		}
		break;
	case PhaseFuse:
		BeepsPlayed = bFresh ? 0 : 1000;
		break;
	case PhaseExploded:
		HideBody();
		if (bFresh)
		{
			PlayExplosionFX(GetActorLocation());
		}
		break;
	default:
		break;
	}
}

void ATN_RaceMine::MulticastExplode_Implementation(FVector_NetQuantize10 Where)
{
	PlayExplosionFX(Where);
}

void ATN_RaceMine::OnFinished()
{
	if (!bHasScreen)
	{
		return;
	}
	HideBody();
}

// ─────────────────────────────────────────────────────────────────────────────
// Visual
// ─────────────────────────────────────────────────────────────────────────────

void ATN_RaceMine::BuildVisuals()
{
	using namespace TNRaceMineDetail;
	USceneComponent* RootComp = GetRootComponent();
	if (!RootComp)
	{
		return;
	}
	LedOffset = TNBeachKit::Hash01(GetTypeHash(GetFName()));

	// La malla del arte (~30 cm) escalada; sin ella, un plato verde oliva de respaldo.
	TNRaceItemArt::FHeldLook Look;
	UStaticMesh* BodyMesh = nullptr;
	double BodyScale = 1.0;
	if (TNRaceItemArt::GetHeldLook(ETNRaceItem::SandMine, Look) && Look.Mesh)
	{
		BodyMesh = Look.Mesh;
		BodyScale = ArtScale;
		MeshRestHalf = FMath::Clamp(static_cast<double>(Look.Mesh->GetBounds().BoxExtent.Z) * ArtScale, 8.0, MineRadius);
	}
	else
	{
		BodyMesh = SharedMesh(KeyFallbackMine, [](FMineBuffers& Buffers) { BuildFallbackMine(Buffers); });
		MeshRestHalf = FallbackRestHalf;
	}
	MineMesh = TNBeachKit::AddPart(this, RootComp, BodyMesh, FVector::ZeroVector, true);
	if (MineMesh)
	{
		MineMesh->SetRelativeScale3D(FVector(BodyScale));
	}

	// Piloto: halo rojo que late y luz roja (solo cerca de la cámara).
	HaloMesh = TNBeachKit::AddPart(this, RootComp, HaloBallMesh(), FVector(0.0, 0.0, HaloLift), false);
	if (HaloMesh)
	{
		HaloMesh->SetVisibility(false);
	}
	Light = NewObject<UPointLightComponent>(this, NAME_None, RF_Transient);
	if (Light)
	{
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetIntensityUnits(ELightUnits::Lumens);
		Light->SetIntensity(0.f);
		Light->SetAttenuationRadius(LedLightRadius);
		Light->SetLightColor(FLinearColor(1.f, 0.12f, 0.06f));
		Light->SetCastShadows(false);
		Light->SetupAttachment(RootComp);
		Light->SetRelativeLocation(FVector(0.0, 0.0, HaloLift + 30.0));
		Light->SetVisibility(false);
		Light->RegisterComponent();
	}

	// Cráter y bola del fogonazo: sueltos en el mundo, escondidos hasta explotar.
	CraterMesh = TNBeachKit::AddPart(this, RootComp, SharedMesh(KeyCrater, [](FMineBuffers& Buffers) { BuildCraterDisc(Buffers); }), FVector::ZeroVector, false);
	if (CraterMesh)
	{
		CraterMesh->SetAbsolute(true, true, true);
		CraterMesh->SetVisibility(false);
	}
	FlashMesh = TNBeachKit::AddPart(this, RootComp, SharedMesh(KeyFlash, [](FMineBuffers& Buffers) { BuildFlashBall(Buffers); }), FVector::ZeroVector, false);
	if (FlashMesh)
	{
		FlashMesh->SetAbsolute(true, true, true);
		FlashMesh->SetVisibility(false);
	}
}

void ATN_RaceMine::VisualTick(float DeltaSeconds)
{
	UpdateVisuals(DeltaSeconds);
	TickFX(DeltaSeconds);
}

void ATN_RaceMine::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// La base deja de llamar a VisualTick al acabar: el humo, el cráter y el fogonazo siguen.
	if (bHasScreen && IsFinished())
	{
		TickFX(DeltaSeconds);
	}
}

void ATN_RaceMine::UpdateVisuals(float DeltaSeconds)
{
	using namespace TNRaceMineDetail;
	// Ya ha explotado aquí (el multicast puede llegar antes que Phase y bFinished): ni piloto ni pitidos sobre el cráter.
	if (bBoomPlayed)
	{
		return;
	}
	const double Now = ServerNow();
	const double Elapsed = FMath::Max(0.0, Now - static_cast<double>(PhaseStartTime));
	const bool bAirborne = Phase == PhaseFlying;

	// Malla: da vueltas en el aire y, al posarse, se endereza y baja hasta apoyarse en la arena.
	if (MineMesh)
	{
		RestBlend = FMath::FInterpConstantTo(RestBlend, bAirborne ? 0.f : 1.f, DeltaSeconds, RestBlendSpeed);
		if (bAirborne)
		{
			const double Spun = GetAge();
			TumbleRot = FRotator(Spun * TumblePitchRate, Spun * TumbleYawRate, 0.0);
		}
		else
		{
			TumbleRot = FMath::RInterpTo(TumbleRot, FRotator::ZeroRotator, DeltaSeconds, 12.f);
		}
		if (bAirborne || RestBlend < 1.f || !TumbleRot.IsNearlyZero(0.05))
		{
			const double Sink = (MineRadius - MeshRestHalf) * static_cast<double>(RestBlend);
			MineMesh->SetRelativeLocationAndRotation(FVector(0.0, 0.0, -Sink), TumbleRot);
		}
	}

	// Piloto: sin armar late cada vez más deprisa; armada da un destello por segundo; en la mecha late rápido y pita.
	bool bLedWanted = false;
	switch (Phase)
	{
	case PhaseSettled:
	{
		const double Settling = FMath::Min(Elapsed, static_cast<double>(ArmDelaySeconds));
		const double Cycles = ArmBlinkStartHz * Settling
			+ 0.5 * (ArmBlinkEndHz - ArmBlinkStartHz) * Settling * Settling / static_cast<double>(ArmDelaySeconds)
			+ ArmBlinkEndHz * (Elapsed - Settling);
		bLedWanted = FMath::Frac(Cycles) < 0.5;
		break;
	}
	case PhaseArmed:
	{
		// A punto de estallar sola, parpadea más deprisa.
		const bool bWarn = static_cast<double>(SelfDestructSeconds) - Elapsed < WarnSeconds;
		const double BlinkRate = bWarn ? WarnBlinkHz : ArmedBlinkHz;
		const double BlinkDuty = bWarn ? 0.5 : ArmedFlashDuty;
		bLedWanted = FMath::Frac(Elapsed * BlinkRate + static_cast<double>(LedOffset)) < BlinkDuty;
		break;
	}
	case PhaseFuse:
	{
		bLedWanted = FMath::Frac(Elapsed * FuseBlinkHz) < 0.5;
		const int32 Cycle = FMath::FloorToInt32(Elapsed * FuseBlinkHz);
		if (Cycle >= BeepsPlayed && Elapsed < static_cast<double>(FuseSeconds) + 0.1)
		{
			BeepsPlayed = Cycle + 1;
			if (UTN_BeachMineSynthComponent* Beeper = GetMineVoice())
			{
				const float Rising = static_cast<float>(FMath::Min(1.0, Elapsed / static_cast<double>(FuseSeconds)));
				Beeper->Play(ETNBeachMineSound::Beep, 1.f + 0.45f * Rising, 0.9f);
			}
		}
		break;
	}
	default:
		break;
	}
	if (bLedWanted != bLedOn)
	{
		bLedOn = bLedWanted;
		if (HaloMesh)
		{
			HaloMesh->SetVisibility(bLedOn);
		}
	}

	// Luz roja: solo si el piloto está encendido y la cámara está cerca (mientras dura el fogonazo manda este).
	if (!bFlashActive && Light)
	{
		const bool bLit = bLedOn && ATN_BeachEnemy::LocalViewDistance(this, GetActorLocation()) < LedLightRange;
		if (bLit != Light->IsVisible())
		{
			if (bLit)
			{
				Light->SetLightColor(FLinearColor(1.f, 0.12f, 0.06f));
				Light->SetAttenuationRadius(LedLightRadius);
				Light->SetIntensity(LedLightIntensity);
			}
			Light->SetVisibility(bLit);
		}
	}
}

void ATN_RaceMine::TickFX(float DeltaSeconds)
{
	using namespace TNRaceMineDetail;

	// Fogonazo: la bola crece en 0,07 s y se apaga en unas décimas; la luz, en FlashLightSeconds.
	if (bFlashActive)
	{
		FlashAge += DeltaSeconds;
		const float Grow = FlashAge < 0.07f ? FlashAge / 0.07f : FMath::Max(0.f, 1.f - (FlashAge - 0.07f) / 0.18f);
		const bool bFlashOn = Grow > 0.01f;
		if (FlashMesh)
		{
			if (FlashMesh->IsVisible() != bFlashOn)
			{
				FlashMesh->SetVisibility(bFlashOn);
			}
			if (bFlashOn)
			{
				FlashMesh->SetWorldScale3D(FVector(FlashScale * static_cast<double>(Grow)));
			}
		}
		const float Glow = FMath::Square(FMath::Max(0.f, 1.f - FlashAge / FlashLightSeconds));
		if (Light && Glow > 0.001f)
		{
			Light->SetIntensity(FlashLightIntensity * Glow);
		}
		if (FlashAge >= FlashLightSeconds)
		{
			bFlashActive = false;
			if (FlashMesh)
			{
				FlashMesh->SetVisibility(false);
			}
			if (Light)
			{
				Light->SetVisibility(false);
			}
		}
	}

	// Partículas.
	if (bFXReady)
	{
		FVector View = GetActorLocation();
		TNBeachKit::LocalCamera(GetWorld(), View);
		TNBeachKit::TickEmitterIfBusy(Fire, DeltaSeconds, View);
		TNBeachKit::TickEmitterIfBusy(Smoke, DeltaSeconds, View);
		TNBeachKit::TickEmitterIfBusy(Dust, DeltaSeconds, View);
		TNBeachKit::TickEmitterIfBusy(Clods, DeltaSeconds, View);
		TNBeachKit::TickEmitterIfBusy(Sparks, DeltaSeconds, View);
	}
	BoomText.Tick(DeltaSeconds, GetWorld());

	// Cráter: crece de golpe, se queda y se encoge en el último segundo.
	if (CraterAge >= 0.f && CraterMesh)
	{
		CraterAge += DeltaSeconds;
		const double Grow = FMath::Min(1.0, static_cast<double>(CraterAge) / 0.1);
		const double Fade = FMath::Clamp((static_cast<double>(LingerSeconds) - static_cast<double>(CraterAge)) / 0.9, 0.0, 1.0);
		const double Scale = Grow * Fade;
		if (Scale <= 0.001)
		{
			CraterMesh->SetVisibility(false);
			CraterAge = -1.f;
		}
		else
		{
			CraterMesh->SetWorldScale3D(FVector(Scale, Scale, 1.0));
		}
	}
}

void ATN_RaceMine::EnsureFX()
{
	// Las partículas se crean con la primera explosión: la mayoría de las minas no llegan a tener humo.
	if (bFXReady || !bHasScreen)
	{
		return;
	}
	bFXReady = true;
	using TNAmbientFX::EShape;
	const uint32 Seed = GetTypeHash(GetFName());

	// Bola de fuego: nube blanda anaranjada que se abre y se apaga enseguida.
	TNAmbientFX::FEmitterDesc FireDesc = TNBeachKit::MakeDesc(EShape::Puff, FLinearColor(1.f, 0.6f, 0.2f), true, 0.85f, 18, 0.f, 1100.f, 0.f, 0.22f, 0.42f, 150.f, 420.f);
	FireDesc.Spread = 1.1f;
	FireDesc.SpawnRadius = 60.f;
	FireDesc.Drag = 2.5f;
	TNBeachKit::InitEmitter(Fire, this, FireDesc, Seed + 1u);

	// Humo oscuro que sube.
	TNAmbientFX::FEmitterDesc SmokeDesc = TNBeachKit::MakeDesc(EShape::Puff, FLinearColor(0.26f, 0.24f, 0.23f), true, 0.7f, 16, 0.f, 320.f, 0.f, 1.3f, 2.3f, 170.f, 460.f);
	SmokeDesc.Spread = 1.f;
	SmokeDesc.SpawnRadius = 90.f;
	SmokeDesc.Buoyancy = 200.f;
	SmokeDesc.Drag = 1.4f;
	TNBeachKit::InitEmitter(Smoke, this, SmokeDesc, Seed + 2u);

	// Nube de arena levantada.
	TNAmbientFX::FEmitterDesc DustDesc = TNBeachKit::MakeDesc(EShape::Puff, FLinearColor(0.86f, 0.76f, 0.56f), true, 0.6f, 24, 0.f, 900.f, -200.f, 0.9f, 1.6f, 140.f, 380.f);
	DustDesc.Spread = 1.5f;
	DustDesc.SpawnRadius = 140.f;
	DustDesc.Drag = 1.6f;
	TNBeachKit::InitEmitter(Dust, this, DustDesc, Seed + 3u);

	// Terrones que vuelan y caen.
	TNAmbientFX::FEmitterDesc ClodDesc = TNBeachKit::MakeDesc(EShape::Ember, FLinearColor(0.79f, 0.65f, 0.42f), false, 1.f, 24, 0.f, 1400.f, -1500.f, 0.8f, 1.4f, 34.f, 22.f);
	ClodDesc.Spread = 0.9f;
	ClodDesc.SpawnRadius = 80.f;
	ClodDesc.Drag = 0.15f;
	TNBeachKit::InitEmitter(Clods, this, ClodDesc, Seed + 4u);

	// Chispas brillantes.
	TNAmbientFX::FEmitterDesc SparkDesc = TNBeachKit::MakeDesc(EShape::Ember, FLinearColor(1.f, 0.72f, 0.2f), true, 0.95f, 20, 0.f, 1800.f, -900.f, 0.35f, 0.75f, 28.f, 6.f);
	SparkDesc.Spread = 1.3f;
	SparkDesc.SpawnRadius = 40.f;
	SparkDesc.Drag = 0.4f;
	TNBeachKit::InitEmitter(Sparks, this, SparkDesc, Seed + 5u);
}

void ATN_RaceMine::PlayLandFX()
{
	// Al posarse: el clic de la espoleta.
	if (UTN_BeachMineSynthComponent* Clicker = GetMineVoice())
	{
		Clicker->Play(ETNBeachMineSound::Click, FMath::FRandRange(0.9f, 1.05f), 0.8f);
	}
}

void ATN_RaceMine::PlayExplosionFX(const FVector& At)
{
	using namespace TNRaceMineDetail;
	// La explosión llega por el multicast y por el OnRep de Phase: solo suena y se ve una vez.
	if (!bHasScreen || bBoomPlayed || !HasActorBegunPlay())
	{
		return;
	}
	bBoomPlayed = true;
	HideBody();
	EnsureFX();
	const FVector FireAt = At + FVector(0.0, 0.0, 20.0);

	// Fogonazo: bola y luz anaranjada un instante.
	FlashAge = 0.f;
	bFlashActive = true;
	if (FlashMesh)
	{
		FlashMesh->SetWorldLocation(FireAt);
		FlashMesh->SetWorldScale3D(FVector(0.01));
		FlashMesh->SetVisibility(true);
	}
	if (Light)
	{
		Light->SetLightColor(FLinearColor(1.f, 0.62f, 0.3f));
		Light->SetAttenuationRadius(FlashLightRadius);
		Light->SetIntensity(FlashLightIntensity);
		Light->SetVisibility(true);
	}

	TNBeachKit::BurstAt(Fire, FireAt, FVector::UpVector, 12);
	TNBeachKit::BurstAt(Smoke, FireAt + FVector(0.0, 0.0, 50.0), FVector::UpVector, 8);
	TNBeachKit::BurstAt(Dust, FireAt - FVector(0.0, 0.0, 15.0), FVector::UpVector, 16);
	TNBeachKit::BurstAt(Clods, FireAt, FVector::UpVector, 14);
	TNBeachKit::BurstAt(Sparks, FireAt, FVector::UpVector, 14);
	BoomText.Show(this, NSLOCTEXT("TNRace", "MineBoom", "¡BUM!"), FColor(255, 140, 40), FireAt + FVector(0.0, 0.0, 260.0), 200.f);

	if (UTN_BeachMineSynthComponent* Speaker = GetMineVoice())
	{
		Speaker->Play(ETNBeachMineSound::Boom, FMath::FRandRange(0.92f, 1.08f), 1.f);
		Speaker->Play(ETNBeachMineSound::Debris, FMath::FRandRange(0.9f, 1.1f), 0.8f);
	}
	UTN_BeachCameraShake::Kick(this, At, 0.7f, 600.f, 3200.f);

	// Cráter chamuscado en la arena, unos segundos.
	if (CraterMesh)
	{
		const double GroundZ = static_cast<double>(GroundHeightAt(At, static_cast<float>(At.Z - MineRadius)));
		CraterMesh->SetWorldLocation(FVector(At.X, At.Y, GroundZ + CraterLift));
		CraterMesh->SetWorldScale3D(FVector(0.01));
		CraterMesh->SetVisibility(true);
		CraterAge = 0.f;
	}
}

void ATN_RaceMine::HideBody()
{
	if (MineMesh)
	{
		MineMesh->SetVisibility(false);
	}
	if (HaloMesh)
	{
		HaloMesh->SetVisibility(false);
	}
	bLedOn = false;
	// Sin piloto, tampoco su luz roja (mientras dura el fogonazo, la luz es suya y la apaga TickFX).
	if (Light && !bFlashActive)
	{
		Light->SetVisibility(false);
	}
}

UTN_BeachMineSynthComponent* ATN_RaceMine::GetMineVoice()
{
	if (!bHasScreen)
	{
		return nullptr;
	}
	if (!Voice)
	{
		Voice = UTN_BeachMineSynthComponent::AttachTo(this, GetActorLocation(), 1200.f, 9000.f);
	}
	return Voice;
}
