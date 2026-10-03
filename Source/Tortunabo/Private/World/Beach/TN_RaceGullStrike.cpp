#include "World/Beach/TN_RaceGullStrike.h"
#include "World/Beach/TN_BeachCameraShake.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "World/Beach/TN_BeachGullTuning.h"
#include "World/Beach/TN_BeachRaceGenerator.h"
#include "World/Beach/TN_BeachSandWorm.h"
#include "World/Beach/TN_BeachStun.h"
#include "World/Beach/TN_RaceItemComponent.h"
#include "World/Beach/TN_RaceItems.h"
#include "TN_BeachEnemyKit.h"
#include "TN_BeachEnemyMeshes.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TN_Log.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Player/TortugaCharacter.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto del
// bloque.
namespace TNRaceGullStrikeDetail
{
	// ── Lanzamiento ──

	/** Como mucho este número de gaviotas justicieras a la vez en el mundo. */
	constexpr int32 MaxGulls = 3;
	/** Si nadie va por delante, el enemigo más cercano por delante a menos de esta distancia (cm). */
	constexpr float EnemySearchRange = 12000.f;

	// ── Línea de tiempo (segundos desde que nace) ──

	/** Hasta aquí vuela hasta situarse sobre el objetivo; a los 3,2 s suelta la cagada. */
	constexpr float ArriveSeconds = TNBeachGullTuning::StrikeReleaseAge;
	/** Lo que tarda en caer la cagada (s) y el instante del impacto. */
	constexpr float FallSeconds = TNBeachGullTuning::StrikeFallSeconds;
	constexpr float ImpactAge = ArriveSeconds + FallSeconds;
	/** A esta edad ha subido y se ha ido: acaba el objeto. */
	constexpr float LeaveAge = 9.f;
	/** Segundos que sigue vivo el actor tras acabar: lo que duran las manchas (8 s) desde el impacto (4,9 s). */
	constexpr float FinishDelay = 4.5f;
	/** Si a un cliente no le llega el multicast del impacto, lo pinta él a esta edad extra (s). */
	constexpr float SplatFallbackDelay = 0.6f;

	// ── Vuelo ──

	/** Nace a esta distancia por detrás del objetivo (cm) y a esta altura sobre él; llega sobre él a HoverHeight. */
	constexpr float StartBehind = 9000.f;
	constexpr float StartHeight = 7000.f;
	constexpr float HoverHeight = 3000.f;
	/** Tras soltarla: en cuánto pasa de ir sobre el objetivo a ir sobre el punto de impacto (s). */
	constexpr float LeaveBlendSeconds = 0.8f;
	/** Al irse: avance hacia delante (velocidad cm/s y aceleración cm/s²) y ascenso. */
	constexpr float LeaveSpeed = 1800.f;
	constexpr float LeaveAccel = 400.f;
	constexpr float ClimbSpeed = 700.f;
	constexpr float ClimbAccel = 500.f;

	// ── Cagada ──

	/**
	 * El punto de impacto sigue al objetivo por la arena a esta velocidad como mucho (cm/s; más de lo que anda y menos de lo que corre una
	 * tortuga) y, desde justo después de soltarla, cae por la línea que llevaba (TNBeachGullTuning::StrikePlan). Girando
	 * corriendo o dándose la vuelta en ese momento, o tirándose en plancha a tiempo, se libra; esprintando en línea recta, también (#636).
	 */
	constexpr float AimSpeed = TNBeachGullTuning::StrikeChaseSpeed;
	/** Radio del impacto en planta (cm; antes 330) y altura máxima (cm) sobre la arena. */
	constexpr float ImpactRadius = TNBeachGullTuning::StrikeImpactRadius;
	constexpr float ImpactHeight = 300.f;
	/** Derribo (s), empujón hacia fuera (cm/s) y hacia arriba (cm/s). */
	constexpr float KnockSeconds = 2.6f;
	constexpr float KnockPush = 260.f;
	constexpr float KnockUp = 150.f;
	/** Mareo del enemigo (s). */
	constexpr float EnemyStunSeconds = 4.f;
	/** Cada cuánto se comprueba que la tortuga a la que sigue el blanco sigue en carrera (s). */
	constexpr float RacerCheckSeconds = 0.25f;

	// ── Aspecto ──

	/** Escala de la gaviota (la de las zonas de gaviotas) y su envergadura (cm). */
	constexpr float GullScale = 28.f;
	constexpr float GullSpan = 2520.f;
	/** Más lejos de la cámara local que esto (cm) no se anima. */
	constexpr float ViewRange = 45000.f;
	/** Tamaño de la cagada que cae. */
	constexpr float DropScale = 1.5f;
	/** Sombra dura de la arena: radio con el que nace (cm), opacidad, segundos de aparición y nitidez del borde. */
	constexpr float MarkerStartRadius = 90.f;
	constexpr float MarkerOpacity = 0.88f;
	constexpr float MarkerFadeIn = 0.35f;
	constexpr float MarkerEdge = 0.92f;
	/** Mancha de la arena y pegote del caparazón: segundos que duran y escala del pegote. */
	constexpr float SplatLife = 8.f;
	constexpr float StainLife = 8.f;
	constexpr float StainScale = 0.7f;

	inline float Smooth01(float X)
	{
		const float Clamped = FMath::Clamp(X, 0.f, 1.f);
		return Clamped * Clamped * (3.f - 2.f * Clamped);
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

	/** Hacia dónde avanza la carrera en el plano (hacia el mar del generador; sin él, +X, como TNRaceItems::CourseProgress). */
	inline FVector CourseForwardOf(const UObject* Context)
	{
		if (const ATN_BeachRaceGenerator* Generator = ATN_BeachRaceGenerator::Find(Context))
		{
			const FVector Sea = Generator->GetSeaDirection().GetSafeNormal2D();
			if (!Sea.IsNearlyZero())
			{
				return Sea;
			}
		}
		return FVector::ForwardVector;
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
	 * su cuerpo, que se pueda marear. null si no hay.
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

	/**
	 * Material de los avisos duros (M_ProcFXHard, de Scripts/create_poop_decal.py): translúcido sin luz como M_ProcFXSoft pero
	 * sin su fundido por profundidad. Null si falta: se usa el suave.
	 */
	inline UMaterialInterface* HardFxMaterial()
	{
		static TWeakObjectPtr<UMaterialInterface> Cached;
		if (!Cached.IsValid())
		{
			Cached = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_ProcFXHard.M_ProcFXHard"), nullptr, LOAD_NoWarn);
		}
		return Cached.Get();
	}

	/** Pone en Comp el material dinámico de un aviso duro (el suave de siempre si falta el duro) con la opacidad Opacity. */
	inline void SetupHardMarker(UStaticMeshComponent* Comp, float Opacity)
	{
		if (!Comp)
		{
			return;
		}
		if (UMaterialInterface* Hard = HardFxMaterial())
		{
			Comp->CreateDynamicMaterialInstance(0, Hard);
		}
		TNBeachKit::SetOpacity(TNBeachKit::SoftMID(Comp), Opacity);
	}

	/**
	 * Aviso duro en la arena: disco negro de borde neto en At, tumbado sobre la cuesta (Normal) y un poco levantado para que no
	 * se hunda en ella; Radius u Opacity a 0 lo esconden.
	 */
	inline void PlaceDropMarker(UStaticMeshComponent* Comp, const FVector& At, const FVector& Normal, float Radius, float Opacity)
	{
		if (!Comp)
		{
			return;
		}
		const bool bShow = Radius > 1.f && Opacity > 0.01f;
		if (Comp->IsVisible() != bShow)
		{
			Comp->SetVisibility(bShow);
		}
		if (!bShow)
		{
			return;
		}
		const FVector Up = Normal.IsNearlyZero() ? FVector::UpVector : Normal.GetSafeNormal();
		const double DiscScale = static_cast<double>(Radius) / 100.0;
		Comp->SetWorldTransform(FTransform(FRotationMatrix::MakeFromZ(Up).ToQuat(), At + Up * 25.0, FVector(DiscScale, DiscScale, 1.0)));
		TNBeachKit::SetOpacity(TNBeachKit::SoftMID(Comp), Opacity);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Lanzamiento
// ─────────────────────────────────────────────────────────────────────────────

bool ATN_RaceGullStrike::ServerLaunch(ATortugaCharacter* Turtle)
{
	using namespace TNRaceGullStrikeDetail;
	UWorld* World = Turtle ? Turtle->GetWorld() : nullptr;
	if (!World || !Turtle->HasAuthority())
	{
		return false;
	}
	if (CountOf(World, StaticClass()) >= MaxGulls)
	{
		return false;
	}

	// A la primera de todas (solo si va por delante de quien lanza) o, si no hay, al enemigo más cercano por delante.
	AActor* Victim = TNRaceItems::FindTurtleTarget(Turtle, false);
	if (!Victim)
	{
		Victim = FindEnemyAhead(Turtle, EnemySearchRange);
	}
	if (!Victim)
	{
		return false;
	}

	const FVector CourseDir = CourseForwardOf(Turtle);
	const FVector VictimAt = Victim->GetActorLocation();
	FVector AimStart = VictimAt;
	AimStart.Z = static_cast<double>(GroundHeight(Turtle, VictimAt, static_cast<float>(VictimAt.Z) - 90.f));
	// Nace 90 m por detrás del objetivo y 70 m por encima de él.
	FVector StartLoc = VictimAt - CourseDir * static_cast<double>(StartBehind);
	StartLoc.Z = VictimAt.Z + static_cast<double>(StartHeight);
	const FTransform SpawnAt(FRotator(0.f, static_cast<float>(CourseDir.Rotation().Yaw), 0.f), StartLoc);

	ATN_RaceGullStrike* Gull = World->SpawnActorDeferred<ATN_RaceGullStrike>(ATN_RaceGullStrike::StaticClass(), SpawnAt, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Gull)
	{
		return false;
	}
	Gull->SetOwnerTurtle(Turtle);
	Gull->Target = Victim;
	Gull->AimServer = AimStart;
	Gull->AimPoint = FVector_NetQuantize10(AimStart);
	Gull->FinishSpawning(SpawnAt);
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] %s manda una gaviota justiciera contra %s."), *GetNameSafe(Turtle), *GetNameSafe(Victim));
	return true;
}

void ATN_RaceGullStrike::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_RaceGullStrike, Target);
	DOREPLIFETIME(ATN_RaceGullStrike, AimPoint);
	DOREPLIFETIME(ATN_RaceGullStrike, Stained);
}

void ATN_RaceGullStrike::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Las manchas siguen su vida aunque la gaviota ya se haya ido (VisualTick ya no corre tras acabar).
	if (bHasScreen)
	{
		TickMarks(DeltaSeconds);
	}
}

void ATN_RaceGullStrike::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Los pegotes pegados a las tortugas se van con la gaviota.
	for (FMark& Mark : Marks)
	{
		if (UStaticMeshComponent* MarkComp = Mark.Comp.Get())
		{
			MarkComp->DestroyComponent();
		}
	}
	Marks.Reset();
	GroundSfx = nullptr;
	Super::EndPlay(EndPlayReason);
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor
// ─────────────────────────────────────────────────────────────────────────────

void ATN_RaceGullStrike::ServerTick(float DeltaSeconds)
{
	using namespace TNRaceGullStrikeDetail;
	const double AgeSeconds = GetAge();
	if (!bImpactDone)
	{
		ServerTrackAim(DeltaSeconds);
	}
	if (!bDropped && AgeSeconds >= static_cast<double>(ArriveSeconds))
	{
		bDropped = true;
		ServerWarnVictim();
	}
	if (!bImpactDone && AgeSeconds >= static_cast<double>(ImpactAge))
	{
		ServerImpact();
	}
	if (AgeSeconds >= static_cast<double>(LeaveAge))
	{
		ServerFinish(FinishDelay);
	}
}

void ATN_RaceGullStrike::ServerTrackAim(float DeltaSeconds)
{
	using namespace TNRaceGullStrikeDetail;
	AActor* Goal = Target.Get();
	if (bTargetLost || !IsValid(Goal))
	{
		return;
	}
	FVector GoalAt = Goal->GetActorLocation();
	if (ATortugaCharacter* Rival = Cast<ATortugaCharacter>(Goal))
	{
		// Muerta, llegada a la meta o comida por un gusano: el blanco se queda donde está.
		RacerClock -= DeltaSeconds;
		if (RacerClock <= 0.f)
		{
			RacerClock = RacerCheckSeconds;
			if (!IsRacingNow(this, Rival))
			{
				bTargetLost = true;
				return;
			}
		}
	}
	else if (const ATN_BeachEnemy* Enemy = Cast<ATN_BeachEnemy>(Goal))
	{
		// Un enemigo: el punto de su cuerpo más cercano al blanco.
		FVector CapsuleA = FVector::ZeroVector;
		FVector CapsuleB = FVector::ZeroVector;
		float CapsuleRadius = 0.f;
		if (Enemy->GetHitCapsule(CapsuleA, CapsuleB, CapsuleRadius))
		{
			GoalAt = FMath::ClosestPointOnSegment(AimServer, CapsuleA, CapsuleB);
		}
	}

	// El blanco sigue al objetivo por la arena más despacio de lo que corre una tortuga (#636) y, desde justo después de soltar la
	// cagada, cae por la línea que llevaba (TNBeachGullTuning::StepAim): girando corriendo o con la plancha a tiempo se libra.
	const FVector GoalVel = Goal->GetVelocity();
	const FVector2D Next = TNBeachGullTuning::StepAim(TNBeachGullTuning::StrikePlan(), static_cast<float>(GetAge()), AimChase,
		FVector2D(AimServer.X, AimServer.Y), FVector2D(GoalAt.X, GoalAt.Y), FVector2D(GoalVel.X, GoalVel.Y), DeltaSeconds);
	AimServer.X = Next.X;
	AimServer.Y = Next.Y;
	AimServer.Z = static_cast<double>(GroundHeightAt(AimServer, static_cast<float>(AimServer.Z)));
	AimPoint = FVector_NetQuantize10(AimServer);
}

void ATN_RaceGullStrike::ServerWarnVictim()
{
	// Pitido de aviso en la tortuga a la que va la cagada (a un enemigo no hace falta). FindOrAddOn: la que lidera puede no
	// haber cogido nunca un objeto y no tener aún el componente.
	ATortugaCharacter* Warned = Cast<ATortugaCharacter>(Target.Get());
	// El objetivo puede haberse desconectado: no se añaden componentes a un peón en destrucción.
	if (!bTargetLost && IsValid(Warned) && !Warned->IsActorBeingDestroyed())
	{
		if (UTN_RaceItemComponent* Effects = UTN_RaceItemComponent::FindOrAddOn(Warned))
		{
			Effects->MulticastCue(ETNRaceSound::Beep, 1.2f);
		}
	}
}

void ATN_RaceGullStrike::ServerImpact()
{
	using namespace TNRaceGullStrikeDetail;
	bImpactDone = true;
	// Donde cae de verdad, congelado desde ahora: lo primero firme sobre la arena del blanco (la arena, o lo alto de un
	// castillo o de una fortaleza, donde se ha visto su sombra).
	AimServer.Z = static_cast<double>(GroundHeightAt(AimServer, static_cast<float>(AimServer.Z)));
	float SurfaceZ = static_cast<float>(AimServer.Z);
	if (ATN_BeachEnemy::TraceDropSurface(this, AimServer, SurfaceZ, nullptr, this))
	{
		AimServer.Z = static_cast<double>(SurfaceZ);
	}
	AimPoint = FVector_NetQuantize10(AimServer);
	const FVector Impact(AimPoint);

	TArray<ATortugaCharacter*> Victims;
	if (ATN_BeachEnemy::IsRaceLive(this))
	{
		TArray<ATortugaCharacter*> Racers;
		TNRaceItems::GatherRacers(this, Racers);
		for (ATortugaCharacter* Racer : Racers)
		{
			if (!IsValid(Racer))
			{
				continue;
			}
			const FVector RacerAt = Racer->GetActorLocation();
			if (FVector::Dist2D(RacerAt, Impact) > static_cast<double>(ImpactRadius) || FMath::Abs(RacerAt.Z - Impact.Z) >= static_cast<double>(ImpactHeight))
			{
				continue;
			}
			if (!TNRaceItems::CanBeHurt(Racer) || !ATN_BeachEnemy::CanBeHit(Racer))
			{
				continue;
			}
			// Tirada en plancha en el momento justo: le pasa por encima.
			if (TNBeach::IsDodgingByBellyDive(Racer))
			{
				UE_LOG(LogTortunabo, Log, TEXT("[Carrera] %s esquiva la gaviota justiciera en plancha."), *GetNameSafe(Racer));
				continue;
			}
			// El pegote la tumba de espaldas: derribo con ragdoll y mareo, con un empujón hacia fuera del centro.
			FVector Away = RacerAt - Impact;
			Away.Z = 0.0;
			Away = Away.IsNearlyZero() ? -Racer->GetActorForwardVector().GetSafeNormal2D() : Away.GetSafeNormal();
			TNBeach::KnockDownTurtle(Racer, KnockSeconds, Away * static_cast<double>(KnockPush) + FVector(0.0, 0.0, static_cast<double>(KnockUp)));
			Victims.Add(Racer);
		}
		// Si iba a por un enemigo, se marea si el pegote le cae encima (su cuerpo, no solo su centro).
		if (ATN_BeachEnemy* Enemy = Cast<ATN_BeachEnemy>(Target.Get()))
		{
			FVector CapsuleA = FVector::ZeroVector;
			FVector CapsuleB = FVector::ZeroVector;
			float CapsuleRadius = 0.f;
			if (Enemy->AcceptsHitStun() && Enemy->GetHitCapsule(CapsuleA, CapsuleB, CapsuleRadius))
			{
				const FVector Nearest = FMath::ClosestPointOnSegment(Impact, CapsuleA, CapsuleB);
				const double Reach = static_cast<double>(CapsuleRadius);
				if (FVector::Dist2D(Nearest, Impact) <= static_cast<double>(ImpactRadius) + Reach
					&& FMath::Abs(Nearest.Z - Impact.Z) < static_cast<double>(ImpactHeight) + Reach)
				{
					Enemy->ApplyHitStun(EnemyStunSeconds, GetOwnerTurtle());
				}
			}
		}
	}

	// A todas las máquinas: el impacto (con el pegote en la víctima principal) y un pegote más por cada otra tortuga cogida.
	ATortugaCharacter* MainVictim = nullptr;
	for (ATortugaCharacter* Candidate : Victims)
	{
		if (Candidate == Target.Get())
		{
			MainVictim = Candidate;
		}
	}
	if (!MainVictim && Victims.Num() > 0)
	{
		MainVictim = Victims[0];
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] La gaviota justiciera de %s suelta su cagada: %d tortugas derribadas."), *GetNameSafe(GetOwnerTurtle()), Victims.Num());
	// Los pegotes como estado replicado (OnRep_Stained): los ve también quien entra ahora o pierde la multicast.
	Stained.Reset();
	for (ATortugaCharacter* Victim : Victims)
	{
		Stained.Add(Victim);
	}
	ForceNetUpdate();
	MulticastSplat(FVector_NetQuantize10(Impact), MainVictim);
	for (ATortugaCharacter* Extra : Victims)
	{
		if (Extra != MainVictim)
		{
			MulticastStain(Extra);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Todas las máquinas: vuelo y blanco
// ─────────────────────────────────────────────────────────────────────────────

FVector ATN_RaceGullStrike::FlightPosition(double AgeSeconds) const
{
	using namespace TNRaceGullStrikeDetail;
	if (AgeSeconds < static_cast<double>(ArriveSeconds))
	{
		// Llega desde atrás y desde arriba, frenando al acercarse.
		const float ArriveU = FMath::Clamp(static_cast<float>(AgeSeconds / static_cast<double>(ArriveSeconds)), 0.f, 1.f);
		const float Eased = FMath::Lerp(ArriveU, 1.f - FMath::Square(1.f - ArriveU), 0.65f);
		FVector Arriving = LastAnchor - CourseForward * static_cast<double>(StartBehind * (1.f - Eased));
		Arriving.Z = LastAnchor.Z + static_cast<double>(FMath::Lerp(StartHeight, HoverHeight, Eased));
		return Arriving;
	}
	// Tras soltarla: pasa de ir sobre el objetivo a ir sobre el punto de impacto, y se va hacia delante y hacia arriba.
	const float Since = static_cast<float>(AgeSeconds - static_cast<double>(ArriveSeconds));
	const float Blend = Smooth01(Since / LeaveBlendSeconds);
	const FVector Over = FMath::Lerp(LastAnchor, ShownAim, static_cast<double>(Blend));
	FVector Leaving(Over.X, Over.Y, LastAnchor.Z + static_cast<double>(HoverHeight));
	Leaving += CourseForward * static_cast<double>(LeaveSpeed * Since + 0.5f * LeaveAccel * Since * Since);
	Leaving.Z += static_cast<double>(ClimbSpeed * Since + 0.5f * ClimbAccel * Since * Since);
	return Leaving;
}

FVector ATN_RaceGullStrike::GroundNormalAt(const FVector& Where) const
{
	const float Z0 = GroundHeightAt(Where, static_cast<float>(Where.Z));
	const float Zx = GroundHeightAt(Where + FVector(150.0, 0.0, 0.0), Z0);
	const float Zy = GroundHeightAt(Where + FVector(0.0, 150.0, 0.0), Z0);
	const FVector Normal = FVector::CrossProduct(FVector(150.0, 0.0, static_cast<double>(Zx - Z0)), FVector(0.0, 150.0, static_cast<double>(Zy - Z0))).GetSafeNormal();
	return Normal.Z > 0.2 ? Normal : FVector::UpVector;
}

void ATN_RaceGullStrike::UpdateShownAim(float DeltaSeconds, double AgeSeconds)
{
	using namespace TNRaceGullStrikeDetail;
	if (bAimLocked)
	{
		// Ya ha llegado el impacto: el blanco es donde cayó (lo puso ShowSplat).
		return;
	}
	const FVector Replicated(AimPoint);
	// En el servidor, el de verdad; la primera vez y desde el instante del impacto, el replicado sin más.
	if (HasAuthority() || !bShownAimValid || AgeSeconds >= static_cast<double>(ImpactAge))
	{
		ShownAim = Replicated;
		bShownAimValid = true;
		return;
	}
	// Hacia el replicado (llega a 20 Hz) algo más deprisa de lo que se mueve el blanco: sin saltos ni retraso.
	const FVector Delta = Replicated - ShownAim;
	const double Distance = Delta.Size();
	const double MaxStep = static_cast<double>(AimSpeed) * 1.6 * static_cast<double>(DeltaSeconds);
	if (Distance > 3000.0 || Distance <= MaxStep)
	{
		ShownAim = Replicated;
		return;
	}
	ShownAim += Delta / Distance * MaxStep;
}

// ─────────────────────────────────────────────────────────────────────────────
// Efectos (todas las máquinas)
// ─────────────────────────────────────────────────────────────────────────────

void ATN_RaceGullStrike::MulticastSplat_Implementation(FVector_NetQuantize10 Where, ATortugaCharacter* Hit)
{
	if (!bHasScreen)
	{
		return;
	}
	ShowSplat(FVector(Where), Hit);
}

void ATN_RaceGullStrike::MulticastStain_Implementation(ATortugaCharacter* Victim)
{
	if (!bHasScreen || !IsValid(Victim))
	{
		return;
	}
	SpawnShellSplat(Victim);
	TNBeachKit::BurstAt(Droplets, Victim->GetActorLocation() + FVector(0.0, 0.0, 120.0), FVector::UpVector, 14);
}

void ATN_RaceGullStrike::OnRep_Stained()
{
	if (!bHasScreen)
	{
		return;
	}
	// Los que ya tienen el pegote (por la multicast) no repiten: SpawnShellSplat lo comprueba.
	for (ATortugaCharacter* Victim : Stained)
	{
		SpawnShellSplat(Victim);
	}
}

void ATN_RaceGullStrike::ShowSplat(const FVector& Where, ATortugaCharacter* Hit)
{
	using namespace TNRaceGullStrikeDetail;
	if (bSplatShown)
	{
		return;
	}
	bSplatShown = true;
	// El blanco se queda donde cayó.
	bAimLocked = true;
	ShownAim = Where;
	bShownAimValid = true;

	if (UTN_RaceItemSynthComponent* GroundVoice = GetGroundSfx(Where))
	{
		GroundVoice->Play(ETNRaceSound::Splat, 1.f, 1.3f);
	}
	TNBeachKit::BurstAt(Droplets, Where + FVector(0.0, 0.0, 40.0), FVector::UpVector, 26);
	SpawnSandSplat(Where);
	if (IsValid(Hit))
	{
		SpawnShellSplat(Hit);
		TNBeachKit::BurstAt(Droplets, Hit->GetActorLocation() + FVector(0.0, 0.0, 120.0), FVector::UpVector, 14);
	}
	UTN_BeachCameraShake::Kick(this, Where, 0.4f, 300.f, 2500.f);
}

UTN_RaceItemSynthComponent* ATN_RaceGullStrike::GetGroundSfx(const FVector& Where)
{
	if (!bHasScreen)
	{
		return nullptr;
	}
	if (!GroundSfx)
	{
		GroundSfx = UTN_RaceItemSynthComponent::AttachTo(this, Where, 2500.f, 14000.f);
		if (GroundSfx)
		{
			// Fijo en el suelo: no sigue a la gaviota.
			GroundSfx->SetAbsolute(true, true, true);
		}
	}
	if (GroundSfx)
	{
		GroundSfx->SetWorldLocation(Where);
	}
	return GroundSfx;
}

void ATN_RaceGullStrike::SpawnSandSplat(const FVector& Where)
{
	using namespace TNRaceGullStrikeDetail;
	using TNProcMesh::FTNProcMeshBuffers;
	UStaticMesh* SplatMesh = TNBeachKit::CachedMesh(TEXT("Beach.Splat"), [](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildSplat(M, 77u); });
	UStaticMeshComponent* Comp = TNBeachKit::AddPart(this, GetRootComponent(), SplatMesh, FVector::ZeroVector, false);
	if (!Comp)
	{
		return;
	}
	// Tumbada sobre la cuesta, con un giro al azar (el mismo para todos no hace falta: es solo una mancha).
	const float SplatYaw = 360.f * TNBeachKit::Hash01(static_cast<uint32>(GetAge() * 1000.0));
	const FVector Normal = GroundNormalAt(Where);
	const float SplatSize = ImpactRadius / 100.f;
	Comp->SetAbsolute(true, true, true);
	Comp->SetWorldTransform(FTransform(FRotationMatrix::MakeFromZ(Normal).ToQuat() * FRotator(0.f, SplatYaw, 0.f).Quaternion(), Where + Normal * 4.0,
		FVector(static_cast<double>(SplatSize))));

	FMark Mark;
	Mark.Comp = Comp;
	Mark.Born = MarkClock;
	Mark.Life = SplatLife;
	Mark.BaseScale = SplatSize;
	Mark.bFlat = true;
	Marks.Add(Mark);
}

void ATN_RaceGullStrike::SpawnShellSplat(ATortugaCharacter* Victim)
{
	using namespace TNRaceGullStrikeDetail;
	using TNProcMesh::FTNProcMeshBuffers;
	// Uno por tortuga: puede llegar por la multicast y por OnRep_Stained.
	if (!IsValid(Victim) || ShellSplatShown.Contains(TWeakObjectPtr<ATortugaCharacter>(Victim)))
	{
		return;
	}
	ShellSplatShown.Add(TWeakObjectPtr<ATortugaCharacter>(Victim));
	UStaticMesh* StainMesh = TNBeachKit::CachedMesh(TEXT("Beach.ShellSplat"), [](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildShellSplat(M); });
	UStaticMeshComponent* Comp = TNBeachKit::AddPart(this, GetRootComponent(), StainMesh, FVector::ZeroVector, false);
	if (!Comp)
	{
		return;
	}
	// Pegado a su espalda (al hueso del caparazón): con el ragdoll del derribo va con el cuerpo.
	TNBeachKit::AttachToTurtleBack(Comp, Victim, StainScale, 24.f, 6.f);

	FMark Mark;
	Mark.Comp = Comp;
	Mark.Born = MarkClock;
	Mark.Life = StainLife;
	Mark.BaseScale = StainScale;
	Mark.bFlat = false;
	Marks.Add(Mark);
}

void ATN_RaceGullStrike::TickMarks(float DeltaSeconds)
{
	MarkClock += DeltaSeconds;
	for (int32 MarkIndex = Marks.Num() - 1; MarkIndex >= 0; --MarkIndex)
	{
		FMark& Mark = Marks[MarkIndex];
		UStaticMeshComponent* MarkComp = Mark.Comp.Get();
		const float MarkAge = MarkClock - Mark.Born;
		// Se va a su hora o si su tortuga ya no está (sin la malla a la que iba pegada no tiene dónde estar).
		if (!MarkComp || MarkAge >= Mark.Life || (!Mark.bFlat && !MarkComp->GetAttachParent()))
		{
			if (MarkComp)
			{
				MarkComp->DestroyComponent();
			}
			Marks.RemoveAt(MarkIndex);
			continue;
		}
		// El último segundo se encoge (la de la arena, a lo ancho; la del caparazón, entera).
		const float TimeLeft = Mark.Life - MarkAge;
		if (TimeLeft < 1.f)
		{
			const float ShrunkScale = Mark.BaseScale * FMath::Max(0.01f, TimeLeft);
			MarkComp->SetWorldScale3D(FVector(static_cast<double>(ShrunkScale), static_cast<double>(ShrunkScale), static_cast<double>(Mark.bFlat ? Mark.BaseScale : ShrunkScale)));
		}
	}
}

void ATN_RaceGullStrike::OnFinished()
{
	// Se esconde la gaviota y lo que cae; las manchas se quedan hasta su hora (Tick las sigue moviendo).
	using namespace TNRaceGullStrikeDetail;
	if (GullRoot)
	{
		GullRoot->SetVisibility(false, true);
	}
	TNBeachKit::PlaceShadow(GullShadow, FVector::ZeroVector, 0.f);
	if (Dropping)
	{
		Dropping->SetVisibility(false);
	}
	PlaceDropMarker(DropShadow, FVector::ZeroVector, FVector::UpVector, 0.f, 0.f);
	Droplets.RateScale = 0.f;
	Trail.RateScale = 0.f;
	if (UInstancedStaticMeshComponent* DropletMesh = Droplets.ISM.Get())
	{
		DropletMesh->SetVisibility(false);
	}
	if (UInstancedStaticMeshComponent* TrailMesh = Trail.ISM.Get())
	{
		TrailMesh->SetVisibility(false);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Visual
// ─────────────────────────────────────────────────────────────────────────────

void ATN_RaceGullStrike::BuildVisuals()
{
	using namespace TNRaceGullStrikeDetail;
	using TNProcMesh::FTNProcMeshBuffers;
	CourseForward = CourseForwardOf(this);
	ShadowZ = static_cast<float>(GetActorLocation().Z);

	// La gaviota de la fauna (la de las zonas de gaviotas): piezas compartidas y la mandíbula de abajo.
	TArray<TNFauna::FTNFaunaPart> Parts;
	TNFauna::FTNFaunaRig FaunaRig;
	TNFauna::TNFaunaBuildSpecies(TNFauna::ETNFaunaSpecies::Gull, Parts, FaunaRig);

	GullRoot = NewObject<USceneComponent>(this, NAME_None, RF_Transient);
	GullRoot->SetupAttachment(GetRootComponent());
	GullRoot->SetAbsolute(true, true, true);
	GullRoot->RegisterComponent();
	// Donde nace (el actor ya está en el punto de salida) y mirando hacia donde avanza la carrera.
	GullRoot->SetWorldTransform(FTransform(FRotator(0.f, static_cast<float>(CourseForward.Rotation().Yaw), 0.f), GetActorLocation(), FVector(static_cast<double>(GullScale))));

	UStaticMeshComponent* BodyComp = nullptr;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (int32 PartIndex = 0; PartIndex < Parts.Num(); ++PartIndex)
		{
			const TNFauna::FTNFaunaPart& Part = Parts[PartIndex];
			const bool bIsBody = Part.Bone == TNFauna::ETNFaunaBone::Body;
			if ((Pass == 0) != bIsBody)
			{
				continue;
			}
			const FTNProcMeshBuffers& Buffers = Part.Mesh;
			UStaticMesh* PartMesh = TNBeachKit::CachedMesh(FString::Printf(TEXT("Beach.Gull.%d"), PartIndex), [&Buffers](FTNProcMeshBuffers& M) { M = Buffers; });
			USceneComponent* Parent = bIsBody ? GullRoot.Get() : static_cast<USceneComponent*>(BodyComp);
			UStaticMeshComponent* Comp = TNBeachKit::AddPart(this, Parent ? Parent : GullRoot.Get(), PartMesh, Part.Pivot, false);
			if (bIsBody && !BodyComp)
			{
				BodyComp = Comp;
			}
			const int32 Index = GullParts.Add(Comp);
			PartPivots.Add(Part.Pivot);
			switch (Part.Bone)
			{
			case TNFauna::ETNFaunaBone::WingL: WingLeftPart = Index; break;
			case TNFauna::ETNFaunaBone::WingR: WingRightPart = Index; break;
			case TNFauna::ETNFaunaBone::LegBL: LegLeftPart = Index; break;
			case TNFauna::ETNFaunaBone::LegBR: LegRightPart = Index; break;
			case TNFauna::ETNFaunaBone::Head: HeadPart = Index; break;
			default: break;
			}
		}
	}
	UStaticMeshComponent* HeadComp = GullParts.IsValidIndex(HeadPart) ? GullParts[HeadPart].Get() : nullptr;
	UStaticMesh* JawMesh = TNBeachKit::CachedMesh(TEXT("Beach.Gull.Jaw"), [](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildBirdJaw(M, false); });
	Jaw = HeadComp ? TNBeachKit::AddPart(this, HeadComp, JawMesh, TNBeachMeshes::BirdGeom(false).BeakBase, false) : nullptr;

	GullShadow = TNBeachKit::AddShadow(this, 0.38f);
	TNBeachKit::PlaceShadow(GullShadow, FVector::ZeroVector, 0.f);

	// La cagada que cae y, en la arena, la sombra dura y negra que marca dónde va a caer (borde neto, opacidad en el material).
	UStaticMesh* DropMesh = TNBeachKit::CachedMesh(TEXT("Beach.Dropping"), [](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildDropping(M); });
	Dropping = TNBeachKit::AddPart(this, GetRootComponent(), DropMesh, FVector::ZeroVector, true);
	if (Dropping)
	{
		Dropping->SetAbsolute(true, true, true);
		Dropping->SetVisibility(false);
	}
	DropShadow = TNBeachKit::AddShadow(this, 0.6f);
	if (DropShadow)
	{
		DropShadow->SetStaticMesh(TNBeachKit::ShadowDiscEdge(MarkerEdge));
		DropShadow->SetTranslucentSortPriority(4);
		SetupHardMarker(DropShadow, 0.f);
		DropShadow->SetVisibility(false);
	}

	// Gotas del impacto y estela de gotitas de la cagada al caer.
	using TNAmbientFX::EShape;
	TNAmbientFX::FEmitterDesc DropletDesc = TNBeachKit::MakeDesc(EShape::Drop, FLinearColor(0.97f, 0.97f, 0.94f), false, 1.f, 40, 0.f, 1000.f, -1800.f, 0.5f, 1.f, 55.f, 40.f);
	DropletDesc.Spread = 0.9f;
	DropletDesc.SpawnRadius = 150.f;
	TNBeachKit::InitEmitter(Droplets, this, DropletDesc, GetUniqueID() + 51u);
	TNAmbientFX::FEmitterDesc TrailDesc = TNBeachKit::MakeDesc(EShape::Drop, FLinearColor(0.98f, 0.98f, 0.95f), false, 1.f, 40, 0.f, 80.f, -300.f, 0.35f, 0.6f, 45.f, 12.f);
	TrailDesc.Spread = 0.5f;
	TrailDesc.SpawnRadius = 40.f;
	TNBeachKit::InitEmitter(Trail, this, TrailDesc, GetUniqueID() + 53u);
}

void ATN_RaceGullStrike::PoseGullPart(int32 PartIndex, const FRotator& PartRotation)
{
	if (GullParts.IsValidIndex(PartIndex) && PartPivots.IsValidIndex(PartIndex))
	{
		TNBeachKit::Pose(GullParts[PartIndex], PartPivots[PartIndex], PartRotation);
	}
}

void ATN_RaceGullStrike::PoseGull(float DeltaSeconds, double AgeSeconds)
{
	using namespace TNRaceGullStrikeDetail;
	// Planea con rachas de aleteo; al llegar frena con las alas abiertas y las patas por delante; al soltarla agacha la
	// cabeza con el pico abierto; después sube aleteando fuerte.
	float Flap = 6.f + 3.f * FMath::Sin(AnimClock * 1.3f);
	float Sweep = 0.f;
	float Tuck = -70.f;
	float HeadPitch = 0.f;
	float HeadYaw = 6.f * FMath::Sin(AnimClock * 0.9f);
	float JawTarget = 0.f;
	if (AgeSeconds < static_cast<double>(ArriveSeconds) - 0.8)
	{
		Flap = 38.f * FMath::Sin(AnimClock * 2.f * PI * 2.8f);
		Tuck = -55.f;
		HeadPitch = -8.f;
	}
	else if (AgeSeconds < static_cast<double>(ArriveSeconds))
	{
		Sweep = -15.f;
		Flap = 28.f * FMath::Sin(AnimClock * 2.f * PI * 4.f);
		Tuck = 35.f;
		HeadPitch = -20.f;
		HeadYaw = 0.f;
		JawTarget = 18.f;
	}
	else if (AgeSeconds < static_cast<double>(ArriveSeconds) + 0.6)
	{
		Flap = 32.f * FMath::Sin(AnimClock * 2.f * PI * 3.4f);
		Tuck = 20.f;
		HeadPitch = -25.f;
		HeadYaw = 0.f;
		JawTarget = 30.f;
	}
	else
	{
		Flap = 45.f * FMath::Sin(AnimClock * 2.f * PI * 3.2f);
		Tuck = -60.f;
	}
	if (JawOpenLeft > 0.f)
	{
		JawOpenLeft -= DeltaSeconds;
		JawTarget = FMath::Max(JawTarget, 28.f);
	}
	JawAngle = FMath::FInterpTo(JawAngle, JawTarget, DeltaSeconds, 18.f);
	PoseGullPart(WingLeftPart, FRotator(0.f, -Sweep, Flap));
	PoseGullPart(WingRightPart, FRotator(0.f, Sweep, -Flap));
	PoseGullPart(LegLeftPart, FRotator(Tuck, 0.f, 0.f));
	PoseGullPart(LegRightPart, FRotator(Tuck, 0.f, 0.f));
	PoseGullPart(HeadPart, FRotator(HeadPitch, HeadYaw, 0.f));
	if (Jaw)
	{
		TNBeachKit::Pose(Jaw, TNBeachMeshes::BirdGeom(false).BeakBase, FRotator(-JawAngle, 0.f, 0.f));
	}
}

void ATN_RaceGullStrike::VisualTick(float DeltaSeconds)
{
	using namespace TNRaceGullStrikeDetail;
	if (!GullRoot)
	{
		return;
	}
	AnimClock += DeltaSeconds;
	const float Dt = FMath::Max(DeltaSeconds, 1.0e-3f);
	const double AgeSeconds = GetAge();
	FVector View = GetActorLocation();
	TNBeachKit::LocalCamera(GetWorld(), View);

	// Dónde ve esta máquina al objetivo (ancla del vuelo) y al blanco de la cagada.
	AActor* Goal = Target.Get();
	if (IsValid(Goal))
	{
		LastAnchor = Goal->GetActorLocation();
		bAnchorValid = true;
	}
	else if (!bAnchorValid)
	{
		LastAnchor = FVector(AimPoint) + FVector(0.0, 0.0, 90.0);
		bAnchorValid = true;
	}
	UpdateShownAim(Dt, AgeSeconds);

	// Sitio y giro de la gaviota: los de la fórmula, con la velocidad suavizada para orientarla.
	const FVector NewPos = FlightPosition(AgeSeconds);
	const bool bFirst = !bPoseValid;
	bPoseValid = true;
	if (bFirst)
	{
		GullVel = FVector::ZeroVector;
		GullYaw = static_cast<float>(CourseForward.Rotation().Yaw);
	}
	else
	{
		const FVector RawVel = ((NewPos - GullPos) / static_cast<double>(Dt)).GetClampedToMaxSize(9000.0);
		GullVel = FMath::Lerp(GullVel, RawVel, static_cast<double>(1.f - FMath::Exp(-Dt / 0.15f)));
	}
	GullPos = NewPos;
	if (!bFirst && GullVel.SizeSquared2D() > 2500.0)
	{
		const float WantedYaw = static_cast<float>(GullVel.Rotation().Yaw);
		const float TurnRate = FMath::FindDeltaAngleDegrees(GullYaw, WantedYaw) / Dt;
		GullYaw = FMath::FixedTurn(GullYaw, WantedYaw, 400.f * Dt);
		GullBank = FMath::FInterpTo(GullBank, FMath::Clamp(TurnRate * 0.3f, -40.f, 40.f), Dt, 3.f);
	}
	const float ClimbDegrees = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(GullVel.Z, FMath::Max(1.0, GullVel.Size2D()))));
	GullPitch = FMath::FInterpTo(GullPitch, FMath::Clamp(ClimbDegrees, -50.f, 45.f), Dt, 5.f);
	const FRotator GullRot(GullPitch, GullYaw, GullBank);
	GullRoot->SetWorldTransform(FTransform(GullRot, GullPos, FVector(static_cast<double>(GullScale))));
	// El actor va con la gaviota: el sonido de la base sale de ella.
	SetActorLocation(GullPos);

	const bool bNear = FVector::DistSquared(View, GullPos) < FMath::Square(static_cast<double>(ViewRange));
	if (bNear)
	{
		PoseGull(Dt, AgeSeconds);
	}

	// Graznido al nacer.
	if (!bBirthSoundPlayed)
	{
		bBirthSoundPlayed = true;
		if (AgeSeconds < 1.0)
		{
			PlaySfx(ETNRaceSound::Squawk, 1.f, 1.3f);
			JawOpenLeft = 0.4f;
		}
	}

	// Sombra de la gaviota en la arena: la de verdad, bajo el cuerpo; más grande y tenue cuanto más alta.
	const TNBeachMeshes::FBirdGeom Geom = TNBeachMeshes::BirdGeom(false);
	const FVector BodyAt = GullPos + GullRot.RotateVector(Geom.BodyPivot * static_cast<double>(GullScale));
	ShadowTimer -= Dt;
	if (ShadowTimer <= 0.f)
	{
		ShadowTimer = 0.1f;
		ShadowZ = GroundHeightAt(BodyAt, ShadowZ);
	}
	const float HeightAbove = FMath::Max(0.f, static_cast<float>(BodyAt.Z) - ShadowZ);
	const float FreeK = FMath::Clamp(HeightAbove / 8000.f, 0.f, 1.f);
	TNBeachKit::PlaceShadow(GullShadow, FVector(BodyAt.X, BodyAt.Y, static_cast<double>(ShadowZ)), bNear ? GullSpan * 0.2f * (0.75f + 0.5f * FreeK) : 0.f);

	// Suelta la cagada: silbido de lo que cae y pico abierto. Desde dónde: bajo la cola.
	if (!bReleaseShown && AgeSeconds >= static_cast<double>(ArriveSeconds))
	{
		bReleaseShown = true;
		DropStart = GullPos + GullRot.RotateVector(FVector(-6.5, 0.0, 5.0) * static_cast<double>(GullScale));
		bDropStartValid = true;
		JawOpenLeft = 0.45f;
		if (AgeSeconds < static_cast<double>(ArriveSeconds) + 1.0)
		{
			PlaySfx(ETNRaceSound::Fall, 1.f, 1.f);
		}
	}

	// Cagada que cae (acelerando, hacia el blanco) y su sombra dura y negra: nace pequeña al soltarla y crece hasta el radio del
	// impacto según cae.
	bool bShowDrop = false;
	if (Dropping && bDropStartValid && !bSplatShown && AgeSeconds >= static_cast<double>(ArriveSeconds) && AgeSeconds < static_cast<double>(ImpactAge))
	{
		// Donde cae de verdad: lo primero firme desde arriba (la arena, o lo alto de un castillo o de una fortaleza), con la
		// misma traza que el impacto del servidor (ServerImpact). La sombra y la cagada van ahí, no a la arena de debajo.
		DropNormalTimer -= Dt;
		if (DropNormalTimer <= 0.f)
		{
			DropNormalTimer = 0.1f;
			float SurfaceZ = static_cast<float>(ShownAim.Z);
			FVector SurfaceNormal = FVector::UpVector;
			DropLift = ATN_BeachEnemy::TraceDropSurface(this, ShownAim, SurfaceZ, &SurfaceNormal, this) ? SurfaceZ - static_cast<float>(ShownAim.Z) : 0.f;
			// En la arena, su cuesta de siempre; encima de algo, la cara en la que cae (si es más o menos plana).
			DropNormal = FMath::Abs(DropLift) < 30.f ? GroundNormalAt(ShownAim) : (SurfaceNormal.Z > 0.5 ? SurfaceNormal : FVector::UpVector);
		}
		const FVector Landing = ShownAim + FVector(0.0, 0.0, static_cast<double>(DropLift));
		const float FallC = FMath::Clamp(static_cast<float>((AgeSeconds - static_cast<double>(ArriveSeconds)) / static_cast<double>(FallSeconds)), 0.f, 1.f);
		const float Along = FMath::Pow(FallC, 1.8f);
		const double Across = static_cast<double>(Smooth01(FallC * 2.f));
		const FVector DropAt(FMath::Lerp(DropStart.X, Landing.X, Across), FMath::Lerp(DropStart.Y, Landing.Y, Across),
			FMath::Lerp(DropStart.Z, Landing.Z + 40.0, static_cast<double>(Along)));
		Dropping->SetWorldTransform(FTransform(FRotator(8.f * FMath::Sin(AnimClock * 9.f), AnimClock * 60.f, 0.f), DropAt, FVector(static_cast<double>(DropScale))));
		bShowDrop = true;

		const float Grow = 0.45f * FallC + 0.55f * FMath::Pow(FallC, 1.6f);
		const float FadeIn = FMath::Clamp(static_cast<float>(AgeSeconds - static_cast<double>(ArriveSeconds)) / MarkerFadeIn, 0.f, 1.f);
		PlaceDropMarker(DropShadow, Landing, DropNormal, bNear ? FMath::Lerp(MarkerStartRadius, ImpactRadius, Grow) : 0.f, MarkerOpacity * FadeIn);

		// Estela de gotitas tras la cagada.
		TrailTimer -= Dt;
		if (TrailTimer <= 0.f)
		{
			TrailTimer = 0.04f;
			TNBeachKit::BurstAt(Trail, DropAt + FVector(0.0, 0.0, 90.0), FVector::UpVector, 1);
		}
	}
	if (Dropping && Dropping->IsVisible() != bShowDrop)
	{
		Dropping->SetVisibility(bShowDrop);
	}
	if (!bShowDrop)
	{
		PlaceDropMarker(DropShadow, FVector::ZeroVector, FVector::UpVector, 0.f, 0.f);
	}

	// Seguro: si a esta máquina no le ha llegado el multicast del impacto, lo pinta ella (sin pegote en la víctima).
	if (!bSplatShown && AgeSeconds >= static_cast<double>(ImpactAge + SplatFallbackDelay))
	{
		// Encima de lo que haya, como el impacto del servidor (el blanco ya puede traer esa altura replicada).
		FVector Where = ShownAim;
		float SurfaceZ = static_cast<float>(Where.Z);
		if (ATN_BeachEnemy::TraceDropSurface(this, Where, SurfaceZ, nullptr, this))
		{
			Where.Z = static_cast<double>(SurfaceZ);
		}
		ShowSplat(Where, nullptr);
	}

	TNBeachKit::TickEmitterIfBusy(Droplets, DeltaSeconds, View);
	TNBeachKit::TickEmitterIfBusy(Trail, DeltaSeconds, View);
}
