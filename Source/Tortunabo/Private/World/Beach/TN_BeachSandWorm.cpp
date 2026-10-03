#include "World/Beach/TN_BeachSandWorm.h"
#include "World/Beach/TN_BeachCameraShake.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "World/Beach/TN_BeachRaceGenerator.h"
#include "World/Beach/TN_BeachSandWormSynth.h"
#include "World/Beach/TN_BeachStunComponent.h"
#include "TN_BeachEnemyKit.h"
#include "TN_BeachSandWormMeshes.h"
#include "Camera/CameraTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Core/TN_Log.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_ShellComponent.h"
#include "Player/TN_TurtleAnimInstance.h"
#include "Player/TortugaCharacter.h"
#include "VR/TN_VRMode.h"

/**
 * Escena del gusano de arena (tiempos en segundos de escena, sobre EatSeconds = 3,2; con desfase, la escena entera va un
 * poco más deprisa para acabar a la vez):
 *   0,00-0,70  aviso: la arena tiembla y se hunde en un remolino (polvo, piedrecitas, retumbar, temblor de cámara).
 *   0,70-1,30  sale: la boca revienta la arena con los labios abriéndose como una flor y sube en vertical 25 m con la
 *              tortuga dentro (pataleando y dando vueltas).
 *   1,20-1,55  bocado: la tortuga sube un poco dentro de la boca, los labios se cierran (1,43) y desaparece (1,55): «¡ÑAM!».
 *   1,55-2,25  trago: mastica, se dobla en arco y un bulto baja por el cuerpo hasta la arena («glup», «glup»).
 *   2,35       eructo: abre un poco la boca y suelta una nube de arena.
 *   2,60-3,00  se hunde acelerando por su agujero.
 *   3,00-3,20  el cráter se cierra.
 */
namespace TNSandWorm
{
	// ── Tiempos ──
	constexpr float WarnEnd = 0.7f;
	constexpr float RiseEnd = 1.3f;
	constexpr float TossStart = 1.2f;
	constexpr float LipsShut = 1.43f;
	constexpr float BiteTime = 1.55f;
	constexpr float SwallowEnd = 2.25f;
	constexpr float BurpTime = 2.35f;
	constexpr float SinkStart = 2.6f;
	constexpr float SinkEnd = 3.0f;
	constexpr float CraterShut = ATN_BeachSandWorm::EatSeconds;
	/** Desfase entre gusanos de la misma tanda: cuánto se separa cada uno del anterior y como mucho (s). */
	constexpr float StaggerStep = 0.11f;
	constexpr float MaxStagger = 0.3f;

	// ── Medidas (cm) ──
	/** Lo que asoma de la arena en lo más alto (25 m) y tramo de arriba que se dobla en arco al tragar. */
	constexpr double PeakOut = 2500.0;
	constexpr double BendLength = 1000.0;
	/** Anillos del cuerpo y separación entre ellos por la columna (la malla de cada uno es algo más larga: se solapan). */
	constexpr int32 NumSegments = 13;
	constexpr double SegmentSpacing = 255.0;
	/** Ondulación de lado: largo de onda, amplitud y rapidez. */
	constexpr double SwayWave = 1500.0;
	constexpr double SwayAmp = 60.0;
	constexpr double SwaySpeed = 5.5;
	/** Justo antes de romper la arena (con los labios cerrados, la cúpula queda a ras del fondo del remolino). */
	constexpr double PreBreach = -240.0;
	/** La cabeza se dibuja a partir de aquí (más abajo está bajo la arena). */
	constexpr double HeadShowFrom = -420.0;
	/** Cráter, remolino del aviso y cuánto se hunde la tortuga en él. */
	constexpr float CraterRadius = 520.f;
	constexpr float WhirlRadius = 430.f;
	constexpr float WhirlDepth = 95.f;
	constexpr float SinkIntoWhirl = 35.f;
	/** La tortuga va así de metida por dentro del borde de la boca; el bote antes del bocado. */
	constexpr float InMouth = 70.f;
	constexpr float TossHeight = 190.f;
	/** Labios cerrados (hacia dentro: cúpula sobre la boca) y abiertos (hacia fuera: flor), en grados. */
	constexpr float LipClosedDeg = -58.f;
	constexpr float LipOpenDeg = 78.f;

	// ── Cámara de la tortuga comida ──
	/** Distancia en planta y altura sobre el suelo durante el aviso, durante la escena y después; mínimo si algo tapa. */
	constexpr float CamWarnDist = 1500.f;
	constexpr float CamWarnHeight = 380.f;
	constexpr float CamSceneDist = 3200.f;
	constexpr float CamSceneHeight = 850.f;
	constexpr float CamAfterHeight = 700.f;
	constexpr float CamMinDist = 800.f;
	constexpr float CamBlend = 0.6f;

	inline float Smooth01(float X)
	{
		const float C = FMath::Clamp(X, 0.f, 1.f);
		return C * C * (3.f - 2.f * C);
	}

	inline float EaseOut3(float X)
	{
		const float C = 1.f - FMath::Clamp(X, 0.f, 1.f);
		return 1.f - C * C * C;
	}

	/** Sale deprisa, se pasa un poco y vuelve (1 en X = 1). */
	inline float EaseOutBack(float X)
	{
		constexpr float C1 = 1.4f;
		const float C = FMath::Clamp(X, 0.f, 1.f) - 1.f;
		return 1.f + (C1 + 1.f) * C * C * C + C1 * C * C;
	}

	/** Joroba suave: 0 fuera de [A, B] y 1 en medio. */
	inline float Bump(float Tau, float A, float B)
	{
		if (Tau <= A || Tau >= B)
		{
			return 0.f;
		}
		return FMath::Sin(UE_PI * (Tau - A) / (B - A));
	}

	/** Cuerpo fuera de la arena (cm de columna, del suelo al borde de la boca). */
	inline double ExposedAt(float Tau)
	{
		constexpr float Climb = 0.16f;
		const double Buried = -(TNSandWormMeshes::HeadLength + 700.0);
		if (Tau < WarnEnd - Climb)
		{
			return Buried;
		}
		if (Tau < WarnEnd)
		{
			return FMath::Lerp(Buried, PreBreach, static_cast<double>(Smooth01((Tau - (WarnEnd - Climb)) / Climb)));
		}
		if (Tau < RiseEnd)
		{
			return FMath::Lerp(PreBreach, PeakOut, static_cast<double>(EaseOutBack((Tau - WarnEnd) / (RiseEnd - WarnEnd))));
		}
		if (Tau < SinkStart)
		{
			// Arriba: se agacha un poco al cerrar la boca, da un respingo con el eructo y respira.
			const double Dip = 0.035 * Bump(Tau, LipsShut, BiteTime + 0.25f);
			const double Hop = 0.03 * Bump(Tau, BurpTime - 0.05f, BurpTime + 0.3f);
			const double Breath = 0.012 * FMath::Sin(Tau * 4.3f) * Bump(Tau, RiseEnd, SinkStart);
			return PeakOut * (1.0 - Dip + Hop + Breath);
		}
		if (Tau < SinkEnd)
		{
			const float U = (Tau - SinkStart) / (SinkEnd - SinkStart);
			return FMath::Lerp(PeakOut, Buried, static_cast<double>(U * U));
		}
		return Buried;
	}

	/** Arco de la parte de arriba (grados): recta mientras sube y come, se dobla al tragar, se endereza al eructar y al hundirse. */
	inline float BendDegAt(float Tau)
	{
		constexpr int32 NumKeys = 8;
		const float KeyT[NumKeys] = { 0.f, BiteTime, 1.95f, 2.2f, BurpTime - 0.05f, BurpTime + 0.12f, SinkStart, 2.95f };
		const float KeyDeg[NumKeys] = { 0.f, 0.f, 72.f, 64.f, 58.f, 30.f, 40.f, 0.f };
		float Deg = KeyDeg[NumKeys - 1];
		if (Tau <= KeyT[0])
		{
			Deg = KeyDeg[0];
		}
		else
		{
			for (int32 i = 0; i + 1 < NumKeys; ++i)
			{
				if (Tau < KeyT[i + 1])
				{
					const float U = (Tau - KeyT[i]) / FMath::Max(0.001f, KeyT[i + 1] - KeyT[i]);
					Deg = FMath::Lerp(KeyDeg[i], KeyDeg[i + 1], Smooth01(U));
					break;
				}
			}
		}
		// Algo de vida mientras está fuera.
		return Deg + 5.f * FMath::Sin(Tau * 3.7f) * Bump(Tau, WarnEnd, SinkEnd);
	}

	/** Apertura de los labios (grados; ver LipClosedDeg y LipOpenDeg). */
	inline float LipDegAt(float Tau)
	{
		if (Tau < WarnEnd)
		{
			return LipClosedDeg;
		}
		if (Tau < WarnEnd + 0.22f)
		{
			return FMath::Lerp(LipClosedDeg, LipOpenDeg, EaseOutBack((Tau - WarnEnd) / 0.22f));
		}
		if (Tau < LipsShut)
		{
			return LipOpenDeg + 5.f * FMath::Sin(Tau * 31.f);
		}
		if (Tau < BiteTime)
		{
			const float U = (Tau - LipsShut) / (BiteTime - LipsShut);
			return FMath::Lerp(LipOpenDeg, LipClosedDeg, U * U);
		}
		if (Tau < BiteTime + 0.5f)
		{
			// Mastica: se entreabre tres veces, cada vez menos.
			const float Chew = Tau - BiteTime;
			return LipClosedDeg + 22.f * FMath::Abs(FMath::Sin(UE_PI * Chew / 0.17f)) * (1.f - Chew / 0.5f);
		}
		const float Burp = Bump(Tau, BurpTime - 0.05f, BurpTime + 0.3f);
		return LipClosedDeg + (LipOpenDeg * 0.45f - LipClosedDeg) * Burp;
	}

	/** Grosor y largo de la cabeza: se hincha al morder y al eructar. */
	inline float HeadRadialAt(float Tau)
	{
		return 1.f + 0.1f * Bump(Tau, BiteTime - 0.06f, BiteTime + 0.22f) + 0.14f * Bump(Tau, BurpTime - 0.05f, BurpTime + 0.3f);
	}

	inline float HeadLengthAt(float Tau)
	{
		return 1.f - 0.1f * Bump(Tau, BiteTime - 0.06f, BiteTime + 0.22f) + 0.05f * Bump(Tau, BurpTime - 0.05f, BurpTime + 0.3f);
	}

	/** Bulto de la tortuga tragada: cuánto engorda el cuerpo a Along cm de columna (0 fuera del trago). */
	inline double BulgeAt(float Tau, double Along, double Exposed)
	{
		constexpr float From = BiteTime + 0.1f;
		if (Tau <= From || Tau >= SwallowEnd)
		{
			return 0.0;
		}
		const float U = (Tau - From) / (SwallowEnd - From);
		const double Center = FMath::Lerp(Exposed - TNSandWormMeshes::HeadLength * 0.6, -150.0, static_cast<double>(FMath::Pow(U, 1.2f)));
		const double Amp = 0.32 * FMath::Min(1.0, U * 5.0) * FMath::Min(1.0, (1.0 - U) * 4.0 + 0.3);
		const double Rel = (Along - Center) / 230.0;
		return Amp * FMath::Exp(-Rel * Rel);
	}

	/** Remolino del aviso (radio y hondo en cm; 0 = no se ve). */
	inline float WhirlRadiusAt(float Tau)
	{
		if (Tau < 0.f || Tau >= WarnEnd + 0.06f)
		{
			return 0.f;
		}
		return WhirlRadius * EaseOut3(Tau / 0.55f);
	}

	inline float WhirlDepthAt(float Tau)
	{
		return WhirlDepth * Smooth01(Tau / WarnEnd);
	}

	/** Cráter: aparece al reventar la arena, se queda mientras el gusano está fuera y se cierra al final. */
	inline float CraterRadiusAt(float Tau)
	{
		if (Tau < WarnEnd || Tau >= CraterShut)
		{
			return 0.f;
		}
		if (Tau < WarnEnd + 0.15f)
		{
			return FMath::Lerp(WhirlRadius, CraterRadius * 1.08f, EaseOut3((Tau - WarnEnd) / 0.15f));
		}
		if (Tau < WarnEnd + 0.4f)
		{
			return FMath::Lerp(CraterRadius * 1.08f, CraterRadius, Smooth01((Tau - WarnEnd - 0.15f) / 0.25f));
		}
		if (Tau < SinkEnd)
		{
			return CraterRadius;
		}
		return CraterRadius * (1.f - Smooth01((Tau - SinkEnd) / (CraterShut - SinkEnd)));
	}

	/** Retumbar bajo la arena (sonido y temblor de cámara), 0..1. */
	inline float RumbleAt(float Tau)
	{
		if (Tau < 0.f)
		{
			return 0.f;
		}
		if (Tau < WarnEnd)
		{
			return 0.35f + 0.65f * Tau / WarnEnd;
		}
		if (Tau < RiseEnd + 0.2f)
		{
			return 0.7f * (1.f - (Tau - WarnEnd) / (RiseEnd + 0.2f - WarnEnd));
		}
		if (Tau >= SinkStart && Tau < CraterShut)
		{
			return 0.75f * FMath::Sqrt(1.f - (Tau - SinkStart) / (CraterShut - SinkStart));
		}
		return 0.f;
	}

	/** Animación de la tortuga (null en un servidor dedicado o sin malla). */
	inline UTN_TurtleAnimInstance* TurtleAnim(const ACharacter* Char)
	{
		const USkeletalMeshComponent* Mesh = Char ? Char->GetMesh() : nullptr;
		return Mesh ? Cast<UTN_TurtleAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
	}

	/**
	 * Suelo bajo la tortuga: si está de pie, el de verdad; si no (en el aire, en bola, en el pico de una gaviota), la
	 * arena de debajo (la del generador, sin trazas; sin él, una traza).
	 */
	FVector FindGround(const ACharacter* Char)
	{
		const UCapsuleComponent* Capsule = Char->GetCapsuleComponent();
		const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.f;
		const FVector Feet = Char->GetActorLocation() - FVector(0.0, 0.0, HalfHeight);
		if (const UCharacterMovementComponent* Move = Char->GetCharacterMovement())
		{
			if (Move->IsMovingOnGround() && Move->CurrentFloor.IsWalkableFloor() && Move->CurrentFloor.HitResult.bBlockingHit)
			{
				return FVector(Feet.X, Feet.Y, Move->CurrentFloor.HitResult.ImpactPoint.Z);
			}
		}
		UWorld* World = Char->GetWorld();
		TActorIterator<ATN_BeachRaceGenerator> Generator(World);
		if (Generator)
		{
			// Si el generador dice que hay algo muy por encima de los pies (dentro de una roca), mejor la traza.
			const float SandZ = Generator->GetGroundHeightAt(Feet);
			if (SandZ < Feet.Z + 150.f)
			{
				return FVector(Feet.X, Feet.Y, SandZ);
			}
		}
		float TraceZ = 0.f;
		if (ATN_BeachEnemy::TraceGround(World, Feet, TraceZ, nullptr, 200.f, 8000.f))
		{
			return FVector(Feet.X, Feet.Y, TraceZ);
		}
		return Feet;
	}

	/** Pieza de malla del gusano: sin colisión, fuera de toda duplicación, oculta hasta que se coloque. */
	UStaticMeshComponent* MakePart(AActor* InOwner, USceneComponent* Parent, UStaticMesh* Mesh, bool bShadow)
	{
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(InOwner, NAME_None, RF_Transient | RF_DuplicateTransient);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCanEverAffectNavigation(false);
		Comp->SetGenerateOverlapEvents(false);
		Comp->SetCastShadow(bShadow);
		Comp->bReceivesDecals = false;
		Comp->SetStaticMesh(Mesh);
		Comp->SetupAttachment(Parent);
		Comp->RegisterComponent();
		Comp->SetVisibility(false);
		return Comp;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Contrato
// ─────────────────────────────────────────────────────────────────────────────

ATN_BeachSandWorm* ATN_BeachSandWorm::EatTurtle(ACharacter* InTurtle)
{
	if (!IsValid(InTurtle) || !InTurtle->HasAuthority() || InTurtle->IsActorBeingDestroyed() || IsBeingEaten(InTurtle))
	{
		return nullptr;
	}
	UWorld* World = InTurtle->GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return nullptr;
	}
	const double Now = ATN_BeachEnemy::ServerNow(World);
	const FVector Ground = TNSandWorm::FindGround(InTurtle);

	// Varias rezagadas: cada gusano sale un poco después del anterior de la misma tanda (0-0,3 s), no todos a la vez.
	int32 Recent = 0;
	for (TActorIterator<ATN_BeachSandWorm> It(World); It; ++It)
	{
		if (IsValid(*It) && FMath::Abs(Now - static_cast<double>(It->StartTime)) < 0.25)
		{
			++Recent;
		}
	}
	const float Stagger = Recent == 0 ? 0.f : FMath::Min(TNSandWorm::MaxStagger, TNSandWorm::StaggerStep * static_cast<float>(Recent) + FMath::FRandRange(0.f, 0.04f));
	// Se dobla más o menos hacia donde miraba la tortuga (en la carrera, hacia el mar).
	const float Yaw = static_cast<float>(InTurtle->GetActorRotation().Yaw) + FMath::FRandRange(-25.f, 25.f);

	const FTransform SpawnAt(FRotator(0.f, Yaw, 0.f), Ground);
	ATN_BeachSandWorm* Worm = World->SpawnActorDeferred<ATN_BeachSandWorm>(ATN_BeachSandWorm::StaticClass(), SpawnAt, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Worm)
	{
		return nullptr;
	}
	Worm->Victim = InTurtle;
	Worm->StartTime = static_cast<float>(Now);
	Worm->StartDelay = Stagger;
	Worm->GroundPoint = Ground;
	Worm->ArcYaw = Yaw;
	Worm->LookSeed = FMath::RandRange(0, 0xFFFF);
	Worm->FinishSpawning(SpawnAt);
	// Antes de empezar: sin aturdimiento, bola, derribo ni carga; quieta, sin colisión y sin que nadie más le dé.
	Worm->ServerCalmVictim();
	Worm->ForceNetUpdate();
	UE_LOG(LogTortunabo, Log, TEXT("[Playa] Un gusano de arena se come a %s (desfase %.2f s)."), *GetNameSafe(InTurtle), Stagger);
	return Worm;
}

bool ATN_BeachSandWorm::IsBeingEaten(const ACharacter* InTurtle)
{
	return FindEating(InTurtle) != nullptr;
}

ATN_BeachSandWorm* ATN_BeachSandWorm::FindEating(const ACharacter* InTurtle)
{
	UWorld* World = InTurtle ? InTurtle->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ATN_BeachSandWorm> It(World); It; ++It)
	{
		ATN_BeachSandWorm* Worm = *It;
		if (IsValid(Worm) && !Worm->IsActorBeingDestroyed() && !Worm->bReleasedLocal && Worm->Victim.Get() == InTurtle)
		{
			return Worm;
		}
	}
	return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Actor
// ─────────────────────────────────────────────────────────────────────────────

ATN_BeachSandWorm::ATN_BeachSandWorm()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	// Después del movimiento de las tortugas y de los enemigos: la última palabra sobre dónde está la comida la tiene la boca.
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	bReplicates = true;
	// Siempre relevante: la escena se ve desde toda la playa y IsBeingEaten tiene que valer en todas las máquinas.
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	// Todo lo replicado se fija al crearlo: basta con el primer envío.
	SetNetUpdateFrequency(2.f);
	SetCanBeDamaged(false);
	SetActorEnableCollision(false);
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SceneRoot->SetMobility(EComponentMobility::Movable);
	RootComponent = SceneRoot;
}

void ATN_BeachSandWorm::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_BeachSandWorm, Victim);
	DOREPLIFETIME(ATN_BeachSandWorm, StartTime);
	DOREPLIFETIME(ATN_BeachSandWorm, StartDelay);
	DOREPLIFETIME(ATN_BeachSandWorm, GroundPoint);
	DOREPLIFETIME(ATN_BeachSandWorm, ArcYaw);
	DOREPLIFETIME(ATN_BeachSandWorm, LookSeed);
}

void ATN_BeachSandWorm::BeginPlay()
{
	Super::BeginPlay();
	bHasScreen = GetNetMode() != NM_DedicatedServer;
	// El suelo replicado manda (la posición con la que nace el actor en un cliente llega cuantizada).
	SetActorLocation(GetGround());
	if (bHasScreen)
	{
		BuildVisuals();
	}
}

void ATN_BeachSandWorm::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseLocal();
	if (Voice)
	{
		Voice->SetRumble(0.f);
	}
	Super::EndPlay(EndPlayReason);
}

void ATN_BeachSandWorm::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = SceneClock.Advance(GetWorld(), DeltaSeconds);
	const float Tau = SceneTime(Now);
	UpdatePose(Tau);
	TickVictim(Tau);
	if (IsActorBeingDestroyed())
	{
		// La tortuga ha reaparecido: el gusano ya no pinta nada.
		return;
	}
	if (HasAuthority())
	{
		ServerTick(Tau);
		if (IsActorBeingDestroyed())
		{
			return;
		}
	}
	if (bHasScreen)
	{
		VisualTick(Tau, DeltaSeconds);
		TickCamera(Tau, DeltaSeconds);
	}
	LastTau = Tau;
	// Acabada la escena y sin partículas: solo queda mirar de vez en cuando si la tortuga ha reaparecido.
	if (!bSlowTick && Tau > TNSandWorm::CraterShut + 2.5f)
	{
		bSlowTick = true;
		SetActorTickInterval(0.1f);
	}
}

float ATN_BeachSandWorm::SceneTime(double Now) const
{
	const float Raw = static_cast<float>(Now - static_cast<double>(StartTime)) - StartDelay;
	if (Raw <= 0.f)
	{
		return Raw;
	}
	// Con desfase, la escena va algo más deprisa y acaba a EatSeconds de crearlo, como la de los demás.
	const float Span = FMath::Max(1.f, EatSeconds - StartDelay);
	return Raw * EatSeconds / Span;
}

FVector ATN_BeachSandWorm::GetArcDir() const
{
	return FRotator(0.f, ArcYaw, 0.f).Vector();
}

FVector ATN_BeachSandWorm::GetSideDir() const
{
	return FVector::CrossProduct(FVector::UpVector, GetArcDir());
}

void ATN_BeachSandWorm::SpineAt(double Along, FVector& OutPoint, FVector& OutTangent) const
{
	// La columna sale recta de la arena (y sigue recta hacia abajo bajo ella); el tramo de arriba (BendLength, o lo que
	// asome si es menos) es un arco de circunferencia hacia GetArcDir. Encima, una ondulación de lado que crece desde la
	// arena. Cuentas cerradas: iguales en todas las máquinas.
	const FVector Ground = GetGround();
	const FVector Up = FVector::UpVector;
	const FVector Arc = GetArcDir();
	const FVector Side = GetSideDir();
	const double Exposed = Pose.Exposed;
	const double ArcLength = FMath::Clamp(Exposed, 1.0, TNSandWorm::BendLength);
	const double ArcStart = FMath::Max(0.0, Exposed - ArcLength);
	// Con poco cuerpo fuera no se dobla del todo (si no, se doblaría a ras de arena).
	const double Theta = Pose.Bend * FMath::Clamp(Exposed / TNSandWorm::BendLength, 0.0, 1.0);
	FVector Point = Ground + Up * Along;
	FVector Tangent = Up;
	if (Along > ArcStart && FMath::Abs(Theta) > 1e-4)
	{
		const double K = Theta / ArcLength;
		const double Phi = K * (Along - ArcStart);
		Point = Ground + Up * ArcStart + Arc * ((1.0 - FMath::Cos(Phi)) / K) + Up * (FMath::Sin(Phi) / K);
		Tangent = Up * FMath::Cos(Phi) + Arc * FMath::Sin(Phi);
	}
	const double Ramp = FMath::Clamp(Along / 600.0, 0.0, 1.0);
	const double Wave = UE_DOUBLE_TWO_PI * Along / TNSandWorm::SwayWave - Pose.SwayPhase;
	Point += Side * (Pose.SwayAmp * Ramp * FMath::Sin(Wave));
	Tangent += Side * (Pose.SwayAmp * Ramp * UE_DOUBLE_TWO_PI / TNSandWorm::SwayWave * FMath::Cos(Wave));
	OutPoint = Point;
	OutTangent = Tangent.GetSafeNormal();
}

void ATN_BeachSandWorm::UpdatePose(float Tau)
{
	Pose.Exposed = TNSandWorm::ExposedAt(Tau);
	Pose.Bend = FMath::DegreesToRadians(static_cast<double>(TNSandWorm::BendDegAt(Tau)));
	// Ondula algo siempre y el doble mientras está fuera.
	Pose.SwayAmp = TNSandWorm::SwayAmp * (0.5 + 0.5 * TNSandWorm::Bump(Tau, TNSandWorm::WarnEnd, TNSandWorm::SinkEnd));
	Pose.SwayPhase = TNSandWorm::SwaySpeed * Tau + static_cast<double>(LookSeed % 7);
	SpineAt(Pose.Exposed, Pose.Rim, Pose.Axis);
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachSandWorm::ServerTick(float Tau)
{
	ACharacter* Eaten = Victim;
	if (!IsValid(Eaten) || Eaten->IsActorBeingDestroyed())
	{
		// Sin tortuga (se ha ido de la partida o la ronda nueva la ha quitado): acaba su escena y se va.
		if (Tau >= TNSandWorm::CraterShut)
		{
			Destroy();
		}
		return;
	}
	// Lo que le caiga mientras está en la boca (una gaviota que la suelta, una trampa...) se le quita enseguida.
	ServerCalmVictim();
}

void ATN_BeachSandWorm::ServerCalmVictim()
{
	ACharacter* Eaten = Victim;
	if (!HasAuthority() || !IsValid(Eaten) || bReleasedLocal)
	{
		return;
	}
	if (ATortugaCharacter* Tortuga = Cast<ATortugaCharacter>(Eaten))
	{
		// El enemigo que la tuviera en la boca o el pico la suelta y deja el ataque (si no, la seguiría colocando en su boca).
		ATN_BeachEnemy::ServerReleaseHeldTurtle(Tortuga, TEXT("se la come un gusano de arena"));
		if (UTN_CarryComponent* Carry = Tortuga->GetCarryComponent())
		{
			if (Carry->IsCarrying())
			{
				Carry->ForceRelease(false);
			}
			if (ATortugaCharacter* Carrier = Carry->GetCarrier())
			{
				if (UTN_CarryComponent* CarrierCarry = Carrier->GetCarryComponent())
				{
					CarrierCarry->ForceRelease(false);
				}
			}
		}
		if (Tortuga->IsKnockedDown())
		{
			Tortuga->RecoverFromKnockdownSilently();
		}
		if (UTN_BeachStunComponent* Stun = UTN_BeachStunComponent::FindOn(Tortuga))
		{
			if (Stun->IsStunned())
			{
				Stun->EndStun(true);
			}
		}
		if (UTN_ShellComponent* Shell = Tortuga->GetShellComponent())
		{
			if (Shell->IsInShell() || Shell->GetBody())
			{
				Shell->SetExitLocked(false);
				Shell->ForceExitShell();
				if (Shell->GetBody())
				{
					Shell->StopBody();
				}
			}
		}
		// Nadie más le da mientras está en la boca (ATN_BeachEnemy::CanBeHit); una gaviota que la soltara lo quitaría.
		ATN_BeachEnemy::SetTurtleHeld(Tortuga, true);
	}
	if (UCharacterMovementComponent* Move = Eaten->GetCharacterMovement())
	{
		if (Move->MovementMode != MOVE_None)
		{
			Move->StopMovementImmediately();
			Move->DisableMovement();
		}
	}
	if (Eaten->GetActorEnableCollision())
	{
		Eaten->SetActorEnableCollision(false);
		bCollisionOff = true;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// La tortuga (todas las máquinas)
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachSandWorm::TickVictim(float Tau)
{
	if (bReleasedLocal)
	{
		return;
	}
	ACharacter* Eaten = Victim;
	if (!IsValid(Eaten) || Eaten->IsActorBeingDestroyed())
	{
		return;
	}
	// Reaparece (la ronda siguiente la vuelve a mostrar en la salida, o cualquier otra cosa la saca): el gusano la suelta.
	if (bHiddenLocal && !Eaten->IsHidden())
	{
		ReleaseLocal();
		if (HasAuthority())
		{
			UE_LOG(LogTortunabo, Log, TEXT("[Playa] %s ha reaparecido: fuera el gusano de arena."), *GetNameSafe(Eaten));
			Destroy();
		}
		return;
	}
	if (!bControlLocal)
	{
		BeginControlLocal(Tau);
	}
	// Patalea mientras sube en la boca.
	if (!bPoseSet && Tau >= TNSandWorm::WarnEnd && Tau < TNSandWorm::BiteTime)
	{
		bPoseSet = true;
		if (UTN_TurtleAnimInstance* Anim = TNSandWorm::TurtleAnim(Eaten))
		{
			Anim->SetCelebration(ETNTurtleCelebration::Tantrum);
		}
	}
	// El bocado: desaparece en todas las máquinas a la vez (en el servidor, además, se replica).
	if (!bHiddenLocal && Tau >= TNSandWorm::BiteTime)
	{
		bHiddenLocal = true;
		Eaten->SetActorHiddenInGame(true);
		if (UTN_TurtleAnimInstance* Anim = TNSandWorm::TurtleAnim(Eaten))
		{
			if (Anim->GetCelebration() == ETNTurtleCelebration::Tantrum)
			{
				Anim->SetCelebration(ETNTurtleCelebration::None);
			}
		}
	}
	// Sin movimiento propio ni colisión (una corrección de red que llegue tarde podría devolverle el movimiento).
	if (UCharacterMovementComponent* Move = Eaten->GetCharacterMovement())
	{
		if (Move->MovementMode != MOVE_None)
		{
			Move->StopMovementImmediately();
			Move->DisableMovement();
		}
	}
	if (Eaten->GetActorEnableCollision())
	{
		// No se replica: cada máquina la quita (y la devuelve al soltarla).
		Eaten->SetActorEnableCollision(false);
		bCollisionOff = true;
	}
	const bool bInScene = Tau < TNSandWorm::CraterShut;
	if (!bInScene && (bCorrectionsOff || bSmoothingSaved))
	{
		EndSceneControlLocal();
	}
	// La colocan todas las máquinas con las mismas cuentas mientras dura la escena; después, solo el servidor (oculta y de
	// pie donde estaba, lejos de cualquier vacío o agua de meta).
	if (!bInScene && !HasAuthority())
	{
		return;
	}
	const UCapsuleComponent* Capsule = Eaten->GetCapsuleComponent();
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.f;
	FVector Loc = VictimLocationAt(Tau, HalfHeight);
	// Al empezar, de donde estuviera a su sitio en 0,3 s.
	const float Blend = TNSandWorm::Smooth01((Tau - ControlFromTau) / 0.3f);
	if (Blend < 1.f)
	{
		Loc = FMath::Lerp(ControlFrom, Loc, static_cast<double>(Blend));
	}
	float Yaw = VictimYaw;
	if (Tau > TNSandWorm::WarnEnd && Tau < TNSandWorm::BiteTime)
	{
		// Da vueltas en la boca.
		Yaw += 300.f * (Tau - TNSandWorm::WarnEnd);
	}
	if (bInScene || !Eaten->GetActorLocation().Equals(Loc, 1.0))
	{
		Eaten->SetActorLocationAndRotation(Loc, FRotator(0.f, Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	}
}

FVector ATN_BeachSandWorm::VictimLocationAt(float Tau, float HalfHeight) const
{
	const FVector Ground = GetGround();
	const FVector Up = FVector::UpVector;
	if (Tau >= TNSandWorm::BiteTime)
	{
		// Oculta: de pie donde estaba.
		return Ground + Up * HalfHeight;
	}
	// Aviso: se hunde un poco en el remolino y tiembla con la arena.
	const float Sink = TNSandWorm::SinkIntoWhirl * TNSandWorm::Smooth01(Tau / TNSandWorm::WarnEnd);
	const float Shake = Tau < TNSandWorm::WarnEnd ? 5.f * TNSandWorm::Smooth01(Tau / 0.3f) : 0.f;
	const FVector Stand = Ground + Up * (HalfHeight - Sink) + FVector(Shake * FMath::Sin(Tau * 71.f), Shake * FMath::Sin(Tau * 53.f + 1.3f), 0.0);
	if (Tau < TNSandWorm::WarnEnd)
	{
		return Stand;
	}
	// En la boca, metida por dentro del borde; antes del bocado sube un poco (el bote) y cae dentro.
	float Toss = 0.f;
	if (Tau > TNSandWorm::TossStart)
	{
		const float U = FMath::Clamp((Tau - TNSandWorm::TossStart) / (TNSandWorm::BiteTime - TNSandWorm::TossStart), 0.f, 1.f);
		Toss = TNSandWorm::TossHeight * FMath::Sin(UE_PI * U) - 90.f * U * U;
	}
	const FVector Feet = Pose.Rim + Pose.Axis * static_cast<double>(Toss - TNSandWorm::InMouth);
	const FVector InMouthAt = Feet + Up * HalfHeight;
	// La boca llega desde abajo: hasta que la levanta, sigue de pie en la arena.
	const double Lift = TNSandWorm::Smooth01((Tau - TNSandWorm::WarnEnd) / 0.15f);
	FVector Out = FMath::Lerp(Stand, InMouthAt, Lift);
	Out.Z = FMath::Max(Stand.Z, InMouthAt.Z);
	return Out;
}

void ATN_BeachSandWorm::BeginControlLocal(float Tau)
{
	ACharacter* Eaten = Victim;
	bControlLocal = true;
	ControlFrom = Eaten->GetActorLocation();
	ControlFromTau = Tau;
	VictimYaw = static_cast<float>(Eaten->GetActorRotation().Yaw);
	if (UCharacterMovementComponent* Move = Eaten->GetCharacterMovement())
	{
		Move->StopMovementImmediately();
		Move->DisableMovement();
		if (Eaten->GetLocalRole() == ROLE_SimulatedProxy)
		{
			// La coloca la boca con las mismas cuentas que en las demás máquinas: sin suavizado de red encima.
			SavedSmoothing = static_cast<uint8>(Move->NetworkSmoothingMode);
			bSmoothingSaved = true;
			Move->NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;
		}
		if (HasAuthority())
		{
			// El dueño la coloca con su reloj: no se le corrige mientras dura la escena.
			Move->bIgnoreClientMovementErrorChecksAndCorrection = true;
			bCorrectionsOff = true;
		}
	}
	// Sin control: el jugador de esta máquina ya no la mueve ni usa nada (hasta que reaparezca).
	if (Eaten->IsLocallyControlled() && Eaten->InputEnabled())
	{
		if (APlayerController* PC = Cast<APlayerController>(Eaten->GetController()))
		{
			Eaten->DisableInput(PC);
			bInputOff = true;
		}
	}
}

void ATN_BeachSandWorm::EndSceneControlLocal()
{
	ACharacter* Eaten = Victim;
	if (UCharacterMovementComponent* Move = IsValid(Eaten) ? Eaten->GetCharacterMovement() : nullptr)
	{
		if (bCorrectionsOff)
		{
			Move->bIgnoreClientMovementErrorChecksAndCorrection = false;
		}
		if (bSmoothingSaved)
		{
			Move->NetworkSmoothingMode = static_cast<ENetworkSmoothingMode>(SavedSmoothing);
		}
	}
	bCorrectionsOff = false;
	bSmoothingSaved = false;
}

void ATN_BeachSandWorm::ReleaseLocal()
{
	if (bReleasedLocal)
	{
		return;
	}
	bReleasedLocal = true;
	EndSceneControlLocal();

	// La cámara vuelve a su tortuga (solo si sigue mirando al gusano: otra cosa pudo cambiarla).
	if (APlayerController* PC = CameraPC.Get())
	{
		if (bCameraTaken && IsValid(PC) && PC->GetViewTarget() == this)
		{
			AActor* Back = PC->GetPawn() ? static_cast<AActor*>(PC->GetPawn()) : static_cast<AActor*>(PC);
			PC->SetViewTargetWithBlend(Back, 0.35f, VTBlend_Cubic);
		}
	}
	bCameraTaken = false;
	bCameraReady = false;
	CameraPC.Reset();

	ACharacter* Eaten = Victim;
	if (IsValid(Eaten) && !Eaten->IsActorBeingDestroyed())
	{
		if (bInputOff)
		{
			Eaten->EnableInput(nullptr);
		}
		if (bPoseSet)
		{
			if (UTN_TurtleAnimInstance* Anim = TNSandWorm::TurtleAnim(Eaten))
			{
				if (Anim->GetCelebration() == ETNTurtleCelebration::Tantrum)
				{
					Anim->SetCelebration(ETNTurtleCelebration::None);
				}
			}
		}
		// La colisión no se replica: cada máquina devuelve la que quitó (en el servidor, la ronda nueva también lo hace). Aquí
		// solo se llega con la tortuga reapareciendo (en un cliente, el gusano puede irse antes de que llegue que se ve).
		if (bCollisionOff)
		{
			Eaten->SetActorEnableCollision(true);
		}
		if (ATortugaCharacter* Tortuga = Cast<ATortugaCharacter>(Eaten))
		{
			ATN_BeachEnemy::SetTurtleHeld(Tortuga, false);
		}
	}
	bInputOff = false;
	bCollisionOff = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lo que se ve y se oye
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachSandWorm::BuildVisuals()
{
	if (bVisualsBuilt)
	{
		return;
	}
	bVisualsBuilt = true;
	// La arena bajo el gusano (cada máquina la mira; el terreno es el mismo en todas): el cráter y el remolino se apoyan en
	// su cuesta. En algo muy empinado (una roca), planos.
	const FVector Ground = GetGround();
	FHitResult Hit;
	const FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(SandWormGround), false, this);
	if (GetWorld()->LineTraceSingleByObjectType(Hit, Ground + FVector(0.0, 0.0, 300.0), Ground - FVector(0.0, 0.0, 300.0),
		FCollisionObjectQueryParams(ECC_WorldStatic), GroundParams))
	{
		const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
		if (Normal.Z > 0.8)
		{
			GroundNormal = Normal;
		}
	}

	const int32 Palette = FMath::Abs(LookSeed) % 3;
	const TNSandWormMeshes::FWormLook Look = TNSandWormMeshes::WormPalette(Palette);
	const uint32 MeshSeed = 7331u + static_cast<uint32>(Palette) * 97u;
	UStaticMesh* SegmentMesh = TNBeachKit::CachedMesh(FString::Printf(TEXT("SandWorm.Segment.%d"), Palette), [&Look, MeshSeed](TNProcMesh::FTNProcMeshBuffers& M)
	{
		TNSandWormMeshes::BuildSegment(M, Look, MeshSeed);
	});
	UStaticMesh* HeadMesh = TNBeachKit::CachedMesh(FString::Printf(TEXT("SandWorm.Head.%d"), Palette), [&Look, MeshSeed](TNProcMesh::FTNProcMeshBuffers& M)
	{
		TNSandWormMeshes::BuildHead(M, Look, MeshSeed);
	});
	UStaticMesh* LipMesh = TNBeachKit::CachedMesh(FString::Printf(TEXT("SandWorm.Lip.%d"), Palette), [&Look, MeshSeed](TNProcMesh::FTNProcMeshBuffers& M)
	{
		TNSandWormMeshes::BuildLip(M, Look, MeshSeed);
	});
	UStaticMesh* CraterMesh = TNBeachKit::CachedMesh(TEXT("SandWorm.Crater"), [](TNProcMesh::FTNProcMeshBuffers& M)
	{
		TNSandWormMeshes::BuildCrater(M, 4242u);
	});
	UStaticMesh* WhirlMesh = TNBeachKit::CachedMesh(TEXT("SandWorm.Whirl"), [](TNProcMesh::FTNProcMeshBuffers& M)
	{
		TNSandWormMeshes::BuildWhirl(M);
	});

	// Cabeza (colocada en el mundo) con sus labios enganchados; anillos del cuerpo, cráter y remolino en el mundo.
	// Piezas de arte (Docs/Arte_Assets.md): cabeza, labios, anillos y cráter (el remolino es un efecto).
	Head = TNSandWorm::MakePart(this, SceneRoot, HeadMesh, true);
	TNArt::ApplyToComponent(Head, TN_ART("Beach.SandWorm.Head"));
	Head->SetAbsolute(true, true, true);
	for (int32 i = 0; i < TNSandWormMeshes::LipCount; ++i)
	{
		UStaticMeshComponent* Lip = TNSandWorm::MakePart(this, Head, LipMesh, true);
		TNArt::ApplyToComponent(Lip, TN_ART("Beach.SandWorm.Lip"));
		Lips.Add(Lip);
	}
	for (int32 k = 0; k < TNSandWorm::NumSegments; ++k)
	{
		UStaticMeshComponent* Ring = TNSandWorm::MakePart(this, SceneRoot, SegmentMesh, true);
		TNArt::ApplyToComponent(Ring, TN_ART("Beach.SandWorm.Segment"));
		Ring->SetAbsolute(true, true, true);
		Segments.Add(Ring);
	}
	Crater = TNSandWorm::MakePart(this, SceneRoot, CraterMesh, false);
	TNArt::ApplyToComponent(Crater, TN_ART("Beach.SandWorm.Crater"));
	Crater->SetAbsolute(true, true, true);
	Whirl = TNSandWorm::MakePart(this, SceneRoot, WhirlMesh, false);
	Whirl->SetAbsolute(true, true, true);

	// Polvo alrededor, arena que resbala del cuerpo, la nube del eructo, piedrecitas y terrones.
	using TNAmbientFX::EShape;
	const uint32 FxSeed = static_cast<uint32>(LookSeed) * 2654435761u;
	TNAmbientFX::FEmitterDesc DustDesc = TNBeachKit::MakeDesc(EShape::Puff, FLinearColor(0.88f, 0.79f, 0.6f), true, 0.6f, 56, 26.f, 420.f, -40.f, 1.f, 1.9f, 160.f, 460.f);
	DustDesc.Spread = 1.f;
	DustDesc.SpawnRadius = 380.f;
	DustDesc.Drag = 1.4f;
	TNBeachKit::InitEmitter(Dust, this, DustDesc, FxSeed + 1u);
	TNAmbientFX::FEmitterDesc FallDesc = TNBeachKit::MakeDesc(EShape::Ember, FLinearColor(0.84f, 0.73f, 0.5f), false, 1.f, 70, 90.f, 160.f, -1500.f, 0.8f, 1.4f, 34.f, 22.f);
	FallDesc.Spread = 1.2f;
	FallDesc.SpawnRadius = 250.f;
	FallDesc.SpawnHeight = 200.f;
	TNBeachKit::InitEmitter(SandFall, this, FallDesc, FxSeed + 2u);
	TNAmbientFX::FEmitterDesc BurpDesc = TNBeachKit::MakeDesc(EShape::Puff, FLinearColor(0.82f, 0.76f, 0.54f), true, 0.7f, 30, 0.f, 950.f, 40.f, 1.1f, 2.f, 170.f, 650.f);
	BurpDesc.Spread = 0.45f;
	BurpDesc.Drag = 1.7f;
	BurpDesc.SpawnRadius = 120.f;
	BurpDesc.Buoyancy = 60.f;
	TNBeachKit::InitEmitter(BurpCloud, this, BurpDesc, FxSeed + 3u);
	Pebbles.Init(this, ETNTrapBurstShape::Chip, FLinearColor(0.52f, 0.46f, 0.38f), 48);
	Pebbles.SetMotion(-1500.f, 0.3f, 48.f, 36.f, 1.f, 1.6f);
	Clods.Init(this, ETNTrapBurstShape::Blob, FLinearColor(0.84f, 0.74f, 0.52f), 64);
	Clods.SetMotion(-1400.f, 0.5f, 75.f, 40.f, 0.9f, 1.5f);

	Voice = UTN_BeachSandWormSynthComponent::AttachTo(this, SceneRoot, 3000.f, 30000.f);
}

void ATN_BeachSandWorm::VisualTick(float Tau, float DeltaSeconds)
{
	if (!bVisualsBuilt)
	{
		return;
	}
	PoseBody(Tau, DeltaSeconds);
	FireEvents(Tau, LastTau);
	const FVector Ground = GetGround();
	const FVector Up = FVector::UpVector;

	// Polvo que se levanta alrededor: cada vez más durante el aviso, al salir y al hundirse.
	float DustRate = 0.f;
	if (Tau >= 0.f && Tau < TNSandWorm::WarnEnd)
	{
		DustRate = 0.25f + 0.75f * Tau / TNSandWorm::WarnEnd;
	}
	else if (Tau >= TNSandWorm::WarnEnd && Tau < TNSandWorm::RiseEnd + 0.3f)
	{
		DustRate = 0.6f * (1.f - (Tau - TNSandWorm::WarnEnd) / (TNSandWorm::RiseEnd + 0.3f - TNSandWorm::WarnEnd));
	}
	else if (Tau >= TNSandWorm::SinkStart && Tau < TNSandWorm::CraterShut)
	{
		DustRate = 0.5f;
	}
	Dust.RateScale = DustRate;
	Dust.Origin = Ground + Up * 40.0;
	// Arena que resbala del cuerpo mientras sube.
	const float FallSpan = TNSandWorm::RiseEnd + 0.5f - TNSandWorm::WarnEnd;
	SandFall.RateScale = Tau >= TNSandWorm::WarnEnd && Tau < TNSandWorm::WarnEnd + FallSpan ? 1.f - (Tau - TNSandWorm::WarnEnd) / FallSpan : 0.f;
	SandFall.Origin = Pose.Rim - Pose.Axis * 220.0;
	// Piedrecitas que saltan del remolino durante el aviso.
	if (Tau >= 0.f && Tau < TNSandWorm::WarnEnd)
	{
		PebbleTimer -= DeltaSeconds;
		if (PebbleTimer <= 0.f)
		{
			PebbleTimer = 0.08f;
			const float Angle = FMath::FRand() * 2.f * UE_PI;
			const float Dist = FMath::Sqrt(FMath::FRand()) * TNSandWorm::WhirlRadius * 0.9f;
			Pebbles.Burst(Ground + FVector(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, 20.f), 3, Up, 420.f + 300.f * Tau / TNSandWorm::WarnEnd, 0.55f, 40.f);
		}
	}
	// Retumbar: sonido y temblor de cámara de cerca.
	const float RumbleNow = TNSandWorm::RumbleAt(Tau);
	if (Voice)
	{
		Voice->SetRumble(RumbleNow);
	}
	if (RumbleNow > 0.f)
	{
		UTN_BeachCameraShake::Rumble(this, Ground, RumbleNow * 0.65f, 1500.f, 6000.f);
	}

	FVector View = Ground;
	TNBeachKit::LocalCamera(GetWorld(), View);
	TNBeachKit::TickEmitterIfBusy(Dust, DeltaSeconds, View);
	TNBeachKit::TickEmitterIfBusy(SandFall, DeltaSeconds, View);
	TNBeachKit::TickEmitterIfBusy(BurpCloud, DeltaSeconds, View);
	Pebbles.Tick(DeltaSeconds);
	Clods.Tick(DeltaSeconds);
	TickNomText(DeltaSeconds);
}

void ATN_BeachSandWorm::PoseBody(float Tau, float DeltaSeconds)
{
	const FVector Ground = GetGround();
	const FVector Side = GetSideDir();
	const double Exposed = Pose.Exposed;

	// Cabeza, con los labios que se abren como una flor y se cierran en cúpula.
	if (Head)
	{
		const bool bShowHead = Tau >= 0.f && Exposed > TNSandWorm::HeadShowFrom;
		if (Head->IsVisible() != bShowHead)
		{
			Head->SetVisibility(bShowHead, true);
		}
		if (bShowHead)
		{
			const float Fat = TNSandWorm::HeadRadialAt(Tau);
			const FQuat HeadRot = FRotationMatrix::MakeFromXY(Pose.Axis, Side).ToQuat();
			Head->SetWorldTransform(FTransform(HeadRot, Pose.Rim, FVector(TNSandWorm::HeadLengthAt(Tau), Fat, Fat)));
			const float Open = TNSandWorm::LipDegAt(Tau);
			const bool bWide = Open > 0.f;
			for (int32 i = 0; i < Lips.Num(); ++i)
			{
				UStaticMeshComponent* Lip = Lips[i];
				if (!Lip)
				{
					continue;
				}
				// Bisagra en el borde (espacio de la cabeza: X hacia fuera de la boca); cada labio gira sobre la tangente del
				// borde: con el ángulo negativo se cierra hacia el centro, con el positivo se abre hacia fuera.
				const double Alpha = UE_DOUBLE_TWO_PI * (i + 0.5) / Lips.Num();
				const FVector Radial(0.0, FMath::Cos(Alpha), FMath::Sin(Alpha));
				const FVector Hinge = Radial * TNSandWormMeshes::LipHingeRadius;
				const FVector LipY = -FVector::CrossProduct(FVector::ForwardVector, Radial);
				const float Flutter = bWide ? 4.f * FMath::Sin(Tau * 17.f + 1.7f * static_cast<float>(i)) : 0.f;
				const double Beta = FMath::DegreesToRadians(static_cast<double>(Open + Flutter));
				const FVector LipX = FVector::ForwardVector * FMath::Cos(Beta) + Radial * FMath::Sin(Beta);
				Lip->SetRelativeLocationAndRotation(Hinge, FRotationMatrix::MakeFromXY(LipX, LipY).Rotator());
			}
		}
	}

	// Anillos del cuerpo a lo largo de la columna: se estrechan hacia abajo, ondulan y llevan el bulto del trago.
	for (int32 k = 0; k < Segments.Num(); ++k)
	{
		UStaticMeshComponent* Ring = Segments[k];
		if (!Ring)
		{
			continue;
		}
		const double Along = Exposed - TNSandWormMeshes::HeadLength - TNSandWorm::SegmentSpacing * (k + 0.45);
		const bool bShowRing = Tau >= 0.f && Along + TNSandWormMeshes::SegmentLength * 0.5 > -150.0;
		if (Ring->IsVisible() != bShowRing)
		{
			Ring->SetVisibility(bShowRing);
		}
		if (!bShowRing)
		{
			continue;
		}
		FVector Point;
		FVector Tangent;
		SpineAt(Along, Point, Tangent);
		const double Taper = FMath::Lerp(1.0, 0.84, static_cast<double>(k) / FMath::Max(1, Segments.Num() - 1));
		const double Squeeze = 0.035 * FMath::Sin(UE_DOUBLE_TWO_PI * Along / 700.0 - 7.0 * Tau);
		const double Girth = Taper * (1.0 + Squeeze + TNSandWorm::BulgeAt(Tau, Along, Exposed));
		Ring->SetWorldTransform(FTransform(FRotationMatrix::MakeFromXY(Tangent, Side).ToQuat(), Point, FVector(1.0, Girth, Girth)));
	}

	// Remolino del aviso (girando cada vez más deprisa) y cráter, apoyados en la arena (sobre su normal).
	const FVector Base = Ground + GroundNormal * 6.0;
	const FQuat OnSand = FRotationMatrix::MakeFromZX(GroundNormal, GetArcDir()).ToQuat();
	if (Whirl)
	{
		const float WhirlR = TNSandWorm::WhirlRadiusAt(Tau);
		const bool bShowWhirl = WhirlR > 5.f;
		if (Whirl->IsVisible() != bShowWhirl)
		{
			Whirl->SetVisibility(bShowWhirl);
		}
		if (bShowWhirl)
		{
			WhirlSpin = FMath::Fmod(WhirlSpin - DeltaSeconds * (220.f + 520.f * FMath::Clamp(Tau / TNSandWorm::WarnEnd, 0.f, 1.f)), 360.f);
			const double Flat = WhirlR / 100.0;
			// Se hunde: el borde se levanta un poco según avisa.
			const double EdgeRise = 1.0 + 1.5 * TNSandWorm::WhirlDepthAt(Tau) / TNSandWorm::WhirlDepth;
			const FQuat Spin(FVector::UpVector, FMath::DegreesToRadians(static_cast<double>(WhirlSpin)));
			Whirl->SetWorldTransform(FTransform(OnSand * Spin, Base, FVector(Flat, Flat, EdgeRise)));
		}
	}
	if (Crater)
	{
		const float CraterR = TNSandWorm::CraterRadiusAt(Tau);
		const bool bShowCrater = CraterR > 5.f;
		if (Crater->IsVisible() != bShowCrater)
		{
			Crater->SetVisibility(bShowCrater);
		}
		if (bShowCrater)
		{
			const double Flat = CraterR / 100.0;
			Crater->SetWorldTransform(FTransform(OnSand, Base, FVector(Flat, Flat, Flat * 0.9)));
		}
	}
}

void ATN_BeachSandWorm::FireEvents(float Tau, float PrevTau)
{
	// Una vez por máquina al cruzar cada instante (quien llega tarde no ve lo que pasó hace más de 0,4 s).
	auto Crossed = [Tau, PrevTau](float At)
	{
		return PrevTau < At && Tau >= At && Tau - At < 0.4f;
	};
	const FVector Ground = GetGround();
	const FVector Up = FVector::UpVector;
	if (Crossed(TNSandWorm::WarnEnd))
	{
		// ¡Revienta la arena! Nube, terrones y piedras hacia arriba, arena y rugido, y un temblor fuerte cerca.
		TNBeachKit::BurstAt(Dust, Ground + Up * 80.0, Up, 20);
		Clods.Burst(Ground + Up * 120.0, 40, Up, 1700.f, 0.85f, 300.f);
		Pebbles.Burst(Ground + Up * 60.0, 22, Up, 1500.f, 0.9f, 280.f);
		if (Voice)
		{
			Voice->Play(ETNSandWormSfx::Sand, 1.f, 1.2f);
			Voice->Play(ETNSandWormSfx::Roar, 1.f, 1.1f);
		}
		UTN_BeachCameraShake::Kick(this, Ground, 0.85f, 2500.f, 9000.f);
	}
	if (Crossed(TNSandWorm::BiteTime))
	{
		// ¡ÑAM! sobre la boca cerrada, bocado y un golpe de temblor.
		ShowNom(Pose.Rim + Pose.Axis * 220.0 + Up * 200.0);
		TNBeachKit::BurstAt(Dust, Pose.Rim, Up, 6);
		if (Voice)
		{
			Voice->Play(ETNSandWormSfx::Bite, 1.f, 1.3f);
		}
		UTN_BeachCameraShake::Kick(this, Ground, 0.35f, 2000.f, 6000.f);
	}
	if (Voice && Crossed(TNSandWorm::BiteTime + 0.3f))
	{
		Voice->Play(ETNSandWormSfx::Gulp, 1.f, 1.1f);
	}
	if (Voice && Crossed(TNSandWorm::BiteTime + 0.62f))
	{
		Voice->Play(ETNSandWormSfx::Gulp, 0.85f, 1.f);
	}
	if (Crossed(TNSandWorm::BurpTime))
	{
		// Eructo: nube de arena por la boca, hacia donde mira.
		TNBeachKit::BurstAt(BurpCloud, Pose.Rim + Pose.Axis * 120.0, Pose.Axis, 18);
		if (Voice)
		{
			Voice->Play(ETNSandWormSfx::Burp, 1.f, 1.3f);
		}
		UTN_BeachCameraShake::Kick(this, Ground, 0.25f, 2500.f, 7000.f);
	}
	if (Crossed(TNSandWorm::SinkStart + 0.08f))
	{
		Clods.Burst(Ground + Up * 60.0, 22, Up, 900.f, 0.9f, 280.f);
		TNBeachKit::BurstAt(Dust, Ground + Up * 60.0, Up, 10);
		if (Voice)
		{
			Voice->Play(ETNSandWormSfx::Sand, 0.75f, 1.f);
		}
	}
	if (Crossed(TNSandWorm::SinkEnd))
	{
		TNBeachKit::BurstAt(Dust, Ground + Up * 30.0, Up, 8);
		UTN_BeachCameraShake::Kick(this, Ground, 0.3f, 2000.f, 6000.f);
	}
}

void ATN_BeachSandWorm::ShowNom(const FVector& WorldAt)
{
	if (!NomText)
	{
		NomText = NewObject<UTextRenderComponent>(this, NAME_None, RF_Transient | RF_DuplicateTransient);
		NomText->SetHorizontalAlignment(EHTA_Center);
		NomText->SetVerticalAlignment(EVRTA_TextCenter);
		NomText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		NomText->SetCastShadow(false);
		NomText->SetupAttachment(SceneRoot);
		NomText->RegisterComponent();
		NomText->SetAbsolute(true, true, true);
		NomText->SetText(NSLOCTEXT("TNBeach", "SandWormNom", "¡ÑAM!"));
		NomText->SetTextRenderColor(FColor(255, 214, 70));
		NomText->SetWorldSize(620.f);
	}
	NomAt = WorldAt;
	NomAge = 0.f;
	NomText->SetWorldLocation(WorldAt);
	NomText->SetWorldScale3D(FVector(0.3));
	NomText->SetVisibility(true);
}

void ATN_BeachSandWorm::TickNomText(float DeltaSeconds)
{
	if (!NomText || NomAge > 5.f)
	{
		return;
	}
	NomAge += DeltaSeconds;
	constexpr float Life = 1.3f;
	if (NomAge >= Life)
	{
		NomText->SetVisibility(false);
		NomAge = 10.f;
		return;
	}
	// Sale de golpe más grande, se asienta, sube frenando, mira a la cámara de esta máquina y al final encoge.
	const float Pop = NomAge < 0.12f ? FMath::Lerp(0.3f, 1.35f, NomAge / 0.12f) : FMath::Lerp(1.35f, 1.f, FMath::Min(1.f, (NomAge - 0.12f) / 0.18f));
	const float Shrink = FMath::Clamp((Life - NomAge) / 0.25f, 0.f, 1.f);
	const FVector At = NomAt + FVector(0.0, 0.0, 180.0 * (1.0 - FMath::Exp(-2.5 * NomAge)));
	FRotator Facing = FRotator::ZeroRotator;
	FVector View = FVector::ZeroVector;
	if (TNBeachKit::LocalCamera(GetWorld(), View))
	{
		Facing = (View - At).Rotation();
	}
	Facing.Roll = 10.f * FMath::Sin(NomAge * 24.f) * FMath::Exp(-NomAge * 2.5f);
	NomText->SetWorldLocationAndRotation(At, Facing);
	NomText->SetWorldScale3D(FVector(Pop * Shrink));
}

// ─────────────────────────────────────────────────────────────────────────────
// Cámara de la tortuga comida
// ─────────────────────────────────────────────────────────────────────────────

APlayerController* ATN_BeachSandWorm::FindVictimLocalController() const
{
	const ACharacter* Eaten = Victim;
	UWorld* World = GetWorld();
	if (!IsValid(Eaten) || !World)
	{
		return nullptr;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (PC && PC->IsLocalController() && PC->GetPawn() == Eaten)
		{
			return PC;
		}
	}
	return nullptr;
}

bool ATN_BeachSandWorm::GetSpectatorView(const FVector& ViewFrom, FVector& OutLocation, FRotator& OutRotation)
{
	const UWorld* World = GetWorld();
	if (!bHasScreen || bReleasedLocal || !World)
	{
		return false;
	}
	// TickCamera la sigue calculando mientras se pida (también sin la tortuga comida en esta máquina).
	SpectatorAskedTime = World->GetTimeSeconds();
	if (!bSceneCamInit)
	{
		InitSceneCamera(ViewFrom);
	}
	if (!bCameraReady)
	{
		return false;
	}
	OutLocation = CamLoc;
	OutRotation = CamRot;
	return true;
}

void ATN_BeachSandWorm::InitSceneCamera(const FVector& ViewFrom)
{
	// Sale de la cámara que la pide hacia un lado del gusano: el arco se ve de perfil y el eructo viene un poco hacia aquí.
	const FVector Ground = GetGround();
	FVector ToCam = (ViewFrom - Ground).GetSafeNormal2D();
	if (ToCam.IsNearlyZero())
	{
		ToCam = -GetArcDir();
	}
	FVector Perp = GetSideDir();
	if (FVector::DotProduct(Perp, ToCam) < 0.0)
	{
		Perp = -Perp;
	}
	CamDir = (Perp * 0.8 + GetArcDir() * 0.35 + ToCam * 0.2).GetSafeNormal2D();
	if (CamDir.IsNearlyZero())
	{
		CamDir = Perp;
	}
	CamDist = TNSandWorm::CamWarnDist;
	CamHeight = TNSandWorm::CamWarnHeight;
	CamLook = Ground + FVector::UpVector * 60.0;
	bSceneCamInit = true;
}

void ATN_BeachSandWorm::TickCamera(float Tau, float DeltaSeconds)
{
	if (bReleasedLocal || Tau < 0.f)
	{
		return;
	}
	const FVector Ground = GetGround();
	const FVector Up = FVector::UpVector;
	// La tortuga comida de esta máquina: su cámara pasa a ver la escena (una vez).
	if (!bCameraTaken && !bCameraGaveUp)
	{
		// En VR la cámara no se la lleva el gusano: se sigue en primera persona (una cámara ajena marea).
		if (TNVR::KeepFirstPersonView())
		{
			bCameraGaveUp = true;
		}
		else if (APlayerController* Taker = FindVictimLocalController(); Taker && Taker->PlayerCameraManager)
		{
			if (!bSceneCamInit)
			{
				InitSceneCamera(Taker->PlayerCameraManager->GetCameraLocation());
			}
			CameraPC = Taker;
			bCameraTaken = true;
			Taker->SetViewTargetWithBlend(this, TNSandWorm::CamBlend, VTBlend_Cubic);
		}
	}
	if (bCameraTaken)
	{
		APlayerController* PC = CameraPC.Get();
		if (!PC || PC->GetViewTarget() != this)
		{
			// Otra cosa se ha llevado la cámara (el fantasma, el podio...): no se le pelea.
			bCameraTaken = false;
			bCameraGaveUp = true;
		}
	}
	// Un espectador de esta máquina que sigue a la comida mira la escena desde aquí también (GetSpectatorView).
	const bool bSpectated = GetWorld() && GetWorld()->GetTimeSeconds() - SpectatorAskedTime < 0.5;
	if (!bCameraTaken && !bSpectated)
	{
		bCameraReady = false;
		return;
	}
	if (!bSceneCamInit)
	{
		return;
	}
	const bool bWarn = Tau < TNSandWorm::WarnEnd;
	const bool bGone = Tau >= TNSandWorm::SinkEnd;
	CamDist = FMath::FInterpTo(CamDist, bWarn ? TNSandWorm::CamWarnDist : TNSandWorm::CamSceneDist, DeltaSeconds, 2.4f);
	CamHeight = FMath::FInterpTo(CamHeight, bWarn ? TNSandWorm::CamWarnHeight : (bGone ? TNSandWorm::CamAfterHeight : TNSandWorm::CamSceneHeight), DeltaSeconds, 2.4f);
	// Mira a la arena durante el aviso y después; mientras está fuera, a media altura del gusano (cabe entero con la boca).
	FVector Want = Ground + Up * 60.0;
	if (!bWarn && !bGone && Pose.Exposed > 0.0)
	{
		const FVector Mid = FMath::Lerp(Ground, Pose.Rim, 0.55);
		Want = FVector(Mid.X, Mid.Y, FMath::Clamp(Mid.Z, Ground.Z + 120.0, Ground.Z + 1500.0));
	}
	CamLook = FMath::VInterpTo(CamLook, Want, DeltaSeconds, 4.f);
	FVector Loc = Ground + CamDir * CamDist + Up * CamHeight;
	// Que no se meta dentro de una roca, del decorado o de una duna: se acerca por delante de lo que tape.
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SandWormCamera), false, this);
	if (const ACharacter* Eaten = Victim)
	{
		Params.AddIgnoredActor(Eaten);
	}
	if (GetWorld()->SweepSingleByChannel(Hit, CamLook, Loc, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(30.f), Params))
	{
		const double Full = FVector::Dist(CamLook, Loc);
		const double Keep = FMath::Max(static_cast<double>(TNSandWorm::CamMinDist), static_cast<double>(Hit.Distance));
		Loc = CamLook + (Loc - CamLook).GetSafeNormal() * FMath::Min(Full, Keep);
	}
	CamLoc = Loc;
	CamRot = (CamLook - Loc).Rotation();
	bCameraReady = true;
}

void ATN_BeachSandWorm::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	if (!bCameraReady)
	{
		Super::CalcCamera(DeltaTime, OutResult);
		return;
	}
	OutResult.Location = CamLoc;
	OutResult.Rotation = CamRot;
}

// ─────────────────────────────────────────────────────────────────────────────
// Consola: TN.Beach.Worm [jugador]. Desde la ventana de un cliente del PIE va al mundo del servidor del mismo proceso;
// en un cliente remoto de verdad no hace nada (hay que escribirlo en el anfitrión).
// ─────────────────────────────────────────────────────────────────────────────

namespace TNSandWormConsole
{
	/** El mundo con autoridad del mismo proceso (el propio si no es un cliente; en el PIE, el del servidor del mismo mapa). */
	UWorld* AuthorityWorld(UWorld* InWorld)
	{
		if (!InWorld)
		{
			return nullptr;
		}
		if (InWorld->GetNetMode() != NM_Client)
		{
			return InWorld;
		}
		if (GEngine)
		{
			const FString MapName = UWorld::RemovePIEPrefix(InWorld->GetMapName());
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* Candidate = Context.World();
				if (Candidate && Candidate != InWorld && Candidate->IsGameWorld() && Candidate->GetNetMode() != NM_Client
					&& UWorld::RemovePIEPrefix(Candidate->GetMapName()) == MapName)
				{
					return Candidate;
				}
			}
		}
		UE_LOG(LogTortunabo, Warning, TEXT("[Playa] TN.Beach.Worm va en la ventana del anfitrión."));
		return nullptr;
	}

	void EatNow(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = AuthorityWorld(InWorld);
		if (!World)
		{
			return;
		}
		const int32 Index = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
		const AGameStateBase* GS = World->GetGameState();
		if (!GS || !GS->PlayerArray.IsValidIndex(Index))
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Playa] TN.Beach.Worm: no hay jugador %d."), Index);
			return;
		}
		const APlayerState* PS = GS->PlayerArray[Index];
		ACharacter* Eaten = PS ? Cast<ACharacter>(PS->GetPawn()) : nullptr;
		if (!Eaten)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Playa] TN.Beach.Worm: el jugador %d no tiene tortuga."), Index);
			return;
		}
		if (!ATN_BeachSandWorm::EatTurtle(Eaten))
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Playa] TN.Beach.Worm: %s ya está en la boca de un gusano."), *GetNameSafe(Eaten));
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs CmdBeachWorm(TEXT("TN.Beach.Worm"),
		TEXT("Un gusano de arena se come ya a la tortuga del jugador: TN.Beach.Worm [índice en PlayerArray=0] (en el anfitrión)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&EatNow), ECVF_Cheat);
}
