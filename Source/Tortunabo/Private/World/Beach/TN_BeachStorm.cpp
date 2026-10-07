#include "World/Beach/TN_BeachStorm.h"
#include "Multiplayer/TN_LocalViews.h"
#include "World/Beach/TN_BeachCameraShake.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "World/Beach/TN_BeachEnemySynth.h"
#include "World/Beach/TN_BeachStormKick.h"
#include "World/Beach/TN_BeachStun.h"
#include "Game/TN_CoopItemComponent.h"
#include "World/ProcMap/TN_PathStormFX.h"
#include "World/ProcMap/TN_StormCough.h"
#include "TN_BeachEnemyKit.h"
#include "TN_BeachEnemyMeshes.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TN_Log.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Net/UnrealNetwork.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_ShellBody.h"
#include "Player/TN_ShellComponent.h"
#include "Player/TortugaCharacter.h"
#include "World/Beach/TN_BeachShelterVolume.h"

namespace TNBeachStormTuning
{
	/** Trastos volando a la vez (alrededor de la cámara), bañistas a lo ancho y piernas de patada a la vez. */
	constexpr int32 NumDebris = 24;
	constexpr int32 NumBathers = 8;
	constexpr int32 NumKickLegs = 2;
	/** El frente se ve y se simula si la cámara está a menos de esto (cm, a lo largo de la carrera). */
	constexpr float VisibleRange = 45000.f;
	/** Cada cuánto mira el servidor quién está detrás del frente y a qué velocidad debe ir (s). */
	constexpr float CheckInterval = 0.2f;
	constexpr float SpeedInterval = 1.f;
	/** Segundos dentro para la peor tos (la tormenta de la playa no mata). */
	constexpr float WorstCoughSeconds = 12.f;
	/** Altura de la cadera de un bañista y media separación de las piernas (cm reales). */
	constexpr double HipHeight = 96.0;
	constexpr double HipHalfWidth = 10.0;
	/** Alto del velo (cm): una pared de arena sobre la playa, no una nube en el cielo. */
	constexpr double VeilHeight = 5500.0;
	/** Fundidos (s): de un trasto al nacer y al morir, de un bañista y de la pierna de la patada. */
	constexpr float DebrisFadeIn = 0.45f;
	constexpr float DebrisFadeOut = 0.6f;
	constexpr float BatherFade = 0.7f;
	/** Patada: la pierna barre en KickSwing s, se queda KickHold y se funde en KickFadeOut (sube un poco). */
	constexpr float KickSwing = 0.25f;
	constexpr float KickHold = 0.2f;
	constexpr float KickFadeOut = 0.45f;
	/** Gravedad con la que se calcula el vuelo de la bola de la patada (cm/s²) si el mundo no dice otra. */
	constexpr float KickGravity = 980.f;
	/** Vuelos que se prueban para que el arco salve lo que haya delante (s): el calculado, uno alto y uno muy alto. */
	constexpr float KickHighFlight = 2.6f;
	constexpr float KickHigherFlight = 3.2f;
	/** Puntos del arco que se comprueban y el radio de la bola que se barre (cm). Se deja sin mirar el último 12 %. */
	constexpr int32 KickArcSamples = 8;
	constexpr float KickArcRadius = 24.f;
	constexpr float KickArcSkipEnd = 0.12f;
	/** Sitios a lo ancho que se prueban por delante (cm a cada lado) antes de buscar alrededor, y hasta dónde (cm). */
	constexpr float KickLateralStep = 400.f;
	constexpr float KickSearchRadius = 1500.f;
	/** Reserva de la patada: el vuelo más este margen (s). Parada (cm/s) y cuánto (s) para darla por atascada. */
	constexpr float KickClaimPad = 2.5f;
	constexpr float KickStuckSpeed = 120.f;
	constexpr float KickStuckSeconds = 0.35f;
	/** Sin sitio por delante: segundos hasta volver a buscar. */
	constexpr float KickRetrySeconds = 1.f;
	/** Detrás del frente con algo que acaba solo (derribo, bola de aturdida, lanzamiento) más de esto (s): se patea igual. */
	constexpr float KickForceAfter = 8.f;

	/** Lo que mueve de verdad a la tortuga (la caja de su bola, si la sigue en esta máquina) y su velocidad. */
	FVector TurtleBody(const ATortugaCharacter& Turtle, FVector& OutVelocity)
	{
		if (const UTN_ShellComponent* Shell = Turtle.GetShellComponent())
		{
			const ATN_ShellBody* Body = Shell->GetBody();
			UBoxComponent* Box = Body ? Body->GetBox() : nullptr;
			if (Shell->HasLocalBody() && Box)
			{
				OutVelocity = Box->GetPhysicsLinearVelocity();
				return Box->GetComponentLocation();
			}
		}
		OutVelocity = Turtle.GetVelocity();
		return Turtle.GetActorLocation();
	}

	/** El arco de la bola (lógica pura, TN_BeachStormKick.h). */
	using TNBeachStormKick::BallisticLaunch;
	using TNBeachStormKick::BallisticPoint;

	/**
	 * Lo que hace falta para el arco de la bola de Turtle: gravedad del mundo, amortiguación de la caja y cuánto queda la
	 * caja por debajo de la cápsula de pie (sobre su cara de abajo).
	 */
	void KickBallistics(const UWorld& World, const ATortugaCharacter& Turtle, float& OutGravity, float& OutDamping, FVector& OutBoxBelowStand)
	{
		OutGravity = World.GetGravityZ() < -1.f ? -World.GetGravityZ() : KickGravity;
		const ATN_ShellBody* BodyDefaults = GetDefault<ATN_ShellBody>();
		OutDamping = BodyDefaults && BodyDefaults->GetBox() ? BodyDefaults->GetBox()->BodyInstance.LinearDamping : 0.25f;
		const ACharacter* TurtleDefaults = Turtle.GetClass()->GetDefaultObject<ACharacter>();
		const float StandHalfHeight = TurtleDefaults && TurtleDefaults->GetCapsuleComponent() ? TurtleDefaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.f;
		OutBoxBelowStand = FVector(0.0, 0.0, StandHalfHeight - ATN_ShellBody::BoxHalfExtent().Z - 10.0);
	}

	/** Las consultas de la bola no cuentan las tortugas ni sus bolas (se apartan o la bola las empuja). */
	void IgnoreTurtles(const AActor& Context, const ATortugaCharacter* Turtle, FCollisionQueryParams& Query)
	{
		TArray<ATortugaCharacter*> Turtles;
		ATN_BeachEnemy::GatherTurtles(&Context, Turtles);
		if (Turtle)
		{
			Turtles.AddUnique(const_cast<ATortugaCharacter*>(Turtle));
		}
		for (const ATortugaCharacter* Other : Turtles)
		{
			Query.AddIgnoredActor(Other);
			if (const UTN_ShellComponent* Shell = Other->GetShellComponent())
			{
				Query.AddIgnoredActor(Shell->GetBody());
			}
		}
	}

	/** La bola de Turtle vuelve a chocar como siempre (si la tiene y estaba atravesando). */
	void EndPassThrough(const ATortugaCharacter* Turtle)
	{
		const UTN_ShellComponent* Shell = Turtle ? Turtle->GetShellComponent() : nullptr;
		if (ATN_ShellBody* Body = Shell ? Shell->GetBody() : nullptr)
		{
			Body->SetPassThrough(false);
		}
	}

	inline FLinearColor SandVeil()
	{
		return FLinearColor(0.78f, 0.66f, 0.48f, 1.f);
	}

	inline FLinearColor SandFog()
	{
		return FLinearColor(0.62f, 0.5f, 0.34f, 1.f);
	}

	/** Trastos grandes (sombrilla, silla, toalla, flotador): vuelan más alto y más despacio, planeando. */
	inline bool IsBigItem(TNBeachMeshes::EStormItem Item)
	{
		using TNBeachMeshes::EStormItem;
		return Item == EStormItem::Umbrella || Item == EStormItem::Chair || Item == EStormItem::Towel || Item == EStormItem::Float;
	}

	inline float Smooth01(float X)
	{
		const float C = FMath::Clamp(X, 0.f, 1.f);
		return C * C * (3.f - 2.f * C);
	}
}

ATN_BeachStorm::ATN_BeachStorm()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(2.f);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	InsidePostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("InsidePostProcess"));
	InsidePostProcess->SetupAttachment(Root);
	InsidePostProcess->bUnbound = true;
	InsidePostProcess->bEnabled = false;
}

void ATN_BeachStorm::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_BeachStorm, StartOffset);
	DOREPLIFETIME(ATN_BeachStorm, FrontSpeed);
	DOREPLIFETIME(ATN_BeachStorm, FrontAccel);
	DOREPLIFETIME(ATN_BeachStorm, TargetSpeed);
	DOREPLIFETIME(ATN_BeachStorm, StartServerTime);
	DOREPLIFETIME(ATN_BeachStorm, Grace);
	DOREPLIFETIME(ATN_BeachStorm, FrozenFront);
	DOREPLIFETIME(ATN_BeachStorm, bActive);
	DOREPLIFETIME(ATN_BeachStorm, bShown);
}

void ATN_BeachStorm::BeginPlay()
{
	Super::BeginPlay();
	Rng.GenerateNewSeed();
	FrontGroundZ = static_cast<float>(GetActorLocation().Z);
	BaseSpeed = DefaultSpeed;
}

void ATN_BeachStorm::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Quitada con patadas en vuelo (ronda nueva): nadie se queda reservado.
	ReleaseAllFlights();
	RestoreFog();
	Super::EndPlay(EndPlayReason);
}

ATN_BeachStorm* ATN_BeachStorm::FindStorm(const UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	TActorIterator<ATN_BeachStorm> It(World);
	return It ? *It : nullptr;
}

float ATN_BeachStorm::GroundAt(const FVector& Where)
{
	// Traza por el canal de visibilidad (los muros invisibles no lo bloquean), desde poco por encima de la tormenta, sin
	// contar lo que empieza dentro. Trazar contra lo estático desde 60 m por encima del último suelo encontrado subía la
	// cota en cada traza al dar con un muro invisible, y la tormenta acababa «arriba del todo».
	const UWorld* World = GetWorld();
	const double Base = GetActorLocation().Z;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BeachStormGround), false, this);
	if (World && World->LineTraceSingleByChannel(Hit, FVector(Where.X, Where.Y, Base + 3000.0), FVector(Where.X, Where.Y, Base - 20000.0), ECC_Visibility, Params)
		&& !Hit.bStartPenetrating)
	{
		return static_cast<float>(Hit.ImpactPoint.Z);
	}
	return bFrontGroundValid ? FrontGroundZ : static_cast<float>(Base);
}

// ─────────────────────────────────────────────────────────────────────────────
// Marcha del frente
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachStorm::StartStorm()
{
	StartStormAt(0.f, DefaultSpeed, DefaultGrace);
}

void ATN_BeachStorm::StartStormAt(float InStartOffset, float Speed, float GraceSeconds)
{
	if (!HasAuthority())
	{
		return;
	}
	// Sale parada y coge velocidad poco a poco (StartAccel) hasta Speed.
	BaseSpeed = FMath::Max(0.f, Speed);
	StartOffset = InStartOffset;
	FrontSpeed = 0.f;
	TargetSpeed = BaseSpeed;
	FrontAccel = FMath::Max(1.f, StartAccel);
	Grace = FMath::Max(0.f, GraceSeconds);
	StartServerTime = static_cast<float>(ATN_BeachEnemy::ServerNow(this));
	MarchStartTime = static_cast<double>(StartServerTime) + Grace;
	FrozenFront = StartOffset;
	bActive = true;
	bShown = true;
	bCatchingUp = false;
	BehindFor.Reset();
	ReleaseAllFlights();
	RetryAfter.Reset();
	CheckTimer = 0.f;
	SpeedTimer = 0.f;
	ForceNetUpdate();
	UE_LOG(LogTortunabo, Log, TEXT("[Playa] Tormenta de bañistas: frente a %.0f cm del actor, %.0f cm/s tras %.1f s de gracia (acelera al final)."),
		StartOffset, BaseSpeed, Grace);
}

void ATN_BeachStorm::StopStorm()
{
	if (!HasAuthority())
	{
		return;
	}
	FrozenFront = GetFrontDistance();
	bActive = false;
	BehindFor.Reset();
	// Las que vuelan siguen su física hasta caer (sin reserva): la ronda se acaba y nadie más las mueve.
	ReleaseAllFlights();
	RetryAfter.Reset();
	ForceNetUpdate();
	UE_LOG(LogTortunabo, Log, TEXT("[Playa] Tormenta de bañistas parada en %.0f cm."), FrozenFront);
}

void ATN_BeachStorm::DebugSetFront(float InFront)
{
	if (!HasAuthority())
	{
		return;
	}
	// Un tramo nuevo desde InFront a la velocidad que llevaba (parada, a la normal), sin gracia.
	const float Speed = bActive ? FMath::Max(GetFrontSpeed(), BaseSpeed) : FMath::Max(BaseSpeed, 1.f);
	const float Now = static_cast<float>(ATN_BeachEnemy::ServerNow(this));
	if (!bActive)
	{
		MarchStartTime = Now;
	}
	StartOffset = InFront;
	StartServerTime = Now;
	Grace = 0.f;
	FrontSpeed = Speed;
	TargetSpeed = Speed;
	FrontAccel = FMath::Max(1.f, SpeedChangeAccel);
	FrozenFront = InFront;
	bActive = true;
	bShown = true;
	BehindFor.Reset();
	RetryAfter.Reset();
	ForceNetUpdate();
	UE_LOG(LogTortunabo, Log, TEXT("[Playa] Tormenta: frente puesto a %.0f cm del actor, a %.0f cm/s."), InFront, Speed);
}

void ATN_BeachStorm::SegmentAt(double T, float& OutDistance, float& OutSpeed) const
{
	const double V0 = FrontSpeed;
	const double A = FrontAccel;
	const double VT = TargetSpeed;
	if (FMath::IsNearlyZero(A) || FMath::IsNearlyEqual(V0, VT))
	{
		OutDistance = static_cast<float>(V0 * T);
		OutSpeed = static_cast<float>(V0);
		return;
	}
	// Acelera (o frena) hasta la velocidad a la que va y sigue a esa.
	const double Tc = FMath::Max(0.0, (VT - V0) / A);
	if (T <= Tc)
	{
		OutDistance = static_cast<float>(V0 * T + 0.5 * A * T * T);
		OutSpeed = static_cast<float>(V0 + A * T);
		return;
	}
	OutDistance = static_cast<float>(V0 * Tc + 0.5 * A * Tc * Tc + VT * (T - Tc));
	OutSpeed = static_cast<float>(VT);
}

float ATN_BeachStorm::GetFrontDistance() const
{
	if (!bActive)
	{
		return FrozenFront;
	}
	const double T = FMath::Max(0.0, ATN_BeachEnemy::ServerNow(this) - static_cast<double>(StartServerTime) - static_cast<double>(Grace));
	float Distance = 0.f;
	float Speed = 0.f;
	SegmentAt(T, Distance, Speed);
	return StartOffset + Distance;
}

float ATN_BeachStorm::GetFrontSpeed() const
{
	if (!bActive)
	{
		return 0.f;
	}
	const double T = ATN_BeachEnemy::ServerNow(this) - static_cast<double>(StartServerTime) - static_cast<double>(Grace);
	if (T <= 0.0)
	{
		return 0.f;
	}
	float Distance = 0.f;
	float Speed = 0.f;
	SegmentAt(T, Distance, Speed);
	return Speed;
}

FVector ATN_BeachStorm::GetFrontLocation() const
{
	return GetActorTransform().TransformPositionNoScale(FVector(GetFrontDistance(), 0.0, 0.0));
}

bool ATN_BeachStorm::IsLocationInside(const FVector& WorldLocation) const
{
	if (!bActive)
	{
		return false;
	}
	const FVector Local = GetActorTransform().InverseTransformPositionNoScale(WorldLocation);
	return Local.X < GetFrontDistance() - InsideMargin && FMath::Abs(Local.Y) < HalfWidth;
}

bool ATN_BeachStorm::IsBehindFront(const FVector& WorldLocation, float Margin) const
{
	if (!bActive)
	{
		return false;
	}
	const FVector Local = GetActorTransform().InverseTransformPositionNoScale(WorldLocation);
	return Local.X < GetFrontDistance() - Margin && FMath::Abs(Local.Y) < HalfWidth;
}

void ATN_BeachStorm::ChangeSpeed(float NewTarget)
{
	// Tramo nuevo desde donde está ahora mismo: los clientes lo calculan igual con la hora replicada.
	const float Front = GetFrontDistance();
	const float Speed = GetFrontSpeed();
	StartOffset = Front;
	StartServerTime = static_cast<float>(ATN_BeachEnemy::ServerNow(this));
	Grace = 0.f;
	FrontSpeed = Speed;
	TargetSpeed = FMath::Max(0.f, NewTarget);
	FrontAccel = (TargetSpeed >= Speed ? 1.f : -1.f) * FMath::Max(1.f, SpeedChangeAccel);
	ForceNetUpdate();
	UE_LOG(LogTortunabo, Verbose, TEXT("[Playa] Tormenta: de %.0f a %.0f cm/s en %.0f cm."), Speed, TargetSpeed, Front);
}

void ATN_BeachStorm::ServerUpdateSpeed()
{
	const double Now = ATN_BeachEnemy::ServerNow(this);
	if (Now < MarchStartTime)
	{
		return;
	}
	float Want = BaseSpeed;
	// Ronda larga: acelera poco a poco.
	const float Marching = static_cast<float>(Now - MarchStartTime);
	if (Marching > LateStartSeconds)
	{
		Want = BaseSpeed + SpeedRampPerMinute * (Marching - LateStartSeconds) / 60.f;
	}
	TArray<ATortugaCharacter*> Turtles;
	ATN_BeachEnemy::GatherTurtles(this, Turtles);
	if (Turtles.Num() > 0)
	{
		const FTransform Xf = GetActorTransform();
		const float Front = GetFrontDistance();
		float Lead = 0.f;
		float Rear = 1.0e9f;
		for (const ATortugaCharacter* Turtle : Turtles)
		{
			const float LocalX = static_cast<float>(Xf.InverseTransformPositionNoScale(Turtle->GetActorLocation()).X);
			Rear = FMath::Min(Rear, LocalX);
			// Lo recorrido a lo largo de la X del actor, en partes de CourseLength.
			Lead = FMath::Max(Lead, LocalX / FMath::Max(1.f, CourseLength));
		}
		// Final de la ronda: la primera está cerca del mar.
		if (Lead >= EndRushProgress)
		{
			Want = FMath::Max(Want, EndRushSpeed);
		}
		// Que la última siempre la note: si le saca mucho, la alcanza (sin echársele encima).
		const float Gap = Rear - Front;
		if (Gap > CatchUpGap)
		{
			bCatchingUp = true;
		}
		else if (Gap < CatchUpRelease)
		{
			bCatchingUp = false;
		}
		if (bCatchingUp)
		{
			Want = FMath::Max(Want, CatchUpSpeed);
		}
	}
	Want = FMath::Min(Want, MaxSpeed);
	if (!FMath::IsNearlyEqual(Want, TargetSpeed, 2.f))
	{
		ChangeSpeed(Want);
	}
}

FString ATN_BeachStorm::DescribeState() const
{
	TArray<ATortugaCharacter*> Turtles;
	ATN_BeachEnemy::GatherTurtles(this, Turtles);
	const FTransform Xf = GetActorTransform();
	const float Front = GetFrontDistance();
	float Rear = 1.0e9f;
	for (const ATortugaCharacter* Turtle : Turtles)
	{
		Rear = FMath::Min(Rear, static_cast<float>(Xf.InverseTransformPositionNoScale(Turtle->GetActorLocation()).X));
	}
	return FString::Printf(TEXT("activa %d · frente %.0f m · %.2f m/s (va a %.2f) · última tortuga a %.0f m por delante%s"),
		bActive ? 1 : 0, Front / 100.f, GetFrontSpeed() / 100.f, TargetSpeed / 100.f, Turtles.Num() > 0 ? (Rear - Front) / 100.f : 0.f,
		bCatchingUp ? TEXT(" · alcanzando") : TEXT(""));
}

void ATN_BeachStorm::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && Flights.Num() > 0)
	{
		ServerTickFlights(DeltaSeconds);
	}
	if (HasAuthority() && bActive)
	{
		CheckTimer += DeltaSeconds;
		if (CheckTimer >= TNBeachStormTuning::CheckInterval)
		{
			CheckTimer = 0.f;
			ServerCheck();
		}
		SpeedTimer += DeltaSeconds;
		if (SpeedTimer >= TNBeachStormTuning::SpeedInterval)
		{
			SpeedTimer = 0.f;
			ServerUpdateSpeed();
		}
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		TickFX(DeltaSeconds);
		TickCough(DeltaSeconds);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor: la patada a quien se queda detrás del frente
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachStorm::ServerCheck()
{
	TArray<ATortugaCharacter*> Turtles;
	ATN_BeachEnemy::GatherTurtles(this, Turtles);
	const double WorldNow = GetWorld()->GetTimeSeconds();
	const FTransform Xf = GetActorTransform();
	const float Front = GetFrontDistance();
	const float Speed = GetFrontSpeed();
	for (ATortugaCharacter* Turtle : Turtles)
	{
		float& Behind = BehindFor.FindOrAdd(Turtle);
		const FVector Local = Xf.InverseTransformPositionNoScale(Turtle->GetActorLocation());
		// Dentro de un búnker (#689) la tormenta no la empuja.
		if (Local.X >= Front - KickSlack || FMath::Abs(Local.Y) >= HalfWidth || ATN_BeachShelterVolume::IsSheltered(Turtle))
		{
			Behind = 0.f;
			continue;
		}
		Behind += TNBeachStormTuning::CheckInterval;
		// Ni en plena patada (la lleva la tormenta hasta su sitio), ni en la gracia de después.
		if (Behind < KickDelay || Flights.Contains(Turtle) || TNBeach::HasStormGrace(Turtle))
		{
			continue;
		}
		// Solo a la que se mueve sola o va en su bola porque quiere. Lo demás manda y la patada llega al soltarla: el pico de
		// una gaviota o la pinza de un cangrejo, los brazos de otra (patean a la que la lleva), el derribo, la bola de
		// aturdida, un lanzamiento por el aire y la red de seguridad. Si algo de lo que acaba solo (derribo, bola, lanzamiento)
		// se alarga demasiado detrás del frente, se patea igual.
		const TNBeach::ETNBeachMover Mover = TNBeach::GetTurtleMover(Turtle);
		const bool bOwnBall = Mover == TNBeach::ETNBeachMover::Ball && !TNBeach::IsTurtleStunned(Turtle);
		// Sin movimiento y sin nada de lo anterior: la tiene otra cosa (dentro de una concha que atrapa, por ejemplo), que la
		// suelta sola.
		const UCharacterMovementComponent* Move = Turtle->GetCharacterMovement();
		const bool bHeldElsewhere = Mover == TNBeach::ETNBeachMover::None && Move && Move->MovementMode == MOVE_None;
		const bool bEndsAlone = Mover == TNBeach::ETNBeachMover::Ball || Mover == TNBeach::ETNBeachMover::Knockdown || Mover == TNBeach::ETNBeachMover::Launch
			|| bHeldElsewhere;
		if ((Mover != TNBeach::ETNBeachMover::None || bHeldElsewhere) && !bOwnBall && !(bEndsAlone && Behind >= TNBeachStormTuning::KickForceAfter))
		{
			continue;
		}
		if (const double* Retry = RetryAfter.Find(Turtle))
		{
			if (WorldNow < *Retry)
			{
				continue;
			}
		}
		if (KickTurtle(Turtle, Front, Speed))
		{
			Behind = 0.f;
			RetryAfter.Remove(Turtle);
		}
		else
		{
			RetryAfter.Add(Turtle, WorldNow + TNBeachStormTuning::KickRetrySeconds);
		}
	}
	for (auto It = BehindFor.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = RetryAfter.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

FVector ATN_BeachStorm::PointAhead(const FVector& From, float Ahead)
{
	const FTransform Xf = GetActorTransform();
	FVector Local = Xf.InverseTransformPositionNoScale(From);
	Local.X = GetFrontDistance() + Ahead;
	return Xf.TransformPositionNoScale(Local);
}

bool ATN_BeachStorm::IsKickArcClear(const ATortugaCharacter* Turtle, const FVector& From, const FVector& Launch, float Flight, float Damping) const
{
	const UWorld* World = GetWorld();
	if (!World || !Turtle)
	{
		return false;
	}
	const float Gravity = World->GetGravityZ() < -1.f ? -World->GetGravityZ() : TNBeachStormTuning::KickGravity;
	// Lo que para a una bola, sin las tortugas ni sus bolas (se apartan o la bola las empuja).
	FCollisionQueryParams Query(SCENE_QUERY_STAT(BeachStormKickArc), false, Turtle);
	TNBeachStormTuning::IgnoreTurtles(*this, Turtle, Query);
	const FCollisionShape Ball = FCollisionShape::MakeSphere(TNBeachStormTuning::KickArcRadius);
	// Desde algo por encima (la caja de una bola quieta toca la arena) y sin el último tramo, que baja a la arena ya mirada.
	FVector Prev = From + FVector(0.0, 0.0, 30.0);
	const float Last = Flight * (1.f - TNBeachStormTuning::KickArcSkipEnd);
	for (int32 i = 1; i <= TNBeachStormTuning::KickArcSamples; ++i)
	{
		const float T = Last * static_cast<float>(i) / static_cast<float>(TNBeachStormTuning::KickArcSamples);
		const FVector Point = TNBeachStormTuning::BallisticPoint(From, Launch, T, Gravity, Damping);
		// El primer tramo sale de donde está la tortuga: si ya toca la pared en la que se apoya, eso no tapa el arco (la bola
		// sale despacio de lo que solapa al nacer); lo que haya más allá, sí.
		Query.bFindInitialOverlaps = i > 1;
		FHitResult Hit;
		if (World->SweepSingleByChannel(Hit, Prev, Point, FQuat::Identity, ECC_PhysicsBody, Ball, Query))
		{
			return false;
		}
		Prev = Point;
	}
	return true;
}

bool ATN_BeachStorm::KickTurtle(ATortugaCharacter* Turtle, float Front, float Speed)
{
	UWorld* World = GetWorld();
	if (!World || !IsValid(Turtle))
	{
		return false;
	}
	// Protegida por el pez globo: no se la puede meter en su bola. Se vuelve a mirar en KickRetrySeconds; nunca se la mueve
	// sin vuelo.
	if (UTN_CoopItemComponent::IsTurtleProtected(Turtle))
	{
		UE_LOG(LogTortunabo, Verbose, TEXT("[Playa] Tormenta: %s está protegida; se la patea cuando se le pase."), *GetNameSafe(Turtle));
		return false;
	}
	const FTransform Xf = GetActorTransform();
	FVector BodyVelocity = FVector::ZeroVector;
	const FVector From = TNBeachStormTuning::TurtleBody(*Turtle, BodyVelocity);
	const float LocalX = static_cast<float>(Xf.InverseTransformPositionNoScale(From).X);

	// Vuelo más largo cuanto más lejos tiene que llegar: KickAhead por delante del frente, que mientras sigue andando.
	const float Flight0 = FMath::Clamp(1.f + (Front + KickAhead - LocalX) / 2800.f, KickMinFlight, KickMaxFlight);
	const FVector Base = PointAhead(From, KickAhead + Speed * (Flight0 + 0.5f));
	// Al final del recorrido el sitio de delante (a 30 m del filo) puede quedar ya detrás del frente: no se patea.
	if (Xf.InverseTransformPositionNoScale(Base).X < Front + KickSlack)
	{
		UE_LOG(LogTortunabo, Verbose, TEXT("[Playa] Tormenta: sin sitio por delante del frente para %s (fin del recorrido)."), *GetNameSafe(Turtle));
		return false;
	}

	// La bola: gravedad del mundo, amortiguación de la caja y la cápsula de pie sobre su cara de abajo.
	float Gravity = 0.f;
	float Damping = 0.f;
	FVector BoxBelowStand = FVector::ZeroVector;
	TNBeachStormTuning::KickBallistics(*World, *Turtle, Gravity, Damping, BoxBelowStand);
	const bool bSwimming = Turtle->GetCharacterMovement() && Turtle->GetCharacterMovement()->IsSwimming();

	// El primer sitio que valga (por delante, luego más cerca y luego alrededor): si ningún arco llega, la bola vuela hasta
	// él atravesando lo que haya.
	FTransform FallbackSpot;
	bool bHaveFallback = false;
	enum class ETry : uint8 { NoArc, Kicked, NoBall };
	// Cada sitio con tres vuelos: el calculado y dos más altos (para salvar lo que haya delante). Nadando (la bola saldría
	// en el agua) o demasiado lejos para un vuelo razonable, sin arco con física.
	const auto TryArcs = [&](const FTransform& Spot, float FlightBase, float Lateral) -> ETry
	{
		if (!bHaveFallback)
		{
			FallbackSpot = Spot;
			bHaveFallback = true;
		}
		if (bSwimming || FVector::Dist2D(From, Spot.GetLocation()) > KickMaxFlightDistance)
		{
			return ETry::NoArc;
		}
		const FVector BoxTo = Spot.GetLocation() - BoxBelowStand;
		constexpr int32 NumFlightTimes = 3;
		const float FlightTimes[NumFlightTimes] = { FlightBase, FMath::Max(FlightBase, TNBeachStormTuning::KickHighFlight),
			FMath::Max(FlightBase, TNBeachStormTuning::KickHigherFlight) };
		for (int32 f = 0; f < NumFlightTimes; ++f)
		{
			if (f > 0 && FMath::IsNearlyEqual(FlightTimes[f], FlightTimes[f - 1]))
			{
				continue;
			}
			const float Flight = FlightTimes[f];
			const FVector Launch = TNBeachStormTuning::BallisticLaunch(From, BoxTo, Flight, Gravity, Damping);
			if (!IsKickArcClear(Turtle, From, Launch, Flight, Damping))
			{
				continue;
			}
			if (!LaunchKick(Turtle, From, Spot.GetLocation(), Flight, false, 0))
			{
				return ETry::NoBall;
			}
			UE_LOG(LogTortunabo, Log, TEXT("[Playa] La tormenta patea a %s: %.0f m detrás del frente, vuela %.1f s hasta su sitio, %.0f m por delante (%.0f m a lo ancho)."),
				*GetNameSafe(Turtle), (Front - LocalX) / 100.f, Flight, (Xf.InverseTransformPositionNoScale(Spot.GetLocation()).X - Front) / 100.f, Lateral / 100.f);
			return ETry::Kicked;
		}
		return ETry::NoArc;
	};

	// Por delante a su altura de la playa y, si ahí no se puede, a los lados; después lo mismo más cerca del frente (arco
	// corto: menos cosas por medio) y, si por delante no había ni un sitio, alrededor.
	const FVector Right = Xf.GetUnitAxis(EAxis::Y);
	const float Offsets[] = { 0.f, TNBeachStormTuning::KickLateralStep, -TNBeachStormTuning::KickLateralStep,
		2.f * TNBeachStormTuning::KickLateralStep, -2.f * TNBeachStormTuning::KickLateralStep };
	const float ShortFlight = FMath::Clamp(1.f + (Front + KickShortAhead - LocalX) / 2800.f, KickMinFlight, KickMaxFlight);
	const FVector ShortBase = PointAhead(From, KickShortAhead + Speed * (ShortFlight + 0.5f));
	const bool bShortAhead = KickShortAhead < KickAhead && Xf.InverseTransformPositionNoScale(ShortBase).X >= Front + KickSlack;
	ETry Result = ETry::NoArc;
	for (int32 Row = 0; Row < 2 && Result == ETry::NoArc; ++Row)
	{
		if (Row == 1 && !bShortAhead)
		{
			break;
		}
		const FVector RowBase = Row == 0 ? Base : ShortBase;
		const float RowFlight = Row == 0 ? Flight0 : ShortFlight;
		for (const float Offset : Offsets)
		{
			FTransform Spot;
			if (TNBeach::FindOpenSandSpot(Turtle, RowBase + Right * Offset, 0.f, Spot))
			{
				Result = TryArcs(Spot, RowFlight, Offset);
				if (Result != ETry::NoArc)
				{
					break;
				}
			}
		}
	}
	if (Result == ETry::NoArc && !bHaveFallback)
	{
		FTransform Around;
		if (TNBeach::FindOpenSandSpot(Turtle, Base, TNBeachStormTuning::KickSearchRadius, Around))
		{
			Result = TryArcs(Around, Flight0, 0.f);
		}
	}
	if (Result == ETry::Kicked)
	{
		return true;
	}
	if (!bHaveFallback)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Playa] Tormenta: sin sitio de arena abierta por delante del frente para %s (se vuelve a mirar en %.0f s)."),
			*GetNameSafe(Turtle), TNBeachStormTuning::KickRetrySeconds);
		return false;
	}
	// Ningún arco libre (una pared delante, un sitio estrecho, nadando o muy lejos): la patada se ve igual. La bola vuela
	// alto atravesando lo que haya y vuelve a chocar al bajar sobre su sitio.
	const float PassFlight = FMath::Max(Flight0, TNBeachStormTuning::KickHighFlight);
	if (Result != ETry::NoBall && LaunchKick(Turtle, From, FallbackSpot.GetLocation(), PassFlight, true, 0))
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Playa] La tormenta patea a %s: %.0f m detrás del frente, sin arco libre%s: vuela %.1f s atravesando hasta su sitio, %.0f m por delante."),
			*GetNameSafe(Turtle), (Front - LocalX) / 100.f, bSwimming ? TEXT(" (nadando)") : TEXT(""), PassFlight,
			(Xf.InverseTransformPositionNoScale(FallbackSpot.GetLocation()).X - Front) / 100.f);
		return true;
	}
	// Último recurso, sin bola posible (la tiene otra cosa que no la suelta): a su sitio sin vuelo.
	MulticastKick(Turtle, From);
	PlaceKicked(Turtle, FallbackSpot, TEXT("sin bola posible"));
	return true;
}

bool ATN_BeachStorm::LaunchKick(ATortugaCharacter* Turtle, const FVector& From, const FVector& Target, float Flight, bool bPassThrough, int32 Hops)
{
	UWorld* World = GetWorld();
	if (!World || !IsValid(Turtle))
	{
		return false;
	}
	float Gravity = 0.f;
	float Damping = 0.f;
	FVector BoxBelowStand = FVector::ZeroVector;
	TNBeachStormTuning::KickBallistics(*World, *Turtle, Gravity, Damping, BoxBelowStand);
	const FVector Launch = TNBeachStormTuning::BallisticLaunch(From, Target - BoxBelowStand, Flight, Gravity, Damping);
	// Patada: dentro del caparazón y lanzada en bola, mareada lo que dura el vuelo y un poco más. Después se la reserva la
	// tormenta (StunTurtle no aturde a una tortuga reservada) hasta que aterrice.
	TNBeach::StunTurtle(Turtle, Flight + KickStunExtra, Launch);
	const UTN_ShellComponent* Shell = Turtle->GetShellComponent();
	ATN_ShellBody* Body = Shell ? Shell->GetBody() : nullptr;
	if (!Body)
	{
		// Sin bola (la lleva otra, por ejemplo): quien llama decide.
		return false;
	}
	Body->SetPassThrough(bPassThrough);
	TNBeach::ClaimTurtle(Turtle, TNBeach::ETNBeachMover::StormKick, Flight + TNBeachStormTuning::KickClaimPad);
	FKickFlight& Kick = Flights.Add(Turtle);
	Kick.Target = Target;
	Kick.StartTime = World->GetTimeSeconds();
	Kick.Flight = Flight;
	Kick.bPassThrough = bPassThrough;
	Kick.Hops = Hops;
	MulticastKick(Turtle, From);
	return true;
}

void ATN_BeachStorm::RetryKick(ATortugaCharacter* Turtle, const FVector& Target, int32 Hops, const TCHAR* Why)
{
	if (!IsValid(Turtle))
	{
		return;
	}
	// El sitio se miró al patear: si ahora no vale (otra tortuga encima) o ya ha quedado detrás del frente, uno cerca o por
	// delante del frente.
	const FRotator Yaw(0.f, Turtle->GetActorRotation().Yaw, 0.f);
	FTransform Spot(Yaw, Target);
	const bool bTargetBehind = GetActorTransform().InverseTransformPositionNoScale(Target).X < GetFrontDistance() + KickSlack;
	const bool bFound = bTargetBehind
		? (FindSpotAhead(Turtle, 1.f, Spot) || TNBeach::FindOpenSandSpot(Turtle, Target, 600.f, Spot))
		: (TNBeach::FindOpenSandSpot(Turtle, Target, 600.f, Spot) || FindSpotAhead(Turtle, 1.f, Spot));
	if (!bFound)
	{
		Spot = FTransform(Yaw, Target);
	}
	if (TNBeachStormKick::ResolveFailedLanding(Hops) == TNBeachStormKick::EFailedLanding::Hop)
	{
		// Otra patada visible desde donde se ha quedado la bola, atravesando lo que haya. Se suelta antes la reserva de la que
		// no ha llegado: StunTurtle no aturde a una tortuga reservada.
		FVector Velocity = FVector::ZeroVector;
		const FVector From = TNBeachStormTuning::TurtleBody(*Turtle, Velocity);
		const float Distance = static_cast<float>(FVector::Dist2D(From, Spot.GetLocation()));
		const float Flight = FMath::Clamp(1.f + Distance / 2800.f, KickMinFlight, KickMaxFlight);
		TNBeach::ReleaseTurtle(Turtle, TNBeach::ETNBeachMover::StormKick);
		if (LaunchKick(Turtle, From, Spot.GetLocation(), Flight, true, Hops + 1))
		{
			UE_LOG(LogTortunabo, Log, TEXT("[Playa] Tormenta: %s no ha llegado a su sitio (%s): otra patada desde donde está, %.0f m en %.1f s."),
				*GetNameSafe(Turtle), Why ? Why : TEXT("patada"), Distance / 100.f, Flight);
			return;
		}
	}
	// Tras varias patadas que no llegan, o sin bola posible: a su sitio sin vuelo (último recurso).
	TNBeachStormTuning::EndPassThrough(Turtle);
	PlaceKicked(Turtle, Spot, Why);
}

bool ATN_BeachStorm::IsKickBallBlocked(const ATortugaCharacter* Turtle) const
{
	const UWorld* World = GetWorld();
	const UTN_ShellComponent* Shell = Turtle ? Turtle->GetShellComponent() : nullptr;
	const ATN_ShellBody* Body = Shell ? Shell->GetBody() : nullptr;
	const UBoxComponent* Box = Body ? Body->GetBox() : nullptr;
	if (!World || !Box)
	{
		return false;
	}
	// Lo que para a una bola (como el arco de la patada), sin las tortugas ni sus bolas, con un poco de holgura.
	FCollisionQueryParams Query(SCENE_QUERY_STAT(BeachStormKickPassThrough), false, Turtle);
	TNBeachStormTuning::IgnoreTurtles(*this, Turtle, Query);
	return World->OverlapBlockingTestByChannel(Box->GetComponentLocation(), Box->GetComponentQuat(), ECC_PhysicsBody,
		FCollisionShape::MakeBox(ATN_ShellBody::BoxHalfExtent() + FVector(10.0)), Query);
}

void ATN_BeachStorm::ServerTickFlights(float DeltaSeconds)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	const FTransform Xf = GetActorTransform();
	const float Front = GetFrontDistance();
	struct FRetry
	{
		TWeakObjectPtr<ATortugaCharacter> Turtle;
		FVector Target = FVector::ZeroVector;
		int32 Hops = 0;
		const TCHAR* Why = nullptr;
	};
	TArray<FRetry> ToRetry;
	TArray<TWeakObjectPtr<ATortugaCharacter>> ToFinish;
	TArray<TWeakObjectPtr<ATortugaCharacter>> ToDrop;
	for (auto It = Flights.CreateIterator(); It; ++It)
	{
		ATortugaCharacter* Turtle = It.Key().Get();
		FKickFlight& Kick = It.Value();
		if (!IsValid(Turtle) || Turtle->IsDead())
		{
			TNBeachStormTuning::EndPassThrough(Turtle);
			It.RemoveCurrent();
			continue;
		}
		// Un enemigo u otra tortuga se la han llevado en pleno vuelo: la patada acaba aquí. (Se mira directamente:
		// GetTurtleMover diría StormKick, la reserva de la propia tormenta.)
		const UTN_CarryComponent* Carry = Turtle->GetCarryComponent();
		if (ATN_BeachEnemy::IsTurtleHeld(Turtle) || (Carry && Carry->IsBeingCarried()))
		{
			ToDrop.Add(Turtle);
			It.RemoveCurrent();
			continue;
		}
		// Otro ha soltado su reserva (el rescate del vacío, la ronda nueva): ya no es suya, no se la vuelve a mover.
		if (TNBeach::GetTurtleClaim(Turtle) != TNBeach::ETNBeachMover::StormKick)
		{
			TNBeachStormTuning::EndPassThrough(Turtle);
			It.RemoveCurrent();
			continue;
		}
		FVector Velocity = FVector::ZeroVector;
		const FVector Body = TNBeachStormTuning::TurtleBody(*Turtle, Velocity);
		const bool bInShell = Turtle->IsInShell();
		const float Elapsed = static_cast<float>(Now - Kick.StartTime);
		const float Dist2D = static_cast<float>(FVector::Dist2D(Body, Kick.Target));
		// Atravesando lo que haya (sin arco libre): ni se atasca ni se hunde. Vuelve a chocar al bajar sobre su sitio sin nada
		// alrededor o, como tarde, para el último tramo, que baja a la arena ya comprobada.
		if (Kick.bPassThrough && bInShell)
		{
			const bool bCheckOverlap = Velocity.Z < 0.0 && Elapsed >= Kick.Flight * TNBeachStormKick::PassThroughEarliest
				&& Elapsed < Kick.Flight * TNBeachStormKick::PassThroughLatest;
			if (TNBeachStormKick::ShouldEndPassThrough(Elapsed, Kick.Flight, static_cast<float>(Velocity.Z), bCheckOverlap && IsKickBallBlocked(Turtle)))
			{
				TNBeachStormTuning::EndPassThrough(Turtle);
				Kick.bPassThrough = false;
				Kick.StuckTime = 0.f;
			}
			continue;
		}
		Kick.StuckTime = Velocity.SizeSquared() < FMath::Square(TNBeachStormTuning::KickStuckSpeed) ? Kick.StuckTime + DeltaSeconds : 0.f;
		const TCHAR* Why = nullptr;
		if (!Why && !bInShell && Dist2D > KickLandTolerance)
		{
			Why = TEXT("fuera de la bola antes de llegar (agua)");
		}
		if (!Why && Elapsed > 0.35f && Kick.StuckTime >= TNBeachStormTuning::KickStuckSeconds && Dist2D > KickLandTolerance)
		{
			Why = TEXT("atascada por el camino");
		}
		if (!Why && Elapsed >= Kick.Flight + 0.4f)
		{
			if (Dist2D > KickLandTolerance)
			{
				Why = TEXT("aterrizó lejos de su sitio");
			}
			else if (Xf.InverseTransformPositionNoScale(Body).X < Front + KickSlack)
			{
				Why = TEXT("aterrizó detrás del frente");
			}
			else
			{
				ToFinish.Add(Turtle);
				It.RemoveCurrent();
				continue;
			}
		}
		if (Why)
		{
			FRetry& NewRetry = ToRetry.AddDefaulted_GetRef();
			NewRetry.Turtle = Turtle;
			NewRetry.Target = Kick.Target;
			NewRetry.Hops = Kick.Hops;
			NewRetry.Why = Why;
			It.RemoveCurrent();
		}
	}
	for (const TWeakObjectPtr<ATortugaCharacter>& Weak : ToDrop)
	{
		TNBeachStormTuning::EndPassThrough(Weak.Get());
		TNBeach::ReleaseTurtle(Weak.Get(), TNBeach::ETNBeachMover::StormKick);
	}
	for (const TWeakObjectPtr<ATortugaCharacter>& Weak : ToFinish)
	{
		FinishKick(Weak.Get());
	}
	// Las que no han llegado: otra patada visible desde donde están (después del recorrido: añade otro vuelo).
	for (const FRetry& Retry : ToRetry)
	{
		RetryKick(Retry.Turtle.Get(), Retry.Target, Retry.Hops, Retry.Why);
	}
}

void ATN_BeachStorm::PlaceKicked(ATortugaCharacter* Turtle, const FTransform& Target, const TCHAR* Why)
{
	if (!IsValid(Turtle))
	{
		return;
	}
	// Último recurso: teletransporte limpio (fuera de la bola, del aturdimiento y de lo que la tuviera), gracia y polvo donde
	// aparece.
	TNBeachStormTuning::EndPassThrough(Turtle);
	TNBeach::ReleaseTurtle(Turtle, TNBeach::ETNBeachMover::StormKick);
	TNBeach::RelocateTurtle(Turtle, Target);
	TNBeach::GrantStormGrace(Turtle, KickGraceSeconds);
	MulticastKickLand(Target.GetLocation());
	const FVector At = Target.GetLocation();
	UE_LOG(LogTortunabo, Log, TEXT("[Playa] Tormenta: %s a su sitio en (%.1f, %.1f, %.1f) m (%s); %.0f s sin patadas."), *GetNameSafe(Turtle),
		At.X / 100.0, At.Y / 100.0, At.Z / 100.0, Why ? Why : TEXT("patada"), KickGraceSeconds);
}

void ATN_BeachStorm::FinishKick(ATortugaCharacter* Turtle)
{
	if (!Turtle)
	{
		return;
	}
	TNBeachStormTuning::EndPassThrough(Turtle);
	TNBeach::ReleaseTurtle(Turtle, TNBeach::ETNBeachMover::StormKick);
	TNBeach::GrantStormGrace(Turtle, KickGraceSeconds);
}

void ATN_BeachStorm::ReleaseAllFlights()
{
	for (const TPair<TWeakObjectPtr<ATortugaCharacter>, FKickFlight>& Pair : Flights)
	{
		if (ATortugaCharacter* Turtle = Pair.Key.Get())
		{
			// Una bola que atravesaba vuelve a chocar: sin nadie que la vigile, no puede seguir cayendo a través de la arena.
			TNBeachStormTuning::EndPassThrough(Turtle);
			TNBeach::ReleaseTurtle(Turtle, TNBeach::ETNBeachMover::StormKick);
		}
	}
	Flights.Reset();
}

bool ATN_BeachStorm::FindSpotAhead(const ATortugaCharacter* Turtle, float Lead, FTransform& OutSpot)
{
	if (!Turtle || !bActive)
	{
		return false;
	}
	const FVector Base = PointAhead(Turtle->GetActorLocation(), KickAhead + GetFrontSpeed() * FMath::Max(0.f, Lead));
	if (GetActorTransform().InverseTransformPositionNoScale(Base).X < GetFrontDistance() + KickSlack)
	{
		return false;
	}
	return TNBeach::FindOpenSandSpot(Turtle, Base, TNBeachStormTuning::KickSearchRadius, OutSpot);
}

bool ATN_BeachStorm::IsKicking(const ATortugaCharacter* Turtle) const
{
	return Turtle && Flights.Contains(TWeakObjectPtr<ATortugaCharacter>(const_cast<ATortugaCharacter*>(Turtle)));
}

void ATN_BeachStorm::MulticastKickLand_Implementation(FVector_NetQuantize10 At)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (!bFXReady)
	{
		SetupFX();
	}
	TNBeachKit::BurstAt(FrontDust, FVector(At) + FVector(0.0, 0.0, 60.0), FVector::UpVector, 10);
	UTN_BeachCameraShake::Kick(this, At, 0.45f, 300.f, 2500.f);
	if (Voice)
	{
		Voice->SetWorldLocation(At);
		Voice->Play(ETNBeachSfx::Stomp, 0.9f, 1.4f);
	}
}

void ATN_BeachStorm::MulticastKick_Implementation(ATortugaCharacter* Victim, FVector_NetQuantize10 KickAt)
{
	if (!Victim || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// Donde estaba al patearla (en un salto, cuando llega esto ya puede estar en su sitio).
	const FVector At = KickAt;
	if (!bFXReady)
	{
		SetupFX();
	}
	// La pierna de un bañista que barre por detrás de la tortuga (se ve un momento y se funde).
	if (KickLegs.Num() > 0)
	{
		FKickLeg& Leg = KickLegs[NextKick];
		NextKick = (NextKick + 1) % KickLegs.Num();
		const FTransform Xf = GetActorTransform();
		const double Ground = GroundAt(At);
		Leg.Hip = FVector(At.X, At.Y, Ground + TNBeachStormTuning::HipHeight * TNBeach::Scale);
		Leg.Yaw = static_cast<float>(Xf.Rotator().Yaw);
		Leg.Age = 0.f;
	}
	if (Voice)
	{
		Voice->SetWorldLocation(At);
		Voice->Play(ETNBeachSfx::Stomp, 1.1f, 1.2f);
		Voice->Play(ETNBeachSfx::Slam, 1.4f, 1.f);
	}
	TNBeachKit::BurstAt(FrontDust, At + FVector(0.0, 0.0, 100.0), FVector::UpVector, 6);
	UTN_BeachCameraShake::Kick(this, At, 0.7f, 300.f, 2500.f);
	if (ATN_BeachEnemy::LocalViewDistance(this, At) < 6000.f)
	{
		HitPop.Show(this, NSLOCTEXT("TNBeach", "StormKick", "¡PATADA!"), FColor(255, 150, 70), At + FVector(0.0, 0.0, 280.0), 160.f);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Efectos
// ─────────────────────────────────────────────────────────────────────────────

float ATN_BeachStorm::Rand01()
{
	FxRng ^= FxRng << 13;
	FxRng ^= FxRng >> 17;
	FxRng ^= FxRng << 5;
	return static_cast<float>(FxRng & 0xFFFFFF) / 16777215.f;
}

void ATN_BeachStorm::ApplyFade(UStaticMeshComponent* Comp, UStaticMesh* Solid, UStaticMesh* Soft, TObjectPtr<UMaterialInstanceDynamic>& Mid, float Fade)
{
	if (!Comp)
	{
		return;
	}
	const bool bShow = Fade > 0.01f;
	if (Comp->IsVisible() != bShow)
	{
		Comp->SetVisibility(bShow);
	}
	if (!bShow)
	{
		return;
	}
	if (Fade >= 0.999f || !Soft)
	{
		// Entera: la de siempre, opaca y con su luz.
		if (Comp->GetStaticMesh() != Solid)
		{
			Comp->SetStaticMesh(Solid);
			Comp->SetMaterial(0, nullptr);
		}
		return;
	}
	if (Comp->GetStaticMesh() != Soft)
	{
		Comp->SetStaticMesh(Soft);
		if (!Mid)
		{
			Mid = UMaterialInstanceDynamic::Create(TNBeachKit::SoftMaterial(), this);
		}
		Comp->SetMaterial(0, Mid);
	}
	TNBeachKit::SetOpacity(Mid, Fade);
}

void ATN_BeachStorm::SetupFX()
{
	bFXReady = true;
	using TNProcMesh::FTNProcMeshBuffers;
	using TNBeachMeshes::EStormItem;

	// Velo del frente: las láminas de la tormenta del camino, color arena, tan anchas como la playa y apoyadas en ella. Su
	// «Opacity» sigue a FrontBlend: se funde al acercarse o alejarse el frente de la cámara.
	FTNProcMeshBuffers VeilBuffers;
	TNStormFX::BuildVeil(VeilBuffers, TNBeachStormTuning::SandVeil(), HalfWidth * 2.0 + 8000.0, TNBeachStormTuning::VeilHeight, 3, 23u);
	UMaterialInterface* VeilMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_ProcStormVeil.M_ProcStormVeil"), nullptr, LOAD_NoWarn);
	if (!VeilMat)
	{
		VeilMat = TNBeachKit::SoftMaterial();
	}
	UStaticMesh* VeilMesh = TNProcRuntimeMesh::MakeStaticMesh(this, VeilBuffers, VeilMat, false, 0.f, 1.f, -2.f);
	Veil = TNBeachKit::AddPart(this, Root, VeilMesh, FVector::ZeroVector, false);
	if (Veil)
	{
		Veil->SetAbsolute(true, true, true);
		Veil->SetVisibility(false);
		VeilMid = Veil->CreateDynamicMaterialInstance(0, VeilMat);
		TNBeachKit::SetOpacity(VeilMid, 0.f);
	}

	// Trastos que vuelan: sombrillas, cubos, sillas, toallas, flotadores, palas, chanclas y pelotas (opacos y, para
	// fundirse, su copia translúcida).
	const int32 NumItems = static_cast<int32>(EStormItem::Count);
	for (int32 i = 0; i < TNBeachStormTuning::NumDebris; ++i)
	{
		FDebris& D = Debris.AddDefaulted_GetRef();
		D.Item = i % NumItems;
		const int32 Variant = (i * 3 + 1) % 4;
		const EStormItem Item = static_cast<EStormItem>(D.Item);
		const FString Key = FString::Printf(TEXT("Beach.Storm.%d.%d"), D.Item, Variant);
		UStaticMesh* Mesh = TNBeachKit::CachedMesh(Key, [Item, Variant](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildStormItem(M, Item, Variant); });
		UStaticMesh* SoftMesh = TNBeachKit::CachedMesh(Key + TEXT(".Soft"), [Item, Variant](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildStormItem(M, Item, Variant); },
			TNBeachKit::EBeachMeshMat::SoftVertexAlpha);
		UStaticMeshComponent* Comp = TNBeachKit::AddPart(this, Root, Mesh, FVector::ZeroVector, TNBeachStormTuning::IsBigItem(Item));
		if (Comp)
		{
			Comp->SetAbsolute(true, true, true);
			Comp->SetVisibility(false);
		}
		DebrisComps.Add(Comp);
		DebrisSolid.Add(Mesh);
		DebrisSoft.Add(SoftMesh);
		DebrisMids.Add(nullptr);
	}

	// Bañistas: caderas y dos piernas cada uno, repartidos a lo ancho y pisando en el borde del frente.
	const double S = TNBeach::Scale;
	const double HipZ = TNBeachStormTuning::HipHeight * S;
	for (int32 b = 0; b < TNBeachStormTuning::NumBathers; ++b)
	{
		FBather& B = Bathers.AddDefaulted_GetRef();
		const float Spread = static_cast<float>(b) / static_cast<float>(FMath::Max(1, TNBeachStormTuning::NumBathers - 1));
		B.SlotY = FMath::Lerp(-0.85f, 0.85f, Spread) * HalfWidth + 1500.f * (TNBeachKit::Hash01(b * 13u + 1u) - 0.5f);
		// Los pies justo dentro del borde del polvo (2,5-11,5 m tras el frente): se ven pisar en el borde.
		B.Depth = 250.f + 900.f * TNBeachKit::Hash01(b * 29u + 7u);
		B.Rate = 0.45f + 0.15f * TNBeachKit::Hash01(b * 31u + 3u);
		B.Phase = TNBeachKit::Hash01(b * 37u + 11u);
		B.Scale = 0.9f + 0.2f * TNBeachKit::Hash01(b * 41u + 5u);
		B.GroundTimer = 0.03f * static_cast<float>(b);
		const TNBeachMeshes::FBatherLook Look = TNBeachMeshes::BatherPalette(b);
		const FString HipsKey = FString::Printf(TEXT("Beach.Bather.%d.Hips"), b % 8);
		const FString LegKey = FString::Printf(TEXT("Beach.Bather.%d.Leg"), b % 8);
		UStaticMesh* HipsMesh = TNBeachKit::CachedMesh(HipsKey, [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildBatherHips(M, Look); });
		UStaticMesh* LegMesh = TNBeachKit::CachedMesh(LegKey, [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildBatherLeg(M, Look); });
		UStaticMesh* HipsSoft = TNBeachKit::CachedMesh(HipsKey + TEXT(".Soft"), [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildBatherHips(M, Look); },
			TNBeachKit::EBeachMeshMat::SoftVertexAlpha);
		UStaticMesh* LegSoft = TNBeachKit::CachedMesh(LegKey + TEXT(".Soft"), [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildBatherLeg(M, Look); },
			TNBeachKit::EBeachMeshMat::SoftVertexAlpha);
		USceneComponent* BatherRoot = NewObject<USceneComponent>(this, NAME_None, RF_Transient);
		BatherRoot->SetupAttachment(Root);
		BatherRoot->SetAbsolute(true, true, true);
		BatherRoot->RegisterComponent();
		BatherRoots.Add(BatherRoot);
		BatherParts.Add(TNBeachKit::AddPart(this, BatherRoot, HipsMesh, FVector(0.0, 0.0, HipZ), true));
		BatherParts.Add(TNBeachKit::AddPart(this, BatherRoot, LegMesh, FVector(0.0, -TNBeachStormTuning::HipHalfWidth * S, HipZ), true));
		BatherParts.Add(TNBeachKit::AddPart(this, BatherRoot, LegMesh, FVector(0.0, TNBeachStormTuning::HipHalfWidth * S, HipZ), true));
		BatherSolid.Add(HipsMesh);
		BatherSolid.Add(LegMesh);
		BatherSolid.Add(LegMesh);
		BatherSoft.Add(HipsSoft);
		BatherSoft.Add(LegSoft);
		BatherSoft.Add(LegSoft);
		for (int32 k = 0; k < 3; ++k)
		{
			BatherMids.Add(nullptr);
			if (UStaticMeshComponent* Part = BatherParts[BatherParts.Num() - 3 + k])
			{
				Part->SetVisibility(false);
			}
		}
	}

	// Piernas de las patadas (una pierna de bañista suelta, con su copia translúcida para fundirse).
	{
		const TNBeachMeshes::FBatherLook Look = TNBeachMeshes::BatherPalette(3);
		KickSolid = TNBeachKit::CachedMesh(TEXT("Beach.Bather.3.Leg"), [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildBatherLeg(M, Look); });
		KickSoft = TNBeachKit::CachedMesh(TEXT("Beach.Bather.3.Leg.Soft"), [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildBatherLeg(M, Look); },
			TNBeachKit::EBeachMeshMat::SoftVertexAlpha);
		for (int32 k = 0; k < TNBeachStormTuning::NumKickLegs; ++k)
		{
			UStaticMeshComponent* Comp = TNBeachKit::AddPart(this, Root, KickSolid, FVector::ZeroVector, true);
			if (Comp)
			{
				Comp->SetAbsolute(true, true, true);
				Comp->SetVisibility(false);
			}
			KickComps.Add(Comp);
			KickMids.Add(nullptr);
			KickLegs.AddDefaulted();
		}
	}

	// Arena y polvo en el frente y, dentro (o a punto), alrededor de la cámara.
	using TNAmbientFX::EShape;
	auto MakeEmitter = [this](TNAmbientFX::FEmitter& E, EShape Shape, const FLinearColor& Color, bool bSoft, float Alpha, int32 MaxCount, float BaseRate,
		float EmitSpeed, float Fall, float Rise, float LifeMin, float LifeMax, float SizeStart, float SizeEnd, float Radius, float Height, uint32 EmitterSeed)
	{
		TNAmbientFX::FEmitterDesc D = TNBeachKit::MakeDesc(Shape, Color, bSoft, Alpha, MaxCount, BaseRate, EmitSpeed, Fall, LifeMin, LifeMax, SizeStart, SizeEnd);
		D.Buoyancy = Rise;
		D.SpawnRadius = Radius;
		D.SpawnHeight = Height;
		D.Spread = 0.55f;
		D.Drag = 0.35f;
		D.Direction = FVector::ForwardVector;
		D.WakeDistance = 1.0e7f;
		TNBeachKit::InitEmitter(E, this, D, EmitterSeed);
	};
	MakeEmitter(FrontSand, EShape::Streak, FLinearColor(0.86f, 0.7f, 0.46f), true, 0.8f, 200, 160.f, 2400.f, -60.f, 0.f, 0.6f, 1.3f, 60.f, 40.f, 3000.f, 1400.f, 61u);
	MakeEmitter(FrontDust, EShape::Puff, FLinearColor(0.8f, 0.68f, 0.5f), true, 0.45f, 70, 26.f, 700.f, 0.f, 30.f, 2.f, 4.f, 400.f, 900.f, 3200.f, 1600.f, 62u);
	MakeEmitter(FrontClouds, EShape::Puff, FLinearColor(0.72f, 0.6f, 0.44f), true, 0.7f, 70, 22.f, 420.f, 0.f, 35.f, 3.f, 5.5f, 700.f, 1500.f, 3500.f, 700.f, 63u);
	MakeEmitter(ViewSand, EShape::Streak, FLinearColor(0.86f, 0.72f, 0.5f), true, 0.8f, 160, 140.f, 2000.f, -60.f, 0.f, 0.5f, 1.1f, 55.f, 40.f, 1500.f, 900.f, 64u);
	MakeEmitter(ViewDust, EShape::Puff, FLinearColor(0.8f, 0.68f, 0.5f), true, 0.4f, 50, 18.f, 500.f, 0.f, 20.f, 2.f, 3.5f, 300.f, 700.f, 1500.f, 900.f, 65u);

	Voice = UTN_BeachEnemySynthComponent::AttachTo(this, Root, 6000.f, 22000.f);

	// Niebla del nivel (la primera); si no hay, una propia apagada.
	for (TActorIterator<AExponentialHeightFog> It(GetWorld()); It; ++It)
	{
		if (UExponentialHeightFogComponent* C = It->GetComponent())
		{
			Fog = C;
			break;
		}
	}
	if (!Fog.IsValid())
	{
		UExponentialHeightFogComponent* Own = NewObject<UExponentialHeightFogComponent>(this, NAME_None, RF_Transient);
		Own->SetupAttachment(Root);
		Own->SetFogDensity(0.f);
		Own->RegisterComponent();
		Fog = Own;
	}
}

void ATN_BeachStorm::TickFX(float DeltaSeconds)
{
	if (!bFXReady)
	{
		SetupFX();
	}
	FXTime += DeltaSeconds;
	UWorld* World = GetWorld();
	const FTransform Xf = GetActorTransform();
	FVector View = GetActorLocation();
	FVector ViewFwd = Xf.GetUnitAxis(EAxis::X);
	// El jugador local más atrás (el que tiene la tormenta más encima); sin pantalla partida, el de siempre.
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	{
		TArray<APlayerController*> LocalControllers;
		TNLocalViews::GetLocalControllers(World, LocalControllers);
		double Behind = TNumericLimits<double>::Max();
		for (const APlayerController* Candidate : LocalControllers)
		{
			const APawn* CandidatePawn = Candidate->GetPawn();
			const double X = CandidatePawn ? Xf.InverseTransformPositionNoScale(CandidatePawn->GetActorLocation()).X : TNumericLimits<double>::Max() * 0.5;
			if (X < Behind) { Behind = X; PC = Candidate; }
		}
	}
	if (PC && PC->PlayerCameraManager)
	{
		View = PC->PlayerCameraManager->GetCameraLocation();
		ViewFwd = PC->PlayerCameraManager->GetCameraRotation().Vector();
	}
	const APawn* LocalPawn = PC ? PC->GetPawn() : nullptr;
	const FVector LocalAt = LocalPawn ? LocalPawn->GetActorLocation() : View;
	const FVector ViewLocal = Xf.InverseTransformPositionNoScale(View);

	const float Front = GetFrontDistance();
	const bool bNearFront = bShown && FMath::Abs(static_cast<float>(ViewLocal.X) - Front) < TNBeachStormTuning::VisibleRange;
	FrontBlend = FMath::FInterpConstantTo(FrontBlend, bNearFront ? 1.f : 0.f, DeltaSeconds, 0.5f);
	const bool bLocalInside = IsLocationInside(LocalAt);
	InsideBlend = FMath::FInterpConstantTo(InsideBlend, bLocalInside ? 1.f : 0.f, DeltaSeconds, 0.7f);
	const float Ahead = static_cast<float>(Xf.InverseTransformPositionNoScale(LocalAt).X) - Front;
	const bool bWarn = bActive && LocalPawn && !bLocalInside && Ahead > -InsideMargin && Ahead < WarnDistance;
	WarnBlend = FMath::FInterpConstantTo(WarnBlend, bWarn ? 1.f - FMath::Max(0.f, Ahead) / FMath::Max(1.f, WarnDistance) : 0.f, DeltaSeconds, 1.5f);

	// Arena bajo el frente donde está la cámara (la playa baja hacia el mar), cuatro veces por segundo.
	const float NearY = FMath::Clamp(static_cast<float>(ViewLocal.Y), -HalfWidth, HalfWidth);
	FVector FrontPoint = Xf.TransformPositionNoScale(FVector(Front, NearY, 0.0));
	GroundTimer -= DeltaSeconds;
	if ((GroundTimer <= 0.f || !bFrontGroundValid) && bShown)
	{
		GroundTimer = 0.25f;
		FrontGroundZ = GroundAt(FrontPoint);
		bFrontGroundValid = true;
	}
	FrontPoint.Z = FrontGroundZ;

	// Velo: se funde (opacidad) y crece desde la arena (escala) con FrontBlend; nada de aparecer de golpe.
	if (Veil)
	{
		const bool bShowVeil = FrontBlend > 0.01f;
		if (Veil->IsVisible() != bShowVeil)
		{
			Veil->SetVisibility(bShowVeil);
		}
		if (bShowVeil)
		{
			const float Grow = TNBeachStormTuning::Smooth01(FrontBlend);
			FVector At = Xf.TransformPositionNoScale(FVector(Front + 180.f * FMath::Sin(FXTime * 0.55f), 0.0, 0.0));
			At.Z = FrontGroundZ + 60.f * FMath::Sin(FXTime * 0.31f);
			Veil->SetWorldTransform(FTransform(FRotator(0.f, static_cast<float>(Xf.Rotator().Yaw), 0.f), At, FVector(1.0, 1.0, 0.25 + 0.75 * Grow)));
			TNBeachKit::SetOpacity(VeilMid, FrontBlend);
		}
	}

	// Partículas: en el frente donde mira la cámara (a ras de arena) y, dentro o a punto, alrededor de ella.
	const FVector Wind = Xf.TransformVectorNoScale(FVector(1.0, 0.0, 0.12)).GetSafeNormal();
	const FVector Back = Xf.GetUnitAxis(EAxis::X);
	const FVector Flat = FVector(ViewFwd.X, ViewFwd.Y, 0.0).GetSafeNormal();
	auto Run = [&](TNAmbientFX::FEmitter& E, const FVector& Origin, float Scale)
	{
		E.Origin = Origin;
		E.Desc.Direction = Wind;
		E.RateScale = Scale;
		TNBeachKit::TickEmitterIfBusy(E, DeltaSeconds, View);
	};
	Run(FrontSand, FrontPoint - Back * 600.0 + FVector(0.0, 0.0, 200.0), FrontBlend);
	Run(FrontDust, FrontPoint - Back * 600.0 + FVector(0.0, 0.0, 300.0), FrontBlend);
	Run(FrontClouds, FrontPoint - Back * 900.0 + FVector(0.0, 0.0, 400.0), FrontBlend);
	const float Around = FMath::Max(InsideBlend, 0.45f * WarnBlend);
	Run(ViewSand, View + Flat * 700.0 - FVector(0.0, 0.0, 350.0), Around);
	Run(ViewDust, View + Flat * 700.0 - FVector(0.0, 0.0, 350.0), InsideBlend);

	// Trastos, bañistas y patadas (se funden solos; lejos del frente no nace ninguno).
	TickDebris(DeltaSeconds, Front, ViewLocal);
	TickBathers(DeltaSeconds, Front, ViewLocal);
	TickKicks(DeltaSeconds);

	// El ruido de la tormenta no lo hace este actor: es el paisaje sonoro de siempre (viento, silbido y truenos, el mismo del
	// cooperativo), que se sube al acercarse el frente y dentro (UTN_AmbientSoundscapeComponent::UpdateMix). Aquí no suena nada
	// continuo ni rítmico; solo el golpe de la patada. Aviso claro si el frente te pisa los talones; temblor dentro.
	if (bActive && LocalPawn)
	{
		if (bLocalInside)
		{
			UTN_BeachCameraShake::Rumble(this, LocalAt, 0.3f, 100.f, 1000.f);
			if (!bWarnedInside)
			{
				bWarnedInside = true;
				WarnPop.Show(this, NSLOCTEXT("TNBeach", "StormRun", "¡CORRE!"), FColor(255, 120, 60), LocalAt + Flat * 400.0 + FVector(0.0, 0.0, 240.0), 150.f);
			}
		}
		else
		{
			bWarnedInside = false;
		}
		if (bWarn)
		{
			UTN_BeachCameraShake::Rumble(this, LocalAt, 0.12f + 0.3f * WarnBlend, 100.f, 1000.f);
			if (!bWarned)
			{
				bWarned = true;
				WarnPop.Show(this, NSLOCTEXT("TNBeach", "StormWarn", "¡QUE VIENE LA TORMENTA!"), FColor(240, 180, 90), LocalAt + Flat * 450.0 + FVector(0.0, 0.0, 260.0), 110.f);
			}
		}
		else if (Ahead > WarnDistance * 1.6f)
		{
			// Se rearma cuando se ha alejado bien (no a cada paso por el borde).
			bWarned = false;
		}
	}
	WarnPop.Tick(DeltaSeconds, World);
	HitPop.Tick(DeltaSeconds, World);
	ApplyInsideLook();
}

void ATN_BeachStorm::SpawnDebris(FDebris& D, float Front, const FVector& ViewLocal)
{
	using TNBeachMeshes::EStormItem;
	const FTransform Xf = GetActorTransform();
	const EStormItem Item = static_cast<EStormItem>(D.Item);
	const bool bBig = TNBeachStormTuning::IsBigItem(Item);
	// Nace en el polvo del borde (1-7 m tras el frente) y se queda por el borde: su sitio respecto al frente, de 2 m por
	// detrás a 5 m por delante (los grandes, algo más fuera, que se vean).
	D.EdgeOffset = bBig ? FMath::Lerp(-100.f, 500.f, Rand01()) : FMath::Lerp(-200.f, 350.f, Rand01());
	const FVector Local(Front - FMath::Lerp(100.f, 700.f, Rand01()),
		FMath::Clamp(static_cast<float>(ViewLocal.Y) + FMath::Lerp(-9000.f, 9000.f, Rand01()), -HalfWidth, HalfWidth), 0.0);
	D.Pos = Xf.TransformPositionNoScale(Local);
	D.Ground = GroundAt(D.Pos);
	D.GroundTimer = 0.25f * Rand01();
	// A la altura de verdad: lo pequeño rueda y rebota a pocos metros; lo grande planea más alto (una sombrilla mide 50 m).
	D.Pos.Z = D.Ground + (bBig ? FMath::Lerp(500.f, 2200.f, Rand01()) : FMath::Lerp(120.f, 600.f, Rand01()));
	// Con el frente (su velocidad y un poco más) y, sobre todo, de lado: barre el borde de la tormenta.
	D.Vel = Xf.TransformVectorNoScale(FVector(GetFrontSpeed() + FMath::Lerp(50.f, 350.f, Rand01()), FMath::Lerp(-650.f, 650.f, Rand01()), 0.0))
		+ FVector(0.0, 0.0, FMath::Lerp(100.f, bBig ? 350.f : 650.f, Rand01()));
	D.SpinAxis = FVector(Rand01() - 0.5f, Rand01() - 0.5f, Rand01() - 0.5f).GetSafeNormal();
	if (D.SpinAxis.IsNearlyZero())
	{
		D.SpinAxis = FVector::UpVector;
	}
	D.SpinRate = bBig ? FMath::Lerp(40.f, 120.f, Rand01()) : FMath::Lerp(120.f, 320.f, Rand01());
	D.Rot = FQuat(FVector(Rand01() - 0.5f, Rand01() - 0.5f, Rand01() + 0.1f).GetSafeNormal(), Rand01() * 2.f * PI);
	D.Scale = bBig ? FMath::Lerp(0.6f, 0.85f, Rand01()) : FMath::Lerp(0.9f, 1.15f, Rand01());
	// Lo grande hace de vela: el viento lo sostiene (cae mucho menos).
	D.Lift = bBig ? 0.75f : 0.f;
	D.Fade = 0.f;
	D.Age = 0.f;
	D.Life = FMath::Lerp(3.5f, 7.f, Rand01());
	D.bDying = false;
	D.bLive = true;
}

void ATN_BeachStorm::TickDebris(float DeltaSeconds, float Front, const FVector& ViewLocal)
{
	const FTransform Xf = GetActorTransform();
	const FVector Fwd = Xf.GetUnitAxis(EAxis::X);
	const FVector Side = Xf.GetUnitAxis(EAxis::Y);
	const double FrontV = GetFrontSpeed();
	for (int32 i = 0; i < Debris.Num(); ++i)
	{
		FDebris& D = Debris[i];
		UStaticMeshComponent* Comp = DebrisComps.IsValidIndex(i) ? DebrisComps[i].Get() : nullptr;
		if (!Comp || !DebrisSolid.IsValidIndex(i) || !DebrisSoft.IsValidIndex(i) || !DebrisMids.IsValidIndex(i))
		{
			continue;
		}
		if (!D.bLive)
		{
			// Lejos del frente no nace ninguno (los que había ya se han fundido).
			if (FrontBlend <= 0.01f)
			{
				ApplyFade(Comp, DebrisSolid[i], DebrisSoft[i], DebrisMids[i], 0.f);
				continue;
			}
			SpawnDebris(D, Front, ViewLocal);
		}
		// Por el borde: el viento lo lleva a la velocidad del frente y un muelle lo devuelve a su sitio respecto a él (ni se
		// adelanta ni se queda atrás); de lado se frena poco a poco; cae (lo grande, planeando), rebota y rueda por la arena.
		FVector Local = Xf.InverseTransformPositionNoScale(D.Pos);
		const double Along = FVector::DotProduct(D.Vel, Fwd);
		const double Across = FVector::DotProduct(D.Vel, Side);
		const double WantX = static_cast<double>(Front + D.EdgeOffset);
		D.Vel += Fwd * (((FrontV - Along) * 1.2 + (WantX - Local.X) * 0.9) * DeltaSeconds);
		D.Vel -= Side * (Across * 0.25 * DeltaSeconds);
		D.Vel.Z -= 700.0 * (1.0 - D.Lift) * DeltaSeconds;
		D.Pos += D.Vel * DeltaSeconds;
		D.Rot = (FQuat(D.SpinAxis, FMath::DegreesToRadians(D.SpinRate) * DeltaSeconds) * D.Rot).GetNormalized();
		D.GroundTimer -= DeltaSeconds;
		if (D.GroundTimer <= 0.f)
		{
			D.GroundTimer = 0.25f;
			D.Ground = GroundAt(D.Pos);
		}
		const double Floor = D.Ground + 120.0 * D.Scale;
		if (D.Pos.Z < Floor)
		{
			D.Pos.Z = Floor;
			D.Vel.Z = FMath::Abs(D.Vel.Z) * 0.5 + 200.0;
			D.SpinRate *= 0.85f;
		}
		// Se funde al acabar su vida, al salirse del borde o de la vista, o si el frente se aleja de la cámara.
		Local = Xf.InverseTransformPositionNoScale(D.Pos);
		D.Age += DeltaSeconds;
		if (!D.bDying && (D.Age > D.Life || FrontBlend <= 0.01f || Local.X > Front + 1200.0 || Local.X < Front - 1800.0
			|| FMath::Abs(Local.Y - ViewLocal.Y) > 14000.0 || FMath::Abs(Local.Y) > HalfWidth + 3000.0 || D.Pos.Z > D.Ground + 4000.0))
		{
			D.bDying = true;
		}
		D.Fade = D.bDying ? FMath::Max(0.f, D.Fade - DeltaSeconds / TNBeachStormTuning::DebrisFadeOut)
			: FMath::Min(1.f, D.Fade + DeltaSeconds / TNBeachStormTuning::DebrisFadeIn);
		if (D.bDying && D.Fade <= 0.f)
		{
			D.bLive = false;
			ApplyFade(Comp, DebrisSolid[i], DebrisSoft[i], DebrisMids[i], 0.f);
			continue;
		}
		// Crece al nacer y encoge al irse, además de la opacidad.
		const float Grow = 0.45f + 0.55f * TNBeachStormTuning::Smooth01(D.Fade);
		Comp->SetWorldTransform(FTransform(D.Rot, D.Pos, FVector(D.Scale * Grow)));
		ApplyFade(Comp, DebrisSolid[i], DebrisSoft[i], DebrisMids[i], D.Fade * FrontBlend);
	}
}

void ATN_BeachStorm::TickBathers(float DeltaSeconds, float Front, const FVector& ViewLocal)
{
	const FTransform Xf = GetActorTransform();
	const float Yaw = static_cast<float>(Xf.Rotator().Yaw);
	const double S = TNBeach::Scale;
	const double HipZ = TNBeachStormTuning::HipHeight * S;
	for (int32 b = 0; b < Bathers.Num(); ++b)
	{
		FBather& B = Bathers[b];
		USceneComponent* BatherRoot = BatherRoots.IsValidIndex(b) ? BatherRoots[b].Get() : nullptr;
		const int32 First = b * 3;
		if (!BatherRoot || !BatherParts.IsValidIndex(First + 2) || !BatherSolid.IsValidIndex(First + 2) || !BatherMids.IsValidIndex(First + 2))
		{
			continue;
		}
		const FVector Local(Front - B.Depth, B.SlotY, 0.0);
		// Entra y sale fundiéndose (con el frente a la vista y a lo ancho cerca de la cámara).
		const bool bWanted = FrontBlend > 0.01f && FMath::Abs(Local.Y - ViewLocal.Y) < 30000.0;
		B.Fade = FMath::FInterpConstantTo(B.Fade, bWanted ? 1.f : 0.f, DeltaSeconds, 1.f / TNBeachStormTuning::BatherFade);
		const float Shown = B.Fade * FrontBlend;
		for (int32 k = 0; k < 3; ++k)
		{
			ApplyFade(BatherParts[First + k], BatherSolid[First + k], BatherSoft[First + k], BatherMids[First + k], Shown);
		}
		if (Shown <= 0.01f)
		{
			continue;
		}
		FVector At = Xf.TransformPositionNoScale(Local);
		B.GroundTimer -= DeltaSeconds;
		if (B.GroundTimer <= 0.f || B.Ground == 0.f)
		{
			B.GroundTimer = 0.25f;
			B.Ground = GroundAt(At);
		}
		At.Z = B.Ground;
		// Andar pesado: las piernas se balancean a contratiempo y la cadera sube y baja; parada, quieta.
		const float Cycle = bActive ? FXTime * B.Rate + B.Phase : B.Phase;
		const float Swing = FMath::Sin(Cycle * 2.f * PI);
		const float Bob = FMath::Abs(FMath::Cos(Cycle * 2.f * PI)) * 0.04f;
		BatherRoot->SetWorldTransform(FTransform(FRotator(0.f, Yaw, 3.f * Swing), At, FVector(B.Scale * (0.85f + 0.15f * Shown))));
		const FVector Hip(0.0, 0.0, HipZ * (1.0 + Bob));
		TNBeachKit::Pose(BatherParts[First], Hip, FRotator(0.f, 4.f * Swing, 0.f));
		TNBeachKit::Pose(BatherParts[First + 1], Hip + FVector(0.0, -TNBeachStormTuning::HipHalfWidth * S, 0.0), FRotator(24.f * Swing, 0.f, 0.f));
		TNBeachKit::Pose(BatherParts[First + 2], Hip + FVector(0.0, TNBeachStormTuning::HipHalfWidth * S, 0.0), FRotator(-24.f * Swing, 0.f, 0.f));
		// Un pisotón en cada paso (dos por ciclo), justo en el borde: solo se ve (polvo). Sin sonido ni sacudida por paso: el
		// «pum, pum, pum» constante era lo más molesto de la tormenta (el ruido es el del paisaje sonoro, como en el cooperativo).
		const float Step = FMath::FloorToFloat(Cycle * 2.f);
		if (bActive && Step != B.LastStep)
		{
			B.LastStep = Step;
			const double DistSq = FVector::DistSquared2D(Local, ViewLocal);
			if (DistSq < FMath::Square(20000.0) && Shown > 0.5f)
			{
				const float FootSide = FMath::Fmod(Step, 2.f) < 0.5f ? -1.f : 1.f;
				const FVector Foot = Xf.TransformPositionNoScale(Local + FVector(300.0, FootSide * TNBeachStormTuning::HipHalfWidth * S, 0.0));
				TNBeachKit::BurstAt(FrontDust, FVector(Foot.X, Foot.Y, B.Ground + 200.0), FVector::UpVector, 3);
			}
		}
	}
}

void ATN_BeachStorm::TickKicks(float DeltaSeconds)
{
	using namespace TNBeachStormTuning;
	for (int32 k = 0; k < KickLegs.Num(); ++k)
	{
		FKickLeg& Leg = KickLegs[k];
		UStaticMeshComponent* Comp = KickComps.IsValidIndex(k) ? KickComps[k].Get() : nullptr;
		if (!Comp || !KickMids.IsValidIndex(k))
		{
			continue;
		}
		const float Total = KickSwing + KickHold + KickFadeOut;
		if (Leg.Age >= Total)
		{
			ApplyFade(Comp, KickSolid, KickSoft, KickMids[k], 0.f);
			continue;
		}
		Leg.Age += DeltaSeconds;
		// Barre de atrás (dentro de la tormenta) hacia delante pasando por la tortuga, se queda arriba y se va fundiendo.
		const float Out = FMath::Clamp((Leg.Age - KickSwing - KickHold) / KickFadeOut, 0.f, 1.f);
		const float Pitch = Leg.Age < KickSwing ? FMath::Lerp(-25.f, 50.f, Smooth01(Leg.Age / KickSwing)) : 50.f + 8.f * Out;
		const float Fade = FMath::Min(1.f, Leg.Age / 0.08f) * (1.f - Out);
		const FVector Hip = Leg.Hip + FVector(0.0, 0.0, 600.0 * Out);
		Comp->SetWorldTransform(FTransform(FRotator(Pitch, Leg.Yaw, 0.f), Hip, FVector(1.0)));
		ApplyFade(Comp, KickSolid, KickSoft, KickMids[k], Fade);
	}
}

void ATN_BeachStorm::TickCough(float DeltaSeconds)
{
	// Tos de las tortugas: cada máquina con audio decide quién está dentro con el frente replicado, sin RPC.
	if (!FApp::CanEverRenderAudio())
	{
		return;
	}
	CoughTimer += DeltaSeconds;
	if (CoughTimer < 0.1f)
	{
		return;
	}
	const float Step = CoughTimer;
	CoughTimer = 0.f;
	TArray<ATortugaCharacter*> Turtles;
	ATN_BeachEnemy::GatherTurtles(this, Turtles);
	for (ATortugaCharacter* Turtle : Turtles)
	{
		UTN_StormCoughComponent* Cough = bActive ? UTN_StormCoughComponent::FindOrAddTo(Turtle) : Turtle->FindComponentByClass<UTN_StormCoughComponent>();
		if (!Cough)
		{
			continue;
		}
		const bool bInside = IsLocationInside(Turtle->GetActorLocation()) && !ATN_BeachShelterVolume::IsSheltered(Turtle);
		float& Seconds = CoughInside.FindOrAdd(Turtle);
		Seconds = bInside ? Seconds + Step : 0.f;
		Cough->SetStormExposure(bInside, Seconds / TNBeachStormTuning::WorstCoughSeconds);
	}
	for (auto It = CoughInside.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

void ATN_BeachStorm::ApplyInsideLook()
{
	const float B = InsideBlend;
	// Niebla de arena: se cierra dentro y vuelve exactamente a su estado al salir.
	if (UExponentialHeightFogComponent* F = Fog.Get())
	{
		if (!bFogCached)
		{
			bFogCached = true;
			FogDensity0 = F->FogDensity;
			FogFalloff0 = F->FogHeightFalloff;
			FogStart0 = F->StartDistance;
			FogOpacity0 = F->FogMaxOpacity;
			FogColor0 = F->FogInscatteringLuminance;
		}
		if (B > 0.001f)
		{
			F->SetFogDensity(FMath::Lerp(FogDensity0, 0.3f, B));
			F->SetFogHeightFalloff(FMath::Lerp(FogFalloff0, 0.002f, B));
			F->SetStartDistance(FMath::Lerp(FogStart0, 0.f, B));
			F->SetFogMaxOpacity(FMath::Lerp(FogOpacity0, 1.f, B));
			F->SetFogInscatteringColor(FMath::Lerp(FogColor0, TNBeachStormTuning::SandFog(), B));
			bFogApplied = true;
		}
		else
		{
			RestoreFog();
		}
	}
	// Tinte cálido, menos color y viñeta.
	InsidePostProcess->bEnabled = B > 0.001f;
	InsidePostProcess->BlendWeight = B;
	FPostProcessSettings& Settings = InsidePostProcess->Settings;
	Settings.bOverride_ColorSaturation = true;
	Settings.ColorSaturation = FVector4(0.65f, 0.65f, 0.65f, 1.f);
	Settings.bOverride_ColorGain = true;
	Settings.ColorGain = FVector4(1.1f, 0.97f, 0.8f, 1.f);
	Settings.bOverride_VignetteIntensity = true;
	Settings.VignetteIntensity = 0.8f;
}

void ATN_BeachStorm::RestoreFog()
{
	if (!bFogApplied)
	{
		return;
	}
	bFogApplied = false;
	if (UExponentialHeightFogComponent* F = Fog.Get())
	{
		F->SetFogDensity(FogDensity0);
		F->SetFogHeightFalloff(FogFalloff0);
		F->SetStartDistance(FogStart0);
		F->SetFogMaxOpacity(FogOpacity0);
		F->SetFogInscatteringColor(FogColor0);
	}
}
