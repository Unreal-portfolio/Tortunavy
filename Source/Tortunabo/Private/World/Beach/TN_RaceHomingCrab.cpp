#include "World/Beach/TN_RaceHomingCrab.h"
#include "World/Beach/TN_BeachCameraShake.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "World/Beach/TN_BeachRaceGenerator.h"
#include "World/Beach/TN_BeachSandWorm.h"
#include "World/Beach/TN_BeachStun.h"
#include "World/Beach/TN_RaceBurstFX.h"
#include "World/Beach/TN_RaceItems.h"
#include "TN_BeachEnemyKit.h"
#include "TN_BeachEnemyMeshes.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TN_Log.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Player/TortugaCharacter.h"
#include "Settings/TN_CombatTuning.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto del
// bloque.
namespace TNRaceHomingCrabDetail
{
	// ── Lanzamiento ──

	/** Como mucho este número de cangrejos teledirigidos a la vez en el mundo. */
	constexpr int32 MaxCrabs = 10;
	/** Nace a esta distancia por delante de quien lo lanza (cm). */
	constexpr float SpawnAhead = 250.f;
	/** Si no hay tortuga por delante, el enemigo más cercano por delante a menos de esta distancia (cm). */
	constexpr float EnemySearchRange = 8000.f;

	// ── Carrera ──

	/** Velocidad al nacer y máxima (cm/s) y segundos que tarda en pasar de una a otra. */
	constexpr float StartSpeed = 900.f;
	constexpr float TopSpeed = 1600.f;
	constexpr float RampSeconds = 0.8f;
	/** Lo que puede girar hacia el objetivo (grados por segundo): radio de giro de ~2,2 m a toda velocidad. */
	constexpr float TurnDegreesPerSecond = 420.f;
	/** Vive como mucho estos segundos. */
	constexpr float LifeSeconds = 12.f;
	/** Saltitos encadenados: duración de cada arco (s) y altura (cm). */
	constexpr float HopSeconds = 0.45f;
	constexpr float HopHeight = 120.f;

	// ── Golpe ──

	/** Da a menos de esta distancia del objetivo, en planta y en vertical (cm). */
	constexpr float HitRadius = 260.f;
	constexpr float HitHeight = 260.f;
	/** Altura del centro del cuerpo sobre la arena, sin contar el salto (cm), para medir la distancia vertical. */
	constexpr float BodyLift = 30.f;
	/** Empujón de la tortuga hacia donde corría el cangrejo (cm/s) y hacia arriba (cm/s); el derribo y el mareo, en UTN_CombatTuning. */
	constexpr float KnockPush = 700.f;
	constexpr float KnockUp = 400.f;
	/** Segundos que sigue vivo el actor tras acabar (para que llegue el multicast y suene el «bonk»). */
	constexpr float FinishDelay = 1.f;
	/** Cada cuánto se comprueba que la tortuga perseguida sigue en carrera (s). */
	constexpr float RacerCheckSeconds = 0.25f;

	// ── Aspecto ──

	/** Escala del modelo: las mallas del cangrejo están a escala 28 (5 m); a 0,2 mide ~1,1 m con las patas. */
	constexpr float ModelScale = 0.2f;
	/** Más lejos de la cámara local que esto (cm) no se anima. */
	constexpr float ViewRange = 30000.f;
	/** Pasitos: cada cuántos segundos suenan y a qué distancia máxima del oyente (cm). */
	constexpr float ScuttleGap = 0.35f;
	constexpr float ScuttleRange = 6000.f;
	/** Patitas: ciclos por segundo; parpadeos por segundo de la bolita de la antena. */
	constexpr float LegBeatsPerSecond = 7.f;
	constexpr float BlinkPerSecond = 3.f;

	/** Parte del arco de salto (0..1) en el que está en el instante AgeSeconds. */
	inline float HopFraction(double AgeSeconds)
	{
		return FMath::Frac(static_cast<float>(AgeSeconds / static_cast<double>(HopSeconds)));
	}

	/** Altura del salto (cm sobre la arena) en el instante AgeSeconds: un arco de parábola tras otro. */
	inline float HopOffset(double AgeSeconds)
	{
		const float HopU = HopFraction(AgeSeconds);
		return HopHeight * 4.f * HopU * (1.f - HopU);
	}

	/** Cota del suelo bajo Where según el generador de la playa (sin trazas); Fallback si no lo hay. */
	inline float GroundHeight(const UObject* Context, const FVector& Where, float Fallback)
	{
		if (const ATN_BeachRaceGenerator* Generator = ATN_BeachRaceGenerator::Find(Context))
		{
			return Generator->GetGroundHeightAt(Where);
		}
		return Fallback;
	}

	/** true si la tortuga sigue en carrera (viva, sin haber llegado a la meta y sin que un gusano se la coma). */
	inline bool IsRacingNow(const UObject* Context, ATortugaCharacter* Turtle)
	{
		if (!IsValid(Turtle) || Turtle->IsDead() || ATN_BeachSandWorm::IsBeingEaten(Turtle))
		{
			return false;
		}
		TArray<ATortugaCharacter*> Racers;
		TNRaceItems::GatherRacers(Context, Racers);
		return Racers.Contains(Turtle);
	}

	/**
	 * El enemigo más cercano por delante de Launcher (en el semiplano de avance de la carrera) a menos de MaxDistance del borde de
	 * su cuerpo, que se pueda marear y no lo esté ya. null si no hay.
	 */
	inline ATN_BeachEnemy* FindEnemyAhead(const ATortugaCharacter* Launcher, float MaxDistance)
	{
		UWorld* World = Launcher ? Launcher->GetWorld() : nullptr;
		if (!World)
		{
			return nullptr;
		}
		const FVector LauncherAt = Launcher->GetActorLocation();
		const float LauncherProgress = TNRaceItems::CourseProgress(Launcher, LauncherAt);
		ATN_BeachEnemy* Best = nullptr;
		double BestDistance = static_cast<double>(MaxDistance);
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
			const FVector Nearest = FMath::ClosestPointOnSegment(LauncherAt, CapsuleA, CapsuleB);
			if (TNRaceItems::CourseProgress(Launcher, Nearest) <= LauncherProgress)
			{
				continue;
			}
			const double Distance = FVector::Dist(Nearest, LauncherAt) - static_cast<double>(CapsuleRadius);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = Enemy;
			}
		}
		return Best;
	}

	/** A quién manda Launcher su cangrejo: la tortuga más cercana por delante o, si no hay, el enemigo más cercano por delante. */
	inline AActor* FindChaseTarget(ATortugaCharacter* Launcher)
	{
		if (ATortugaCharacter* Rival = TNRaceItems::FindTurtleTarget(Launcher, true))
		{
			return Rival;
		}
		return FindEnemyAhead(Launcher, EnemySearchRange);
	}

	// ── Dibujo ──

	/** Cangrejo de juguete: todo rojo, con las puntas de las pinzas y las patas color crema. */
	inline TNBeachMeshes::FCrabLook ToyLook()
	{
		TNBeachMeshes::FCrabLook Toy;
		Toy.ShellTop = TNBeachMeshes::Rgb(0.92f, 0.08f, 0.06f);
		Toy.Shell = TNBeachMeshes::Rgb(1.f, 0.42f, 0.28f);
		Toy.Leg = TNBeachMeshes::Rgb(0.8f, 0.1f, 0.08f);
		Toy.Big = TNBeachMeshes::Rgb(0.95f, 0.12f, 0.08f);
		Toy.Tip = TNBeachMeshes::Rgb(1.f, 0.93f, 0.8f);
		Toy.Small = TNBeachMeshes::Rgb(0.95f, 0.12f, 0.08f);
		return Toy;
	}

	/** Pie de la antena del mando en el espacio del cuerpo (mallas a escala 28): atrás, sobre el caparazón. */
	inline FVector AntennaBase()
	{
		return FVector(-TNBeachMeshes::CrabD * TNBeach::Scale * 0.42, 0.0, TNBeachMeshes::CrabH * TNBeach::Scale * 0.46);
	}

	/** Punta de la antena respecto a su pie (donde va la bolita), algo inclinada hacia atrás. */
	inline FVector AntennaTip()
	{
		return FVector(-30.0, 0.0, 230.0);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Lanzamiento
// ─────────────────────────────────────────────────────────────────────────────

bool ATN_RaceHomingCrab::ServerLaunch(ATortugaCharacter* Turtle)
{
	using namespace TNRaceHomingCrabDetail;
	UWorld* World = Turtle ? Turtle->GetWorld() : nullptr;
	if (!World || !Turtle->HasAuthority())
	{
		return false;
	}
	if (CountOf(World, StaticClass()) >= MaxCrabs)
	{
		return false;
	}
	AActor* Goal = FindChaseTarget(Turtle);
	if (!Goal)
	{
		return false;
	}

	// Sale hacia el objetivo, 2,5 m por delante de quien lo lanza, en la arena.
	const FVector TurtleAt = Turtle->GetActorLocation();
	FVector LaunchDir = Goal->GetActorLocation() - TurtleAt;
	LaunchDir.Z = 0.0;
	LaunchDir = LaunchDir.GetSafeNormal();
	if (LaunchDir.IsNearlyZero())
	{
		LaunchDir = Turtle->GetActorForwardVector().GetSafeNormal2D();
		if (LaunchDir.IsNearlyZero())
		{
			LaunchDir = FVector::ForwardVector;
		}
	}
	FVector SpawnLoc = TurtleAt + LaunchDir * static_cast<double>(SpawnAhead);
	SpawnLoc.Z = static_cast<double>(GroundHeight(Turtle, SpawnLoc, static_cast<float>(TurtleAt.Z - static_cast<double>(Turtle->GetSimpleCollisionHalfHeight()))));
	const FRotator SpawnRot(0.f, static_cast<float>(LaunchDir.Rotation().Yaw), 0.f);
	const FTransform SpawnAt(SpawnRot, SpawnLoc);

	ATN_RaceHomingCrab* Crab = World->SpawnActorDeferred<ATN_RaceHomingCrab>(ATN_RaceHomingCrab::StaticClass(), SpawnAt, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Crab)
	{
		return false;
	}
	Crab->SetOwnerTurtle(Turtle);
	Crab->Chased = Goal;
	Crab->HeadingYaw = static_cast<float>(SpawnRot.Yaw);
	Crab->FinishSpawning(SpawnAt);
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] %s lanza un cangrejo teledirigido contra %s."), *GetNameSafe(Turtle), *GetNameSafe(Goal));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor
// ─────────────────────────────────────────────────────────────────────────────

void ATN_RaceHomingCrab::ServerTick(float DeltaSeconds)
{
	using namespace TNRaceHomingCrabDetail;
	const float AgeSeconds = static_cast<float>(GetAge());
	if (AgeSeconds >= LifeSeconds || !ATN_BeachEnemy::IsRaceLive(this))
	{
		Fizzle();
		return;
	}

	// Rumbo: gira hacia el objetivo hasta TurnDegreesPerSecond; sin objetivo, sigue recto.
	FVector ChasePoint = FVector::ZeroVector;
	const bool bChasing = !bChaseLost && ResolveChasePoint(DeltaSeconds, ChasePoint);
	const FVector Here = GetActorLocation();
	if (bChasing)
	{
		FVector ToChase = ChasePoint - Here;
		ToChase.Z = 0.0;
		if (!ToChase.IsNearlyZero())
		{
			HeadingYaw = FMath::FixedTurn(HeadingYaw, static_cast<float>(ToChase.Rotation().Yaw), TurnDegreesPerSecond * DeltaSeconds);
		}
	}

	// Avance: 9 m/s que suben a 16 m/s en 0,8 s. Pegado a la arena; los saltitos los pone Hopper. No choca con nada.
	const float RunSpeed = FMath::Lerp(StartSpeed, TopSpeed, FMath::Clamp(AgeSeconds / RampSeconds, 0.f, 1.f));
	const FRotator Facing(0.f, HeadingYaw, 0.f);
	FVector NewLoc = Here + Facing.Vector() * static_cast<double>(RunSpeed * DeltaSeconds);
	NewLoc.Z = static_cast<double>(GroundHeightAt(NewLoc, static_cast<float>(Here.Z)));
	SetActorLocationAndRotation(NewLoc, Facing);

	// Golpe: cerca en planta y en vertical (contando lo que sube en su saltito).
	if (bChasing)
	{
		const double PlanarGap = FVector::Dist2D(NewLoc, ChasePoint);
		const double VerticalGap = FMath::Abs(NewLoc.Z + static_cast<double>(BodyLift + HopOffset(static_cast<double>(AgeSeconds))) - ChasePoint.Z);
		if (PlanarGap < static_cast<double>(HitRadius) && VerticalGap < static_cast<double>(HitHeight))
		{
			ApplyHit(NewLoc + FVector(0.0, 0.0, static_cast<double>(BodyLift)));
		}
	}
}

bool ATN_RaceHomingCrab::ResolveChasePoint(float DeltaSeconds, FVector& OutPoint)
{
	using namespace TNRaceHomingCrabDetail;
	AActor* Goal = Chased.Get();
	if (!IsValid(Goal))
	{
		bChaseLost = true;
		return false;
	}
	if (ATortugaCharacter* Rival = Cast<ATortugaCharacter>(Goal))
	{
		// Muerta, llegada a la meta o comida por un gusano: se acaba el objetivo.
		RacerClock -= DeltaSeconds;
		if (RacerClock <= 0.f)
		{
			RacerClock = RacerCheckSeconds;
			if (!IsRacingNow(this, Rival))
			{
				bChaseLost = true;
				return false;
			}
		}
		OutPoint = Rival->GetActorLocation();
		return true;
	}
	if (const ATN_BeachEnemy* Enemy = Cast<ATN_BeachEnemy>(Goal))
	{
		// El punto más cercano de su cuerpo (la superficie de la cápsula), no su eje.
		FVector CapsuleA = FVector::ZeroVector;
		FVector CapsuleB = FVector::ZeroVector;
		float CapsuleRadius = 0.f;
		if (!Enemy->AcceptsHitStun() || !Enemy->GetHitCapsule(CapsuleA, CapsuleB, CapsuleRadius))
		{
			bChaseLost = true;
			return false;
		}
		const FVector Here = GetActorLocation();
		const FVector AxisPoint = FMath::ClosestPointOnSegment(Here, CapsuleA, CapsuleB);
		const FVector Outward = Here - AxisPoint;
		OutPoint = Outward.IsNearlyZero() ? AxisPoint : AxisPoint + Outward.GetSafeNormal() * static_cast<double>(CapsuleRadius);
		return true;
	}
	bChaseLost = true;
	return false;
}

void ATN_RaceHomingCrab::ApplyHit(const FVector& Where)
{
	using namespace TNRaceHomingCrabDetail;
	bool bBounced = true;
	const FVector RunDir = FRotator(0.f, HeadingYaw, 0.f).Vector();
	if (ATortugaCharacter* Victim = Cast<ATortugaCharacter>(Chased.Get()))
	{
		// Invulnerable (protector, pelícano) o ya aturdida o derribada: rebota sin más.
		if (TNRaceItems::CanBeHurt(Victim) && ATN_BeachEnemy::CanBeHit(Victim))
		{
			TNBeach::KnockDownTurtle(Victim, UTN_CombatTuning::Get().HomingCrabKnockSeconds, RunDir * static_cast<double>(KnockPush) + FVector(0.0, 0.0, static_cast<double>(KnockUp)));
			bBounced = false;
		}
	}
	else if (ATN_BeachEnemy* Enemy = Cast<ATN_BeachEnemy>(Chased.Get()))
	{
		Enemy->ApplyHitStun(UTN_CombatTuning::Get().HomingCrabEnemyStunSeconds, GetOwnerTurtle());
		bBounced = false;
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] El cangrejo teledirigido de %s da a %s%s."), *GetNameSafe(GetOwnerTurtle()), *GetNameSafe(Chased.Get()),
		bBounced ? TEXT(" (rebota)") : TEXT(""));
	MulticastHit(FVector_NetQuantize10(Where), bBounced);
	ServerFinish(FinishDelay);
}

void ATN_RaceHomingCrab::Fizzle()
{
	using namespace TNRaceHomingCrabDetail;
	MulticastFizzle(FVector_NetQuantize10(GetActorLocation() + FVector(0.0, 0.0, static_cast<double>(BodyLift))));
	ServerFinish(FinishDelay);
}

// ─────────────────────────────────────────────────────────────────────────────
// Efectos (todas las máquinas)
// ─────────────────────────────────────────────────────────────────────────────

void ATN_RaceHomingCrab::MulticastHit_Implementation(FVector_NetQuantize10 Where, bool bBounced)
{
	if (!bHasScreen)
	{
		return;
	}
	const FVector At(Where);
	PlaySfx(ETNRaceSound::Bonk, bBounced ? 1.5f : 1.f, 1.f);
	ATN_RaceBurstFX::SpawnLocal(GetWorld(), ETNRaceBurst::Poof, At, 1.f);
	if (!bBounced)
	{
		UTN_BeachCameraShake::Kick(this, At, 0.3f, 200.f, 1500.f);
	}
}

void ATN_RaceHomingCrab::MulticastFizzle_Implementation(FVector_NetQuantize10 Where)
{
	if (!bHasScreen)
	{
		return;
	}
	ATN_RaceBurstFX::SpawnLocal(GetWorld(), ETNRaceBurst::Poof, FVector(Where), 0.6f);
}

void ATN_RaceHomingCrab::OnFinished()
{
	// Se esconde el modelo (el actor vive un poco más para que llegue el multicast); el polvo, quieto y fuera.
	if (Hopper)
	{
		Hopper->SetVisibility(false, true);
	}
	if (Shadow)
	{
		TNBeachKit::PlaceShadow(Shadow, FVector::ZeroVector, 0.f);
	}
	Dust.RateScale = 0.f;
	if (UInstancedStaticMeshComponent* DustMesh = Dust.ISM.Get())
	{
		DustMesh->SetVisibility(false);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Visual
// ─────────────────────────────────────────────────────────────────────────────

void ATN_RaceHomingCrab::BuildVisuals()
{
	using namespace TNRaceHomingCrabDetail;
	using TNProcMesh::FTNProcMeshBuffers;
	const TNBeachMeshes::FCrabLook Look = ToyLook();
	const TNBeachMeshes::FCrabRig Rig = TNBeachMeshes::CrabRig();
	ShownYaw = static_cast<float>(GetActorRotation().Yaw);

	// Hopper sube y baja con los saltitos; Scaler encoge el cangrejo (mallas a escala 28) a ~1,1 m.
	Hopper = NewObject<USceneComponent>(this, NAME_None, RF_Transient);
	Hopper->SetupAttachment(GetRootComponent());
	Hopper->RegisterComponent();
	Scaler = NewObject<USceneComponent>(this, NAME_None, RF_Transient);
	Scaler->SetupAttachment(Hopper);
	Scaler->SetRelativeScale3D(FVector(ModelScale));
	Scaler->RegisterComponent();

	UStaticMesh* BodyMesh = TNBeachKit::CachedMesh(TEXT("Race.Crab.Toy.Body"), [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildCrabBody(M, Look); });
	UStaticMesh* EyeMesh = TNBeachKit::CachedMesh(TEXT("Race.Crab.Toy.Eye"), [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildCrabEye(M, Look); });
	UStaticMesh* LegLeftMesh = TNBeachKit::CachedMesh(TEXT("Race.Crab.Toy.LegL"), [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildCrabLeg(M, Look, -1.0); });
	UStaticMesh* LegRightMesh = TNBeachKit::CachedMesh(TEXT("Race.Crab.Toy.LegR"), [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildCrabLeg(M, Look, 1.0); });
	UStaticMesh* ArmMesh = TNBeachKit::CachedMesh(TEXT("Race.Crab.Toy.Arm"), [&Look, &Rig](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildCrabBigArm(M, Look, Rig); });
	UStaticMesh* HandMesh = TNBeachKit::CachedMesh(TEXT("Race.Crab.Toy.Hand"), [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildCrabBigHand(M, Look); });
	UStaticMesh* FingerMesh = TNBeachKit::CachedMesh(TEXT("Race.Crab.Toy.Finger"), [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildCrabBigFinger(M, Look); });
	UStaticMesh* SmallMesh = TNBeachKit::CachedMesh(TEXT("Race.Crab.Toy.Small"), [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildCrabSmallClaw(M, Look); });

	// Antena del mando a distancia (en el espacio del cuerpo, a escala 28): peana oscura y varilla de acero; la bolita, aparte.
	UStaticMesh* AntennaMesh = TNBeachKit::CachedMesh(TEXT("Race.Crab.Toy.Antenna"), [](FTNProcMeshBuffers& M)
	{
		const FLinearColor Dark = TNBeachMeshes::Rgb(0.14f, 0.14f, 0.17f);
		const FLinearColor Steel = TNBeachMeshes::Rgb(0.78f, 0.79f, 0.84f);
		TNProcMesh::TNProcAddCylinder(M, FVector::ZeroVector, FVector(0.0, 0.0, 26.0), 30.0, 22.0, 8, Dark, true);
		M.AddBeam(FVector(0.0, 0.0, 20.0), AntennaTip(), 5.5, Steel);
	});
	UStaticMesh* BallOnMesh = TNBeachKit::CachedMesh(TEXT("Race.Crab.Toy.BallOn"), [](FTNProcMeshBuffers& M)
	{
		TNFauna::TNFaunaBlob(M, FVector::ZeroVector, FVector(30.0, 30.0, 30.0), TNBeachMeshes::Rgb(1.f, 0.97f, 0.4f), TNBeachMeshes::Rgb(1.f, 0.75f, 0.2f), 8, 4);
	});
	UStaticMesh* BallOffMesh = TNBeachKit::CachedMesh(TEXT("Race.Crab.Toy.BallOff"), [](FTNProcMeshBuffers& M)
	{
		TNFauna::TNFaunaBlob(M, FVector::ZeroVector, FVector(30.0, 30.0, 30.0), TNBeachMeshes::Rgb(0.42f, 0.05f, 0.05f), TNBeachMeshes::Rgb(0.28f, 0.03f, 0.03f), 8, 4);
	});

	// Solo el cuerpo da sombra de verdad; para lo demás vale la redonda de la arena.
	Body = TNBeachKit::AddPart(this, Scaler, BodyMesh, FVector(0.0, 0.0, Rig.BodyZ), true);
	Eyes.Add(TNBeachKit::AddPart(this, Body, EyeMesh, Rig.EyeL, false));
	Eyes.Add(TNBeachKit::AddPart(this, Body, EyeMesh, Rig.EyeR, false));
	for (int32 LegIndex = 0; LegIndex < 8; ++LegIndex)
	{
		UStaticMeshComponent* Leg = TNBeachKit::AddPart(this, Body, LegIndex < 4 ? LegLeftMesh : LegRightMesh, Rig.LegPivot[LegIndex], false);
		if (Leg)
		{
			Leg->SetRelativeRotation(FRotator(0.f, Rig.LegSplay[LegIndex], 0.f));
		}
		Legs.Add(Leg);
	}
	BigArm = TNBeachKit::AddPart(this, Body, ArmMesh, Rig.BigShoulder, false);
	BigHand = TNBeachKit::AddPart(this, BigArm, HandMesh, Rig.BigElbow, false);
	BigFinger = TNBeachKit::AddPart(this, BigHand, FingerMesh, Rig.BigKnuckle, false);
	SmallClaw = TNBeachKit::AddPart(this, Body, SmallMesh, Rig.SmallShoulder, false);
	Antenna = TNBeachKit::AddPart(this, Body, AntennaMesh, AntennaBase(), false);
	BallOn = TNBeachKit::AddPart(this, Antenna, BallOnMesh, AntennaTip(), false);
	BallOff = TNBeachKit::AddPart(this, Antenna, BallOffMesh, AntennaTip(), false);
	if (BallOff)
	{
		BallOff->SetVisibility(false);
	}
	bBallLit = true;

	Shadow = TNBeachKit::AddShadow(this, 0.45f);
	TNBeachKit::PlaceShadow(Shadow, FVector::ZeroVector, 0.f);

	// Polvo de arena tras él al correr y al aterrizar de cada salto.
	TNAmbientFX::FEmitterDesc DustDesc = TNBeachKit::MakeDesc(TNAmbientFX::EShape::Puff, FLinearColor(0.88f, 0.78f, 0.58f), true, 0.5f, 36, 22.f, 300.f, -80.f, 0.5f, 0.9f, 22.f, 85.f);
	DustDesc.Drag = 1.6f;
	DustDesc.Spread = 0.8f;
	DustDesc.SpawnRadius = 35.f;
	TNBeachKit::InitEmitter(Dust, this, DustDesc, GetUniqueID());
}

void ATN_RaceHomingCrab::VisualTick(float DeltaSeconds)
{
	using namespace TNRaceHomingCrabDetail;
	if (!Hopper || !Body)
	{
		return;
	}
	VisualClock += DeltaSeconds;
	const double AgeSeconds = GetAge();
	const FVector Here = GetActorLocation();
	FVector View = Here;
	TNBeachKit::LocalCamera(GetWorld(), View);
	const double CameraDistSq = FVector::DistSquared(View, Here);
	const bool bNear = CameraDistSq < FMath::Square(static_cast<double>(ViewRange));

	// Saltitos: la altura sale del reloj del servidor (igual en todas las máquinas). Sube con el morro arriba, baja con él
	// abajo y se inclina hacia el lado al que gira.
	const float HopU = HopFraction(AgeSeconds);
	const float HopNow = HopOffset(AgeSeconds);
	const float AirK = FMath::Clamp(HopNow / HopHeight, 0.f, 1.f);
	const float ActorYaw = static_cast<float>(GetActorRotation().Yaw);
	const float YawRate = FMath::FindDeltaAngleDegrees(ShownYaw, ActorYaw) / FMath::Max(DeltaSeconds, 1.0e-3f);
	ShownYaw = ActorYaw;
	LeanRoll = FMath::FInterpTo(LeanRoll, FMath::Clamp(YawRate * 0.05f, -22.f, 22.f), DeltaSeconds, 6.f);
	Hopper->SetRelativeLocationAndRotation(FVector(0.0, 0.0, static_cast<double>(HopNow)), FRotator(14.f * (1.f - 2.f * HopU), 0.f, LeanRoll));

	if (bNear)
	{
		const TNBeachMeshes::FCrabRig Rig = TNBeachMeshes::CrabRig();
		GaitPhase += DeltaSeconds * 2.f * PI * LegBeatsPerSecond;

		// Cuerpo: se bambolea con cada paso.
		TNBeachKit::Pose(Body, FVector(0.0, 0.0, Rig.BodyZ), FRotator(0.f, 0.f, 3.f * FMath::Sin(GaitPhase)));

		// Patitas en dos grupos que se alternan (como el cangrejo gigante); en el aire las recogen.
		for (int32 LegIndex = 0; LegIndex < Legs.Num(); ++LegIndex)
		{
			const float Side = LegIndex < 4 ? -1.f : 1.f;
			const int32 InSide = LegIndex % 4;
			const bool bGroupA = (Side < 0.f) == (InSide % 2 == 0);
			const float LegPhase = GaitPhase + (bGroupA ? 0.f : PI);
			const float StepLift = FMath::Lerp(26.f * FMath::Max(0.f, FMath::Sin(LegPhase)), 34.f, FMath::Clamp(AirK * 2.f, 0.f, 1.f));
			const float StepSwing = 10.f * FMath::Sin(LegPhase + static_cast<float>(InSide));
			TNBeachKit::Pose(Legs[LegIndex], Rig.LegPivot[LegIndex], FRotator(0.f, Rig.LegSplay[LegIndex] + StepSwing, -Side * StepLift));
		}

		// Ojos saltones: miran al frente y se bambolean.
		for (int32 EyeIndex = 0; EyeIndex < Eyes.Num(); ++EyeIndex)
		{
			const float EyeSign = EyeIndex == 0 ? -1.f : 1.f;
			const float EyeYaw = 10.f * FMath::Sin(VisualClock * 7.f + static_cast<float>(EyeIndex) * 2.f);
			const float EyePitch = -14.f + 8.f * FMath::Sin(VisualClock * 9.f + static_cast<float>(EyeIndex));
			TNBeachKit::Pose(Eyes[EyeIndex], EyeIndex == 0 ? Rig.EyeL : Rig.EyeR, FRotator(EyePitch, EyeYaw, EyeSign * 6.f));
		}

		// Pinzas en alto, chasqueando sin parar.
		TNBeachKit::Pose(BigArm, Rig.BigShoulder, FRotator(34.f + 12.f * FMath::Sin(VisualClock * 9.f), static_cast<float>(Rig.ArmYaw), 0.f));
		TNBeachKit::Pose(BigHand, Rig.BigElbow, FRotator(-18.f, 0.f, 0.f));
		TNBeachKit::Pose(BigFinger, Rig.BigKnuckle, FRotator(8.f + 28.f * FMath::Abs(FMath::Sin(VisualClock * 13.f)), 0.f, 0.f));
		TNBeachKit::Pose(SmallClaw, Rig.SmallShoulder, FRotator(10.f + 14.f * FMath::Sin(VisualClock * 10.f + 1.f), 8.f, 0.f));

		// Antena del mando: se dobla con la carrera.
		TNBeachKit::Pose(Antenna, AntennaBase(), FRotator(5.f * FMath::Sin(VisualClock * 9.f), 0.f, 4.f * FMath::Sin(VisualClock * 7.f + 1.f)));
	}

	// La bolita de la antena parpadea: encendida (amarilla) y apagada (rojo oscuro).
	const bool bLit = FMath::Frac(VisualClock * BlinkPerSecond) < 0.5f;
	if (bLit != bBallLit)
	{
		bBallLit = bLit;
		if (BallOn)
		{
			BallOn->SetVisibility(bLit);
		}
		if (BallOff)
		{
			BallOff->SetVisibility(!bLit);
		}
	}

	// Sombra redonda: más pequeña cuanto más alto salta.
	TNBeachKit::PlaceShadow(Shadow, Here, bNear ? FMath::Lerp(70.f, 46.f, AirK) : 0.f);

	// Polvo de arena tras él mientras corre por el suelo y una bocanada al aterrizar de cada saltito.
	if (Dust.ISM.IsValid())
	{
		const FVector Behind = -GetActorForwardVector();
		Dust.Origin = Here + Behind * 55.0 + FVector(0.0, 0.0, 8.0);
		Dust.Desc.Direction = (Behind + FVector(0.0, 0.0, 0.7)).GetSafeNormal();
		Dust.RateScale = (bNear && HopNow < 25.f) ? 1.f : 0.f;
		if (bNear && HopU < PrevHopFraction)
		{
			TNBeachKit::BurstAt(Dust, Here + FVector(0.0, 0.0, 8.0), FVector::UpVector, 3);
		}
	}
	PrevHopFraction = HopU;
	TNBeachKit::TickEmitterIfBusy(Dust, DeltaSeconds, View);

	// Sonido: «boing» al salir y pasitos seguidos mientras el oyente esté cerca.
	if (!bBornSoundPlayed)
	{
		bBornSoundPlayed = true;
		if (AgeSeconds < 1.0)
		{
			PlaySfx(ETNRaceSound::Boing, 1.f, 1.f);
		}
	}
	ScuttleClock -= DeltaSeconds;
	if (ScuttleClock <= 0.f)
	{
		ScuttleClock = ScuttleGap;
		if (CameraDistSq < FMath::Square(static_cast<double>(ScuttleRange)))
		{
			PlaySfx(ETNRaceSound::Scuttle, 0.9f + 0.2f * FMath::FRand(), 0.7f);
		}
	}
}
