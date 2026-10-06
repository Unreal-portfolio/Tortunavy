#include "World/Beach/TN_BeachGiantCrab.h"
#include "World/Beach/TN_BeachCameraShake.h"
#include "World/Beach/TN_BeachEnemySynth.h"
#include "World/Beach/TN_BeachStun.h"
#include "World/TN_EnemyDecisions.h"
#include "TN_BeachEnemyKit.h"
#include "TN_BeachEnemyMeshes.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TN_Log.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Player/TortugaCharacter.h"
#include "Settings/TN_CombatTuning.h"

namespace TNBeachCrab
{
	/** Estados del cangrejo gigante (Mover.State). */
	enum class EState : uint8 { Patrol, Idle, Chase, WindUp, Slam, Recover, Return, Dazed, ChargePrep, Charge, Skid };

	inline uint8 ToByte(EState E)
	{
		return static_cast<uint8>(E);
	}

	/** Zona de patrulla: esta fracción de la huella alrededor de su sitio. */
	constexpr float PatrolFraction = 0.52f;
	/** Ve a una tortuga a esta distancia del cuerpo (cm, por el tamaño) si está dentro de su cono delantero (±70°). */
	constexpr float DetectRadius = 2200.f;
	constexpr float SightHalfAngle = 70.f;
	/**
	 * Oye a una tortuga a esta distancia (cm, por el tamaño) desde cualquier lado: ×1,3 si corre (más de 3 m/s: la tortuga
	 * anda a 2 y corre a 4; antes 6, que no se alcanzaba nunca), ×0,6 si va agachada, en bola, en panzazo o casi quieta
	 * (menos de 0,6 m/s).
	 */
	constexpr float HearRadius = 1000.f;
	constexpr float HearLoud = 1.3f;
	constexpr float HearQuiet = 0.6f;
	constexpr float LoudSpeed = 300.f;
	constexpr float QuietSpeed = 60.f;
	/** La deja si la tortuga se aleja de su sitio más que esto (o 1,4 veces la huella). */
	constexpr float LeashRadius = 3800.f;
	/**
	 * Velocidades (cm/s, por el tamaño): paseo y vuelta al recorrido. La persecución, en TNBeachCrabTuning::ChaseSpeedFor
	 * (entre andar y correr: la tortuga anda a 200 y corre a 400).
	 */
	constexpr float PatrolSpeed = 300.f;
	constexpr float ReturnSpeed = 420.f;
	/** Aceleración y frenada andando (cm/s², por el tamaño): de parado a la persecución en ~0,8 s, y frena en ~0,5 s. */
	constexpr float WalkAccel = 700.f;
	constexpr float WalkDecel = 1100.f;
	/** Giro: velocidad máxima (grados/s) y cuánto tarda en llegar a lo que quiere (s): gira poco a poco, sin latigazos. */
	constexpr float MaxTurnRate = 170.f;
	constexpr float TurnResponse = 0.3f;
	/** Mira por delante (cm, por el tamaño; además del cuerpo y de lo que anda en 0,6 s) para rodear lo grande del reparto. */
	constexpr float LookAheadExtra = 250.f;
	/** Atasco: si en StuckCheck s se ha movido menos que StuckMove (cm, por el tamaño) queriendo andar, sale de lado un rato. */
	constexpr float StuckCheck = 1.f;
	constexpr float StuckMove = 60.f;
	constexpr float EscapeTime = 0.9f;
	/** Paradas del recorrido (s): cortas, chasqueando la pinza. Si no llega a un punto en este tiempo, pasa al siguiente. */
	constexpr float StopMin = 0.5f;
	constexpr float StopMax = 0.95f;
	constexpr float LegTimeout = 12.f;
	/** Radio del cuerpo en planta (cm, por el tamaño) para apartarse de los demás y de lo grande del reparto. */
	constexpr float BodyRadius = 420.f;
	/**
	 * Lo que se recoloca durante el aviso para que la pinza caiga donde marca la sombra (cm/s, por el tamaño: antes 650, que
	 * le dejaba avanzar 3,1 m durante el aviso y golpear desde ~10 m).
	 */
	constexpr float WindUpSpeed = 400.f;
	/** Empieza el mazazo si la tortuga está a su alcance más esto (antes 300: atacaba desde 10,1 m; ahora desde ~8,1 m). */
	constexpr float AttackSlack = 100.f;
	/** Aviso (pinza en alto temblando), caída, pinza clavada y espera hasta el siguiente mazazo (s). */
	constexpr float WindUpTime = 0.6f;
	constexpr float SlamTime = 0.14f;
	constexpr float RecoverTime = 1.1f;
	constexpr float CooldownTime = 1.4f;
	/**
	 * Radio del golpe alrededor de la sombra (cm, por el tamaño): la mano y el dedo, no medio campo (antes 280). Sale más
	 * 40 cm del radio de la tortuga. Además la pinza tiene que haber llegado de verdad a la sombra: si al caer su punta está
	 * a más de SlamTipTolerance de ella, el golpe cuenta donde toca la punta (ResolveSlam).
	 */
	constexpr float HitRadius = 170.f;
	constexpr float SlamTipTolerance = 150.f;
	/**
	 * Diferencia de altura máxima con la tortuga para que la pinza (a ras de arena) la pille (subida a algo alto no llega).
	 * Saltando por encima se libra: TNBeachCrabTuning::ClearsSlamByJump (un salto solo sube la cápsula 1,2 m, menos que esto).
	 */
	constexpr float SlamHeight = 280.f;
	/** Adelanto al apuntar (s de la velocidad de la tortuga; antes 0,3). */
	constexpr float LeadSeconds = 0.2f;
	/** Cabeceo del brazo con la pinza en alto. */
	constexpr float RaisePitch = 72.f;
	/** Largo de un ciclo de paso (cm, por el tamaño): cada pata da un paso por ciclo. */
	constexpr float Stride = 190.f;

	// ── Embestida ──
	/**
	 * A qué distancia de la tortuga (cm, por el tamaño) embiste y cuántas veces por segundo lo intenta ahí. Antes de 9 a
	 * 21 m; ahora empieza donde acaba el alcance de la pinza (8,5 m) y llega a 13 m.
	 */
	constexpr float ChargeMinDist = 850.f;
	constexpr float ChargeMaxDist = 1300.f;
	constexpr float ChargeChance = 0.55f;
	/** Se agacha clavando las patas (s), sale disparado (cm/s y cm/s², por el tamaño: más que la tortuga esprintando). */
	constexpr float ChargePrepTime = 0.55f;
	constexpr float ChargeSpeed = 1150.f;
	constexpr float ChargeAccel = 3200.f;
	/** Como mucho este tiempo embistiendo (antes 1,5); se pasa de la tortuga esto (cm; antes 700) y luego derrapa (s, cm/s²). */
	constexpr float ChargeMaxTime = 1.4f;
	constexpr float ChargeOvershoot = 400.f;
	constexpr float SkidTime = 0.8f;
	constexpr float SkidDecel = 2000.f;
	/** Tiempo sin volver a embestir (s). */
	constexpr float ChargeCooldown = 5.f;
	/**
	 * Holgura de la caja que arrolla, además del cuerpo con las patas (cm): el radio de la tortuga. Antes 90 y 110 cm,
	 * que arrollaban a 1 m del cangrejo sin tocarlo.
	 */
	constexpr float RamMargin = 45.f;
	/** Arrollada: empujón en el sentido de la embestida, hacia arriba y de lado, y vueltas (el derribo, en UTN_CombatTuning). */
	constexpr float ChargePush = 950.f;
	constexpr float ChargeUp = 480.f;
	constexpr float ChargeSidePush = 260.f;
	constexpr float ChargeSpin = 320.f;
	/** Surcos del derrape: cuántos a la vez y cuánto duran (s). */
	constexpr int32 MaxFurrows = 16;
	constexpr float FurrowLife = 5.f;

	inline float Smooth01(float X)
	{
		const float C = FMath::Clamp(X, 0.f, 1.f);
		return C * C * (3.f - 2.f * C);
	}
}

ATN_BeachGiantCrab::ATN_BeachGiantCrab()
{
	NetFrequencyNear = 12.f;
	SetNetUpdateFrequency(NetFrequencyNear);
	bThrottleWhenFar = true;
}

float ATN_BeachGiantCrab::GetBodyRadius() const
{
	return TNBeachCrab::BodyRadius * SizeK;
}

void ATN_BeachGiantCrab::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_BeachGiantCrab, BlindEndTime);
}

// ─────────────────────────────────────────────────────────────────────────────
// Construcción
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachGiantCrab::ApplySpec()
{
	SizeK = FMath::Clamp(Spec.SizeScale, 0.8f, 1.2f);
	const float Footprint = GetFootprintRadius();
	PatrolRadius = Footprint * TNBeachCrab::PatrolFraction;
	DetectRadius = TNBeachCrab::DetectRadius * SizeK;
	HearRadius = TNBeachCrab::HearRadius * SizeK;
	LeashRadius = FMath::Max(TNBeachCrab::LeashRadius * SizeK, Footprint * 1.4f);
	Reach = static_cast<float>(TNBeachMeshes::CrabRig().Reach) * SizeK;
	GroundZ = static_cast<float>(GetActorLocation().Z);
	StuckFrom = GetActorLocation();
	BuildCrab();
}

void ATN_BeachGiantCrab::BuildCrab()
{
	if (BodyBlock)
	{
		return;
	}
	USceneComponent* RigRoot = MakeRig();
	const TNBeachMeshes::FCrabRig R = TNBeachMeshes::CrabRig();

	Scaler = NewObject<USceneComponent>(this, TEXT("CrabScaler"));
	Scaler->SetupAttachment(RigRoot);
	Scaler->SetRelativeScale3D(FVector(SizeK));
	Scaler->RegisterComponent();

	// Cuerpo sólido en todas las máquinas: la tortuga no lo atraviesa (su bola, por el empuje propio). Tipo Pawn que bloquea lo dinámico,
	// como el cangrejo de siempre, para que los objetos del jugador le den (ITN_EnemyTargetInterface).
	BodyBlock = NewObject<UBoxComponent>(this, TEXT("CrabBlock"));
	BodyBlock->SetupAttachment(Scaler);
	BodyBlock->SetBoxExtent(FVector(TNBeachMeshes::CrabD * TNBeach::Scale, TNBeachMeshes::CrabW * TNBeach::Scale * 1.02, R.BodyZ * 0.75), false);
	BodyBlock->SetRelativeLocation(FVector(0.0, 0.0, R.BodyZ * 0.75));
	BodyBlock->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BodyBlock->SetCollisionObjectType(ECC_Pawn);
	BodyBlock->SetCollisionResponseToAllChannels(ECR_Ignore);
	BodyBlock->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	BodyBlock->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	BodyBlock->SetCanEverAffectNavigation(false);
	BodyBlock->SetGenerateOverlapEvents(true);
	BodyBlock->RegisterComponent();
	// La bola del caparazón no choca con él en la física: la saca el servidor por un lado (movido sin barrido, la hundía).
	RegisterSolidBlock(BodyBlock);

	if (!bHasScreen)
	{
		return;
	}

	const TNBeachMeshes::FCrabLook Look = TNBeachMeshes::CrabPalette(Spec.Seed);
	const FString Pal = FString::Printf(TEXT("Beach.Crab.%d."), ((Spec.Seed % 4) + 4) % 4);
	using TNProcMesh::FTNProcMeshBuffers;
	UStaticMesh* BodyMesh = TNBeachKit::CachedMesh(Pal + TEXT("Body"), [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildCrabBody(M, Look); });
	UStaticMesh* EyeMesh = TNBeachKit::CachedMesh(Pal + TEXT("Eye"), [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildCrabEye(M, Look); });
	UStaticMesh* LegL = TNBeachKit::CachedMesh(Pal + TEXT("LegL"), [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildCrabLeg(M, Look, -1.0); });
	UStaticMesh* LegR = TNBeachKit::CachedMesh(Pal + TEXT("LegR"), [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildCrabLeg(M, Look, 1.0); });
	UStaticMesh* ArmMesh = TNBeachKit::CachedMesh(Pal + TEXT("Arm"), [&Look, &R](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildCrabBigArm(M, Look, R); });
	UStaticMesh* HandMesh = TNBeachKit::CachedMesh(Pal + TEXT("Hand"), [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildCrabBigHand(M, Look); });
	UStaticMesh* FingerMesh = TNBeachKit::CachedMesh(Pal + TEXT("Finger"), [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildCrabBigFinger(M, Look); });
	UStaticMesh* SmallMesh = TNBeachKit::CachedMesh(Pal + TEXT("Small"), [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildCrabSmallClaw(M, Look); });

	Body = TNBeachKit::AddPart(this, Scaler, BodyMesh, FVector(0.0, 0.0, R.BodyZ), true, TN_ART("Beach.GiantCrab.Body"));
	Eyes.Add(TNBeachKit::AddPart(this, Body, EyeMesh, R.EyeL, false, TN_ART("Beach.GiantCrab.Eye")));
	Eyes.Add(TNBeachKit::AddPart(this, Body, EyeMesh, R.EyeR, false, TN_ART("Beach.GiantCrab.Eye")));
	for (int32 k = 0; k < 8; ++k)
	{
		UStaticMeshComponent* Leg = TNBeachKit::AddPart(this, Body, k < 4 ? LegL : LegR, R.LegPivot[k], true,
			k < 4 ? TN_ART("Beach.GiantCrab.LegLeft") : TN_ART("Beach.GiantCrab.LegRight"));
		if (Leg)
		{
			Leg->SetRelativeRotation(FRotator(0.f, R.LegSplay[k], 0.f));
		}
		Legs.Add(Leg);
	}
	BigArm = TNBeachKit::AddPart(this, Body, ArmMesh, R.BigShoulder, true, TN_ART("Beach.GiantCrab.BigArm"));
	BigHand = TNBeachKit::AddPart(this, BigArm, HandMesh, R.BigElbow, true, TN_ART("Beach.GiantCrab.BigHand"));
	BigFinger = TNBeachKit::AddPart(this, BigHand, FingerMesh, R.BigKnuckle, true, TN_ART("Beach.GiantCrab.BigFinger"));
	SmallClaw = TNBeachKit::AddPart(this, Body, SmallMesh, R.SmallShoulder, true, TN_ART("Beach.GiantCrab.SmallClaw"));
	ClawShadow = TNBeachKit::AddShadow(this, 0.6f);
	TNBeachKit::PlaceShadow(ClawShadow, FVector::ZeroVector, 0.f);

	using TNAmbientFX::EShape;
	TNAmbientFX::FEmitterDesc FoamDesc = TNBeachKit::MakeDesc(EShape::Puff, FLinearColor(0.96f, 0.98f, 1.f), true, 0.85f, 40, 4.f, 90.f, 0.f, 1.2f, 2.4f, 45.f, 95.f);
	FoamDesc.Buoyancy = 60.f;
	FoamDesc.Spread = 0.7f;
	FoamDesc.SpawnRadius = 60.f;
	TNBeachKit::InitEmitter(Foam, this, FoamDesc, static_cast<uint32>(Spec.Seed) + 11u);
	TNAmbientFX::FEmitterDesc PuffDesc = TNBeachKit::MakeDesc(EShape::Puff, FLinearColor(0.86f, 0.76f, 0.56f), true, 0.55f, 30, 0.f, 650.f, -100.f, 1.2f, 2.2f, 220.f, 520.f);
	PuffDesc.Drag = 1.2f;
	PuffDesc.Spread = 0.9f;
	PuffDesc.SpawnRadius = 150.f;
	TNBeachKit::InitEmitter(SandPuff, this, PuffDesc, static_cast<uint32>(Spec.Seed) + 12u);
	TNAmbientFX::FEmitterDesc GrainDesc = TNBeachKit::MakeDesc(EShape::Ember, FLinearColor(0.8f, 0.7f, 0.5f), false, 1.f, 40, 0.f, 1300.f, -1600.f, 0.8f, 1.4f, 26.f, 20.f);
	GrainDesc.Spread = 0.85f;
	GrainDesc.SpawnRadius = 120.f;
	TNBeachKit::InitEmitter(SandGrains, this, GrainDesc, static_cast<uint32>(Spec.Seed) + 13u);
	// Arena que levantan las patas al embestir y al derrapar (por delante, hacia arriba y hacia donde va).
	TNAmbientFX::FEmitterDesc DragDesc = TNBeachKit::MakeDesc(EShape::Puff, FLinearColor(0.88f, 0.78f, 0.58f), true, 0.6f, 60, 26.f, 700.f, -250.f, 0.8f, 1.6f, 160.f, 420.f);
	DragDesc.Drag = 1.4f;
	DragDesc.Spread = 0.75f;
	DragDesc.SpawnRadius = 260.f;
	TNBeachKit::InitEmitter(DragSand, this, DragDesc, static_cast<uint32>(Spec.Seed) + 14u);

	GetVoice(Body, 1200.f, 9000.f);
	LastShown = ShownLoc;
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor: recorrido y percepción
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachGiantCrab::BuildRoute()
{
	bRouteBuilt = true;
	Route.Reset();
	RouteStops.Reset();
	const float Yaw0 = ServerRng.FRandRange(0.f, 360.f);
	const FVector Axis = FRotator(0.f, Yaw0, 0.f).Vector();
	const FVector Side = FRotator(0.f, Yaw0 + 90.f, 0.f).Vector();
	const float Kind = ServerRng.FRand();

	// Un óvalo alrededor de su sitio (dos paradas por vuelta).
	if (Route.Num() == 0 && Kind < 0.7f)
	{
		const float A = PatrolRadius * 0.9f;
		const float B = A * ServerRng.FRandRange(0.45f, 0.7f);
		constexpr int32 N = 8;
		for (int32 k = 0; k < N; ++k)
		{
			const float Ang = 2.f * PI * k / N;
			Route.Add(Home + Axis * (FMath::Cos(Ang) * A) + Side * (FMath::Sin(Ang) * B));
			RouteStops.Add(k % 4 == 0 ? 1 : 0);
		}
		bRouteLoops = true;
	}
	// Ida y vuelta de lado por una recta que pasa por su sitio (se para en los extremos).
	if (Route.Num() == 0)
	{
		const float L = PatrolRadius * 0.9f;
		Route.Add(Home - Axis * L);
		Route.Add(Home);
		Route.Add(Home + Axis * L);
		RouteStops.Add(1);
		RouteStops.Add(0);
		RouteStops.Add(1);
		bRouteLoops = false;
	}
	for (FVector& P : Route)
	{
		P.Z = Home.Z;
	}
	RouteIndex = ServerRng.RandRange(0, Route.Num() - 1);
	RouteStep = ServerRng.FRand() < 0.5f ? 1 : -1;
}

void ATN_BeachGiantCrab::AdvanceRoute()
{
	const int32 Num = Route.Num();
	if (Num < 2)
	{
		return;
	}
	if (bRouteLoops)
	{
		RouteIndex = (RouteIndex + RouteStep + Num) % Num;
		return;
	}
	if (RouteIndex + RouteStep >= Num || RouteIndex + RouteStep < 0)
	{
		RouteStep = -RouteStep;
	}
	RouteIndex = FMath::Clamp(RouteIndex + RouteStep, 0, Num - 1);
}

int32 ATN_BeachGiantCrab::NearestRoutePoint() const
{
	int32 Best = 0;
	double BestSq = 1.0e18;
	for (int32 i = 0; i < Route.Num(); ++i)
	{
		const double DistSq = FVector::DistSquared2D(Route[i], SimLoc);
		if (DistSq < BestSq)
		{
			BestSq = DistSq;
			Best = i;
		}
	}
	return Best;
}

ATortugaCharacter* ATN_BeachGiantCrab::Perceive() const
{
	TArray<ATortugaCharacter*> Turtles;
	GatherTurtles(this, Turtles);
	const FVector Facing = FRotator(0.f, SimYaw, 0.f).Vector();
	const double CosSight = FMath::Cos(FMath::DegreesToRadians(static_cast<double>(TNBeachCrab::SightHalfAngle)));
	ATortugaCharacter* Best = nullptr;
	double BestSq = 1.0e18;
	for (ATortugaCharacter* Turtle : Turtles)
	{
		if (!IsTargetable(Turtle))
		{
			continue;
		}
		const FVector At = Turtle->GetActorLocation();
		if (FVector::Dist2D(At, Home) > LeashRadius)
		{
			continue;
		}
		FVector To = At - SimLoc;
		To.Z = 0.0;
		const double DistSq = To.SizeSquared();
		if (DistSq >= BestSq)
		{
			continue;
		}
		const double Dist = FMath::Sqrt(DistSq);
		// De frente la ve lejos; alrededor la oye: más si corre, menos si va agachada, en bola o casi quieta.
		bool bSensed = Dist < DetectRadius && (Dist < 1.0 || FVector::DotProduct(To / Dist, Facing) >= CosSight);
		if (!bSensed)
		{
			const float Speed = static_cast<float>(Turtle->GetVelocity().Size2D());
			float Hear = HearRadius;
			if (Turtle->IsInShell() || Turtle->IsBellyPoseActive() || Turtle->bIsCrouched || Speed < TNBeachCrab::QuietSpeed)
			{
				Hear *= TNBeachCrab::HearQuiet;
			}
			else if (Speed > TNBeachCrab::LoudSpeed)
			{
				Hear *= TNBeachCrab::HearLoud;
			}
			bSensed = Dist < Hear;
		}
		if (bSensed)
		{
			BestSq = DistSq;
			Best = Turtle;
		}
	}
	return Best;
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor: marcha (acelera, frena, gira poco a poco, rodea y sale de los atascos)
// ─────────────────────────────────────────────────────────────────────────────

FVector ATN_BeachGiantCrab::Integrate(float DeltaSeconds, bool* bOutBlocked)
{
	const FVector Free = SimLoc + FVector(MoveVel.X, MoveVel.Y, 0.0) * DeltaSeconds;
	// Sin meterse en otro enemigo.
	FVector Next = ResolveStep(Free, GetBodyRadius());
	if (bOutBlocked)
	{
		const double Step = FVector::Dist2D(Free, SimLoc);
		*bOutBlocked = Step > 2.0 && FVector::Dist2D(Next, Free) > Step * 0.65;
	}
	// Nunca más lejos de su sitio que la correa.
	FVector FromHome = Next - Home;
	FromHome.Z = 0.0;
	if (FromHome.Size() > LeashRadius)
	{
		Next = Home + FromHome.GetSafeNormal() * LeashRadius;
	}
	GroundTimer -= DeltaSeconds;
	if (GroundTimer <= 0.f)
	{
		GroundTimer = 0.1f;
		float Z = GroundZ;
		if (GroundHeightAt(Next, Z))
		{
			GroundZ = Z;
		}
	}
	Next.Z = FMath::FInterpTo(SimLoc.Z, static_cast<double>(GroundZ), static_cast<double>(DeltaSeconds), 8.0);
	// Lo que de verdad avanza: si algo lo frena, se frena (no sigue empujando ni patina contra el decorado).
	if (DeltaSeconds > KINDA_SMALL_NUMBER)
	{
		const FVector2D Real((Next.X - SimLoc.X) / DeltaSeconds, (Next.Y - SimLoc.Y) / DeltaSeconds);
		if (Real.SizeSquared() < MoveVel.SizeSquared())
		{
			MoveVel = Real;
		}
	}
	return Next;
}

FVector ATN_BeachGiantCrab::Drive(const FVector& Goal, float MaxSpeed, float DeltaSeconds, bool bArrive, float Accel, float Decel)
{
	FVector To = Goal - SimLoc;
	To.Z = 0.0;
	const float Dist = static_cast<float>(To.Size());
	float WantSpeed = Dist > 20.f ? MaxSpeed : 0.f;
	if (bArrive)
	{
		// Frena a tiempo para llegar parado (v² = 2·a·d).
		WantSpeed = FMath::Min(WantSpeed, FMath::Sqrt(2.f * Decel * FMath::Max(0.f, Dist - 20.f)));
	}
	FVector Dir = Dist > 1.f ? To / Dist : FVector::ZeroVector;
	if (EscapeLeft > 0.f)
	{
		// Saliendo de un atasco: hacia el lado elegido un momento.
		EscapeLeft -= DeltaSeconds;
		Dir = FVector(EscapeDir.X, EscapeDir.Y, 0.0);
		WantSpeed = MaxSpeed * 0.8f;
	}
	// Acelera o frena hacia la velocidad que quiere; un cambio de rumbo también cuenta como frenada (gira sin patinar).
	const FVector2D Want = FVector2D(Dir.X, Dir.Y) * WantSpeed;
	FVector2D Change = Want - MoveVel;
	const float Rate = (Want.SizeSquared() > MoveVel.SizeSquared() ? Accel : Decel) * DeltaSeconds;
	const float ChangeSize = static_cast<float>(Change.Size());
	if (ChangeSize > Rate && ChangeSize > KINDA_SMALL_NUMBER)
	{
		Change *= Rate / ChangeSize;
	}
	MoveVel += Change;

	// Atasco: quiere andar y apenas se mueve. Sale un momento de lado (a un lado y al otro por turnos).
	if (WantSpeed > 60.f && EscapeLeft <= 0.f)
	{
		StuckTimer += DeltaSeconds;
		if (StuckTimer >= TNBeachCrab::StuckCheck)
		{
			if (FVector::Dist2D(SimLoc, StuckFrom) < TNBeachCrab::StuckMove * SizeK)
			{
				++StuckCount;
				const FVector2D Ahead(Dir.X, Dir.Y);
				const FVector2D Across(-Dir.Y, Dir.X);
				EscapeDir = ((StuckCount % 2 == 1 ? Across : -Across) * 0.85 - Ahead * 0.5).GetSafeNormal();
				EscapeLeft = TNBeachCrab::EscapeTime;
			}
			else
			{
				StuckCount = 0;
			}
			StuckTimer = 0.f;
			StuckFrom = SimLoc;
		}
	}
	else if (EscapeLeft <= 0.f)
	{
		StuckTimer = 0.f;
		StuckFrom = SimLoc;
	}
	return Integrate(DeltaSeconds);
}

float ATN_BeachGiantCrab::UpdateFacing(float WantYaw, float DeltaSeconds, float MaxRate)
{
	const float Diff = FMath::FindDeltaAngleDegrees(SimYaw, WantYaw);
	// El giro acelera y frena (nada de latigazos) y nunca se pasa de lo que quiere.
	const float WantRate = FMath::Clamp(Diff / TNBeachCrab::TurnResponse, -MaxRate, MaxRate);
	YawRate = FMath::FInterpTo(YawRate, WantRate, DeltaSeconds, 10.f);
	float Turn = YawRate * DeltaSeconds;
	if ((Diff >= 0.f && Turn > Diff) || (Diff < 0.f && Turn < Diff))
	{
		Turn = Diff;
	}
	return FMath::UnwindDegrees(SimYaw + Turn);
}

float ATN_BeachGiantCrab::SidewaysYaw(float Heading)
{
	// Mirando a Heading - 90 avanza hacia su derecha; a Heading + 90, hacia su izquierda. Se queda con el costado que lleva
	// salvo que el otro esté mucho más a mano (no cambia de lado a cada curva).
	const float Right = FMath::UnwindDegrees(Heading - 90.f);
	const float Left = FMath::UnwindDegrees(Heading + 90.f);
	const float ToRight = FMath::Abs(FMath::FindDeltaAngleDegrees(SimYaw, Right));
	const float ToLeft = FMath::Abs(FMath::FindDeltaAngleDegrees(SimYaw, Left));
	if (SideSign == 0.f)
	{
		SideSign = ToRight <= ToLeft ? 1.f : -1.f;
	}
	else if (SideSign > 0.f && ToRight > ToLeft + 50.f)
	{
		SideSign = -1.f;
	}
	else if (SideSign < 0.f && ToLeft > ToRight + 50.f)
	{
		SideSign = 1.f;
	}
	return SideSign > 0.f ? Right : Left;
}

void ATN_BeachGiantCrab::ServerWalk(const FVector& Goal, float Speed, float DeltaSeconds, bool bSideways, bool bArrive)
{
	const FVector Next = Drive(Goal, Speed, DeltaSeconds, bArrive, TNBeachCrab::WalkAccel * SizeK, TNBeachCrab::WalkDecel * SizeK);
	float WantYaw = SimYaw;
	FVector Heading(MoveVel.X, MoveVel.Y, 0.0);
	if (Heading.SizeSquared() < FMath::Square(40.0))
	{
		Heading = Goal - SimLoc;
		Heading.Z = 0.0;
	}
	if (Heading.SizeSquared() > FMath::Square(30.0))
	{
		const float HeadingYaw = static_cast<float>(Heading.Rotation().Yaw);
		WantYaw = bSideways ? SidewaysYaw(HeadingYaw) : HeadingYaw;
	}
	ServerMoveTo(Next, UpdateFacing(WantYaw, DeltaSeconds, TNBeachCrab::MaxTurnRate));
}

void ATN_BeachGiantCrab::ServerBrake(float DeltaSeconds, float Decel, float WantYaw, float MaxTurnRate)
{
	const float Speed = static_cast<float>(MoveVel.Size());
	const float Drop = Decel * DeltaSeconds;
	MoveVel = Speed > Drop ? MoveVel * ((Speed - Drop) / Speed) : FVector2D::ZeroVector;
	const FVector Next = Integrate(DeltaSeconds);
	if (MaxTurnRate <= 0.f)
	{
		YawRate = 0.f;
		ServerMoveTo(Next, SimYaw);
		return;
	}
	ServerMoveTo(Next, UpdateFacing(WantYaw, DeltaSeconds, MaxTurnRate));
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor: mazazo y embestida
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachGiantCrab::StartWindUp(ATortugaCharacter* Victim)
{
	using TNBeachCrab::EState;
	const FVector Vel = Victim->GetVelocity();
	FVector Impact = Victim->GetActorLocation() + FVector(Vel.X, Vel.Y, 0.0) * TNBeachCrab::LeadSeconds;
	// Como mucho, a donde llega la pinza desde donde puede colocarse durante el aviso.
	FVector FromMe = Impact - SimLoc;
	FromMe.Z = 0.0;
	const float MaxReach = Reach + TNBeachCrab::WindUpSpeed * SizeK * TNBeachCrab::WindUpTime * 0.8f;
	if (FromMe.Size() > MaxReach)
	{
		Impact = SimLoc + FromMe.GetSafeNormal() * MaxReach;
	}
	float Z = static_cast<float>(Victim->GetActorLocation().Z);
	if (TraceGround(this, Impact, Z))
	{
		Impact.Z = Z;
	}
	else
	{
		Impact.Z = SimLoc.Z;
	}
	ServerSetState(TNBeachCrab::ToByte(EState::WindUp), Impact);
}

void ATN_BeachGiantCrab::ResolveSlam()
{
	// El golpe cuenta donde la pinza toca de verdad: la punta del dedo, delante del cuerpo a lo que alcanza (Reach). Si al caer
	// aún no ha llegado a la sombra (no le ha dado tiempo a recolocarse en el aviso), vale donde ha caído, no donde iba.
	FVector Impact = Mover.Aim;
	const FVector Tip = SimLoc + FRotator(0.f, SimYaw, 0.f).RotateVector(FVector(Reach, 0.0, 0.0));
	if (FVector::Dist2D(Tip, Impact) > TNBeachCrab::SlamTipTolerance * SizeK)
	{
		Impact = FVector(Tip.X, Tip.Y, Impact.Z);
	}
	bool bHit = false;
	TArray<ATortugaCharacter*> Turtles;
	GatherTurtles(this, Turtles);
	const float Radius = TNBeachCrab::HitRadius * SizeK + 40.f;
	for (ATortugaCharacter* Turtle : Turtles)
	{
		if (!CanBeHit(Turtle))
		{
			continue;
		}
		const FVector At = Turtle->GetActorLocation();
		if (FVector::Dist2D(At, Impact) > Radius || FMath::Abs(At.Z - Impact.Z) > TNBeachCrab::SlamHeight)
		{
			continue;
		}
		// Saltando por encima de la pinza: por el aire, con los pies bien despegados de su suelo al caer la pinza.
		const UCapsuleComponent* Capsule = Turtle->GetCapsuleComponent();
		const UCharacterMovementComponent* Move = Turtle->GetCharacterMovement();
		const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.f;
		float TurtleGroundZ = static_cast<float>(At.Z) - HalfHeight;
		GroundHeightAt(At, TurtleGroundZ);
		const float FeetAboveGround = static_cast<float>(At.Z) - HalfHeight - TurtleGroundZ;
		if (TNBeachCrabTuning::ClearsSlamByJump(Move && Move->IsFalling(), FeetAboveGround))
		{
			UE_LOG(LogTortunabo, Log, TEXT("[Playa] %s salta por encima del mazazo de %s (%.0f cm)."), *GetNameSafe(Turtle), *GetName(), FeetAboveGround);
			continue;
		}
		FVector Away = At - Impact;
		Away.Z = 0.0;
		Away = Away.GetSafeNormal();
		// Despachurrada: casi en el sitio, un empujoncito hacia fuera.
		StunTurtle(Turtle, UTN_CombatTuning::Get().GiantCrabStunSeconds, Away * 320.0 + FVector(0.0, 0.0, 160.0));
		IgnoreTurtle(Turtle, UTN_CombatTuning::Get().GiantCrabIgnoreSeconds);
		bHit = true;
	}
	MulticastSlam(Impact, bHit);
}

bool ATN_BeachGiantCrab::IsChargeLaneClear(const FVector& To)
{
	FVector Lane = To - SimLoc;
	Lane.Z = 0.0;
	const double Length = Lane.Size();
	if (Length < 1.0)
	{
		return false;
	}
	// Sin nada grande en medio (en tramos de 2,5 m, con casi todo el cuerpo) y sin salirse de la correa.
	const FVector Dir = Lane / Length;
	const double End = Length + TNBeachCrab::ChargeOvershoot * SizeK;
	for (double D = 250.0; D <= End; D += 250.0)
	{
		const FVector P = SimLoc + Dir * D;
		if (FVector::Dist2D(P, Home) > LeashRadius)
		{
			return false;
		}
	}
	return true;
}

void ATN_BeachGiantCrab::StartChargePrep(ATortugaCharacter* Victim)
{
	const FVector Vel = Victim->GetVelocity();
	const FVector Aim = Victim->GetActorLocation() + FVector(Vel.X, Vel.Y, 0.0) * 0.35;
	FVector Dir = Aim - SimLoc;
	Dir.Z = 0.0;
	Dir = Dir.GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		return;
	}
	// Se pasa un poco de la tortuga (no frena justo encima), sin salirse de su correa.
	FVector End = Aim + Dir * (TNBeachCrab::ChargeOvershoot * SizeK);
	for (int32 k = 0; k < 6 && FVector::Dist2D(End, Home) > LeashRadius * 0.95f; ++k)
	{
		End = FMath::Lerp(SimLoc, End, 0.8);
	}
	End.Z = SimLoc.Z;
	ChargeDir = Dir;
	Target = Victim;
	ServerSetState(TNBeachCrab::ToByte(TNBeachCrab::EState::ChargePrep), End);
}

void ATN_BeachGiantCrab::ChargeHits()
{
	const float Speed = static_cast<float>(MoveVel.Size());
	if (Speed < 450.f * SizeK)
	{
		return;
	}
	TArray<ATortugaCharacter*> Turtles;
	GatherTurtles(this, Turtles);
	const FTransform Xf(FRotator(0.f, SimYaw, 0.f), SimLoc);
	const double S = TNBeach::Scale * SizeK;
	const double HalfX = TNBeachMeshes::CrabD * S + TNBeachCrab::RamMargin;
	const double HalfY = TNBeachMeshes::CrabW * S * 1.08 + TNBeachCrab::RamMargin;
	const FVector Dir = FVector(MoveVel.X, MoveVel.Y, 0.0).GetSafeNormal();
	for (ATortugaCharacter* Turtle : Turtles)
	{
		if (!IsTargetable(Turtle))
		{
			continue;
		}
		const FVector At = Turtle->GetActorLocation();
		const FVector Local = Xf.InverseTransformPositionNoScale(At);
		if (FMath::Abs(Local.X) > HalfX || FMath::Abs(Local.Y) > HalfY || Local.Z < -150.0 || Local.Z > 700.0)
		{
			continue;
		}
		// Arrollada: lanzada en el sentido de la embestida, algo hacia fuera y hacia arriba, dando vueltas.
		FVector Away = At - SimLoc;
		Away.Z = 0.0;
		const FVector Out = (Away - Dir * FVector::DotProduct(Away, Dir)).GetSafeNormal();
		const FVector Push = Dir * TNBeachCrab::ChargePush + Out * TNBeachCrab::ChargeSidePush + FVector(0.0, 0.0, TNBeachCrab::ChargeUp);
		const FVector Spin = FVector::CrossProduct(FVector::UpVector, Dir) * TNBeachCrab::ChargeSpin;
		KnockDownTurtle(Turtle, UTN_CombatTuning::Get().GiantCrabChargeKnockSeconds, Push, Spin);
		IgnoreTurtle(Turtle, UTN_CombatTuning::Get().GiantCrabIgnoreSeconds);
		MulticastRam(At);
	}
}

void ATN_BeachGiantCrab::EndCharge()
{
	AttackCooldown = 0.8f;
	ChargeCooldownLeft = TNBeachCrab::ChargeCooldown;
	ATortugaCharacter* Next = Target.Get();
	const bool bCanSee = !IsBlinded();
	if (!bCanSee || !IsTargetable(Next) || FVector::Dist2D(Next->GetActorLocation(), Home) > LeashRadius)
	{
		Next = bCanSee ? Perceive() : nullptr;
	}
	Target = Next;
	ServerSetState(TNBeachCrab::ToByte(Next ? TNBeachCrab::EState::Chase : TNBeachCrab::EState::Return));
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor: estados
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachGiantCrab::ServerTick(float DeltaSeconds)
{
	using TNBeachCrab::EState;
	using TNBeachCrab::ToByte;
	const EState State = static_cast<EState>(GetMoverState());
	AttackCooldown -= DeltaSeconds;
	ChargeCooldownLeft -= DeltaSeconds;
	StateLeft -= DeltaSeconds;

	if (IsHitStunned())
	{
		if (State != EState::Dazed)
		{
			Target.Reset();
			ServerSetState(ToByte(EState::Dazed));
		}
		// Mareado: frena en unos pasos (sin patinar) y se queda quieto, con los ojos dando vueltas.
		ServerBrake(DeltaSeconds, TNBeachCrab::SkidDecel * SizeK, SimYaw, 0.f);
		return;
	}
	const bool bCanSee = !IsBlinded();
	if (!bRouteBuilt)
	{
		BuildRoute();
	}

	switch (State)
	{
	case EState::Dazed:
		Target.Reset();
		ServerSetState(ToByte(EState::Return));
		break;

	case EState::Patrol:
	case EState::Idle:
	{
		if (bCanSee)
		{
			if (ATortugaCharacter* Sensed = Perceive())
			{
				Target = Sensed;
				ServerSetState(ToByte(EState::Chase));
				break;
			}
		}
		if (State == EState::Patrol)
		{
			// Siempre andando su recorrido; frena para llegar a los puntos de parada y en ellos chasquea la pinza.
			PatrolGoal = Route.IsValidIndex(RouteIndex) ? Route[RouteIndex] : Home;
			const bool bStop = RouteStops.IsValidIndex(RouteIndex) && RouteStops[RouteIndex] != 0;
			ServerWalk(PatrolGoal, TNBeachCrab::PatrolSpeed * SizeK, DeltaSeconds, true, bStop);
			const bool bArrived = FVector::Dist2D(SimLoc, PatrolGoal) < (bStop ? 90.0 : 200.0);
			if (bArrived || GetStateAge() > TNBeachCrab::LegTimeout || StuckCount >= 2)
			{
				StuckCount = 0;
				AdvanceRoute();
				if (bStop && bArrived)
				{
					StateLeft = ServerRng.FRandRange(TNBeachCrab::StopMin, TNBeachCrab::StopMax);
					ServerSetState(ToByte(EState::Idle));
				}
				else
				{
					ServerSetState(ToByte(EState::Patrol));
				}
			}
		}
		else
		{
			ServerBrake(DeltaSeconds, TNBeachCrab::WalkDecel * SizeK, SimYaw, 0.f);
			if (StateLeft <= 0.f)
			{
				ServerSetState(ToByte(EState::Patrol));
			}
		}
		break;
	}

	case EState::Chase:
	{
		ATortugaCharacter* Victim = Target.Get();
		const bool bValid = bCanSee && IsTargetable(Victim);
		const float FromHome = bValid ? static_cast<float>(FVector::Dist2D(Victim->GetActorLocation(), Home)) : 0.f;
		const float ToVictim = bValid ? static_cast<float>(FVector::Dist2D(Victim->GetActorLocation(), SimLoc)) : 0.f;
		// A media distancia, de vez en cuando, embiste (si no hay nada grande en medio).
		if (bValid && FromHome <= LeashRadius && ChargeCooldownLeft <= 0.f && AttackCooldown <= 0.f
			&& ToVictim > TNBeachCrab::ChargeMinDist * SizeK && ToVictim < TNBeachCrab::ChargeMaxDist * SizeK
			&& ServerRng.FRand() < TNBeachCrab::ChargeChance * DeltaSeconds && IsChargeLaneClear(Victim->GetActorLocation()))
		{
			StartChargePrep(Victim);
			break;
		}
		// La misma decisión (probada) que el cangrejo de siempre, con las medidas del gigante.
		switch (TNCrabLogic::DecideChaseTransition(bValid, FromHome, LeashRadius, ToVictim, Reach + TNBeachCrab::AttackSlack * SizeK))
		{
		case TNCrabLogic::EChaseTransition::ReturnToPatrol_TargetLost:
			Target = bCanSee ? Perceive() : nullptr;
			if (!Target.IsValid())
			{
				ServerSetState(ToByte(EState::Return));
			}
			break;
		case TNCrabLogic::EChaseTransition::ReturnToPatrol_OutOfZone:
			Target.Reset();
			ServerSetState(ToByte(EState::Return));
			break;
		case TNCrabLogic::EChaseTransition::StartAttack:
			if (AttackCooldown <= 0.f)
			{
				StartWindUp(Victim);
			}
			else
			{
				ServerWalk(Victim->GetActorLocation(), TNBeachCrabTuning::ChaseSpeedFor(SizeK) * 0.5f, DeltaSeconds, true, true);
			}
			break;
		case TNCrabLogic::EChaseTransition::KeepChasing:
			ServerWalk(Victim->GetActorLocation(), TNBeachCrabTuning::ChaseSpeedFor(SizeK), DeltaSeconds, true, false);
			break;
		}
		break;
	}

	case EState::WindUp:
	{
		// Se recoloca para que la pinza caiga en la sombra y la encara.
		const FVector Impact = Mover.Aim;
		FVector Dir = Impact - SimLoc;
		Dir.Z = 0.0;
		Dir = Dir.IsNearlyZero() ? FRotator(0.f, SimYaw, 0.f).Vector() : Dir.GetSafeNormal();
		const FVector Next = Drive(Impact - Dir * Reach, TNBeachCrab::WindUpSpeed * SizeK, DeltaSeconds, true,
			TNBeachCrab::WalkAccel * 2.5f * SizeK, TNBeachCrab::WalkDecel * 2.5f * SizeK);
		ServerMoveTo(Next, UpdateFacing(static_cast<float>(Dir.Rotation().Yaw), DeltaSeconds, 420.f));
		if (GetStateAge() >= TNBeachCrab::WindUpTime)
		{
			ServerSetState(ToByte(EState::Slam), Impact);
		}
		break;
	}

	case EState::Slam:
		ServerBrake(DeltaSeconds, TNBeachCrab::SkidDecel * SizeK, SimYaw, 0.f);
		if (GetStateAge() >= TNBeachCrab::SlamTime)
		{
			ResolveSlam();
			AttackCooldown = TNBeachCrab::CooldownTime;
			ServerSetState(ToByte(EState::Recover), FVector(Mover.Aim));
		}
		break;

	case EState::Recover:
		ServerBrake(DeltaSeconds, TNBeachCrab::SkidDecel * SizeK, SimYaw, 0.f);
		if (GetStateAge() >= TNBeachCrab::RecoverTime)
		{
			ATortugaCharacter* Next = Target.Get();
			if (!bCanSee || !IsTargetable(Next) || FVector::Dist2D(Next->GetActorLocation(), Home) > LeashRadius)
			{
				Next = bCanSee ? Perceive() : nullptr;
			}
			Target = Next;
			ServerSetState(ToByte(Next ? EState::Chase : EState::Return));
		}
		break;

	case EState::ChargePrep:
	{
		// Se agacha clavando las patas, frena y se pone de lado hacia la tortuga.
		const float Heading = static_cast<float>(ChargeDir.Rotation().Yaw);
		ServerBrake(DeltaSeconds, TNBeachCrab::SkidDecel * SizeK, SidewaysYaw(Heading), 360.f);
		if (GetStateAge() >= TNBeachCrab::ChargePrepTime)
		{
			ServerSetState(ToByte(EState::Charge), FVector(Mover.Aim));
		}
		break;
	}

	case EState::Charge:
	{
		// Disparado de lado en línea recta (sin rodear: el camino estaba libre al empezar).
		const FVector2D Want = FVector2D(ChargeDir.X, ChargeDir.Y) * (TNBeachCrab::ChargeSpeed * SizeK);
		FVector2D Change = Want - MoveVel;
		const float Rate = TNBeachCrab::ChargeAccel * SizeK * DeltaSeconds;
		const float ChangeSize = static_cast<float>(Change.Size());
		if (ChangeSize > Rate && ChangeSize > KINDA_SMALL_NUMBER)
		{
			Change *= Rate / ChangeSize;
		}
		MoveVel += Change;
		bool bBlocked = false;
		const FVector Next = Integrate(DeltaSeconds, &bBlocked);
		const float Heading = static_cast<float>(ChargeDir.Rotation().Yaw);
		ServerMoveTo(Next, UpdateFacing(SidewaysYaw(Heading), DeltaSeconds, 200.f));
		ChargeHits();
		if (bBlocked && GetStateAge() > 0.15f)
		{
			// Se ha estampado contra algo grande (o contra otro enemigo): se queda mareado.
			MulticastCrash(SimLoc + ChargeDir * (TNBeachMeshes::CrabW * TNBeach::Scale * SizeK));
			MoveVel = FVector2D::ZeroVector;
			EndCharge();
			ApplyHitStun(UTN_CombatTuning::Get().GiantCrabCrashStunSeconds, this);
			break;
		}
		FVector Left = FVector(Mover.Aim) - SimLoc;
		Left.Z = 0.0;
		if (FVector::DotProduct(Left, ChargeDir) < 60.0 || GetStateAge() > TNBeachCrab::ChargeMaxTime)
		{
			ServerSetState(ToByte(EState::Skid), FVector(Mover.Aim));
		}
		break;
	}

	case EState::Skid:
		// Derrapa clavando las patas (surcos y arena por delante) y aún arrolla a quien pille.
		ServerBrake(DeltaSeconds, TNBeachCrab::SkidDecel * SizeK, SimYaw, 0.f);
		ChargeHits();
		if (MoveVel.SizeSquared() < FMath::Square(40.0) || GetStateAge() > TNBeachCrab::SkidTime)
		{
			EndCharge();
		}
		break;

	case EState::Return:
	default:
	{
		if (bCanSee)
		{
			if (ATortugaCharacter* Sensed = Perceive())
			{
				Target = Sensed;
				ServerSetState(ToByte(EState::Chase));
				break;
			}
		}
		// Vuelve a su recorrido por el punto más cercano y sigue patrullando desde ahí.
		const int32 Nearest = NearestRoutePoint();
		const FVector Back = Route.IsValidIndex(Nearest) ? Route[Nearest] : Home;
		ServerWalk(Back, TNBeachCrab::ReturnSpeed * SizeK, DeltaSeconds, true, false);
		if (FVector::Dist2D(SimLoc, Back) < 200.0 || GetStateAge() > TNBeachCrab::LegTimeout * 2.f || StuckCount >= 3)
		{
			StuckCount = 0;
			RouteIndex = Nearest;
			AdvanceRoute();
			ServerSetState(ToByte(EState::Patrol));
		}
		break;
	}
	}

	if (IsDebugDraw())
	{
		UWorld* World = GetWorld();
		DrawDebugCircle(World, SimLoc + FVector(0.0, 0.0, 40.0), DetectRadius, 48, FColor::Yellow, false, -1.f, 0, 10.f, FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false);
		DrawDebugCircle(World, Home + FVector(0.0, 0.0, 40.0), LeashRadius, 64, FColor::Orange, false, -1.f, 0, 10.f, FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false);
		DrawDebugCircle(World, SimLoc + FVector(0.0, 0.0, 40.0), HearRadius, 32, FColor::Cyan, false, -1.f, 0, 8.f, FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false);
		for (int32 i = 0; i + 1 < Route.Num() + (bRouteLoops ? 1 : 0); ++i)
		{
			const FVector A = Route[i] + FVector(0.0, 0.0, 60.0);
			const FVector B = Route[(i + 1) % Route.Num()] + FVector(0.0, 0.0, 60.0);
			DrawDebugLine(World, A, B, RouteStops.IsValidIndex(i) && RouteStops[i] ? FColor::Green : FColor(0, 120, 0), false, -1.f, 0, 8.f);
		}
		DrawDebugLine(World, SimLoc + FVector(0.0, 0.0, 500.0), SimLoc + FVector(MoveVel.X, MoveVel.Y, 0.0) + FVector(0.0, 0.0, 500.0), FColor::White, false, -1.f, 0, 10.f);
		const EState Shown = static_cast<EState>(GetMoverState());
		if (Shown == EState::WindUp || Shown == EState::Slam)
		{
			DrawDebugCircle(World, FVector(Mover.Aim) + FVector(0.0, 0.0, 40.0), TNBeachCrab::HitRadius * SizeK, 32, FColor::Red, false, -1.f, 0, 12.f, FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false);
		}
		if (Shown == EState::ChargePrep || Shown == EState::Charge || Shown == EState::Skid)
		{
			DrawDebugLine(World, SimLoc + FVector(0.0, 0.0, 200.0), FVector(Mover.Aim) + FVector(0.0, 0.0, 200.0), FColor::Magenta, false, -1.f, 0, 14.f);
		}
		if (const ATortugaCharacter* Victim = Target.Get())
		{
			DrawDebugLine(World, SimLoc + FVector(0.0, 0.0, 300.0), Victim->GetActorLocation(), FColor::Red, false, -1.f, 0, 8.f);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Objetos del jugador (ITN_EnemyTargetInterface) y mareo por lo que se le lanza
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachGiantCrab::ApplyStun(float Duration)
{
	// La concha trampa (y lo que venga por la interfaz de siempre): el mareo de la playa, con pajaritos.
	ApplyHitStun(Duration, nullptr);
}

void ATN_BeachGiantCrab::ApplyBlind(float Duration)
{
	if (!HasAuthority() || Duration <= 0.f)
	{
		return;
	}
	const float Now = static_cast<float>(ServerNow(this));
	BlindEndTime = TNCrabLogic::ExtendEffectEndTime(BlindEndTime, Now, Duration);
	const TNBeachCrab::EState State = static_cast<TNBeachCrab::EState>(GetMoverState());
	if (State == TNBeachCrab::EState::Chase || State == TNBeachCrab::EState::WindUp || State == TNBeachCrab::EState::ChargePrep)
	{
		Target.Reset();
		ServerSetState(TNBeachCrab::ToByte(TNBeachCrab::EState::Return));
	}
}

bool ATN_BeachGiantCrab::IsStunned() const
{
	return IsHitStunned();
}

bool ATN_BeachGiantCrab::IsBlinded() const
{
	return TNCrabLogic::ComputeEffectRemaining(BlindEndTime, static_cast<float>(ServerNow(this))) > 0.f;
}

void ATN_BeachGiantCrab::ApplyHitStun(float Seconds, AActor* InstigatorActor)
{
	Super::ApplyHitStun(Seconds, InstigatorActor);
	if (HasAuthority() && IsHitStunned() && static_cast<TNBeachCrab::EState>(GetMoverState()) != TNBeachCrab::EState::Dazed)
	{
		Target.Reset();
		ServerSetState(TNBeachCrab::ToByte(TNBeachCrab::EState::Dazed));
	}
}

bool ATN_BeachGiantCrab::GetHitCapsule(FVector& OutA, FVector& OutB, float& OutRadius) const
{
	// El caparazón: de un costado al otro (5 m de ancho) a la altura del cuerpo.
	const double S = TNBeach::Scale * SizeK;
	const FVector Right = FRotator(0.f, ShownYaw, 0.f).RotateVector(FVector::RightVector);
	const FVector Center = ShownLoc + FVector(0.0, 0.0, TNBeachMeshes::CrabH * 1.25 * S);
	const double Half = TNBeachMeshes::CrabW * S * 0.55;
	OutA = Center - Right * Half;
	OutB = Center + Right * Half;
	OutRadius = static_cast<float>(TNBeachMeshes::CrabD * S);
	return true;
}

FVector ATN_BeachGiantCrab::GetHitStunAnchor() const
{
	// Por encima de los ojos (pedúnculos incluidos).
	return ShownLoc + FVector(0.0, 0.0, TNBeachMeshes::CrabH * TNBeach::Scale * SizeK * 3.3);
}

float ATN_BeachGiantCrab::GetHitStunScale() const
{
	return 6.f * SizeK;
}

// ─────────────────────────────────────────────────────────────────────────────
// Visual
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachGiantCrab::OnMoverStateChanged(uint8 OldState)
{
	if (!bHasScreen || !Voice)
	{
		return;
	}
	using TNBeachCrab::EState;
	const EState State = static_cast<EState>(GetMoverState());
	const float Pitch = 1.f / FMath::Sqrt(SizeK);
	switch (State)
	{
	case EState::Chase:
		if (static_cast<EState>(OldState) != EState::Recover)
		{
			Voice->Play(ETNBeachSfx::Clack, Pitch, 1.f);
		}
		break;
	case EState::WindUp:
		Voice->Play(ETNBeachSfx::Clack, Pitch * 0.8f, 1.2f);
		break;
	case EState::Dazed:
		Voice->Play(ETNBeachSfx::Clack, Pitch * 0.6f, 0.8f);
		break;
	case EState::ChargePrep:
		// Chasquido grave y las patas que se clavan en la arena.
		Voice->Play(ETNBeachSfx::Clack, Pitch * 0.65f, 1.3f);
		Voice->Play(ETNBeachSfx::Skitter, Pitch * 0.7f, 0.9f);
		break;
	case EState::Charge:
		Voice->Play(ETNBeachSfx::Skitter, Pitch * 1.2f, 1.1f);
		break;
	case EState::Skid:
		// Arena que rasca al frenar.
		Voice->Play(ETNBeachSfx::Roll, 0.55f, 1.3f);
		break;
	default:
		break;
	}
}

void ATN_BeachGiantCrab::MulticastSlam_Implementation(FVector_NetQuantize Where, bool bHit)
{
	if (!bHasScreen)
	{
		return;
	}
	const FVector At = Where;
	if (Voice)
	{
		Voice->SetWorldLocation(At);
		Voice->Play(ETNBeachSfx::Slam, 1.f / FMath::Sqrt(SizeK), bHit ? 1.4f : 1.1f);
	}
	TNBeachKit::BurstAt(SandPuff, At + FVector(0.0, 0.0, 60.0), FVector::UpVector, 16);
	TNBeachKit::BurstAt(SandGrains, At + FVector(0.0, 0.0, 40.0), FVector::UpVector, 30);
	UTN_BeachCameraShake::Kick(this, At, bHit ? 0.8f : 0.55f, 1500.f, 6500.f);
}

void ATN_BeachGiantCrab::MulticastRam_Implementation(FVector_NetQuantize Where)
{
	if (!bHasScreen)
	{
		return;
	}
	const FVector At = Where;
	if (Voice)
	{
		Voice->SetWorldLocation(At);
		Voice->Play(ETNBeachSfx::Slam, 1.25f / FMath::Sqrt(SizeK), 1.3f);
		Voice->Play(ETNBeachSfx::Clack, 1.1f, 1.f);
	}
	TNBeachKit::BurstAt(SandPuff, At, FVector::UpVector, 10);
	TNBeachKit::BurstAt(SandGrains, At + FVector(0.0, 0.0, 40.0), FVector::UpVector, 20);
	UTN_BeachCameraShake::Kick(this, At, 0.6f, 600.f, 3500.f);
	ShowPop(NSLOCTEXT("TNBeach", "CrabRam", "¡EMBESTIDA!"), FColor(255, 110, 70), At + FVector(0.0, 0.0, 300.0), 150.f);
}

void ATN_BeachGiantCrab::MulticastCrash_Implementation(FVector_NetQuantize Where)
{
	if (!bHasScreen)
	{
		return;
	}
	const FVector At = Where;
	if (Voice)
	{
		Voice->SetWorldLocation(At);
		Voice->Play(ETNBeachSfx::Slam, 0.8f / FMath::Sqrt(SizeK), 1.5f);
	}
	TNBeachKit::BurstAt(SandPuff, At + FVector(0.0, 0.0, 80.0), FVector::UpVector, 14);
	TNBeachKit::BurstAt(SandGrains, At + FVector(0.0, 0.0, 60.0), FVector::UpVector, 24);
	UTN_BeachCameraShake::Kick(this, At, 0.7f, 1200.f, 5000.f);
	ShowPop(NSLOCTEXT("TNBeach", "CrabCrash", "¡CATAPLÁN!"), FColor(255, 200, 80), At + FVector(0.0, 0.0, 350.0), 150.f);
}

void ATN_BeachGiantCrab::VisualTick(float DeltaSeconds)
{
	VisualClock += DeltaSeconds;
	FVector Delta = ShownLoc - LastShown;
	Delta.Z = 0.0;
	LastShown = ShownLoc;
	const float Dt = FMath::Max(DeltaSeconds, 1e-3f);
	const float Step = static_cast<float>(Delta.Size());
	// Velocidad que se ve (suavizada), en el mundo y en su marco (de lado = Y).
	ShownVel = FMath::Lerp(ShownVel, Delta / Dt, static_cast<double>(1.f - FMath::Exp(-Dt / 0.12f)));
	LocalVel = FRotator(0.f, ShownYaw, 0.f).UnrotateVector(ShownVel);
	const float Speed = static_cast<float>(ShownVel.Size2D());
	Moving = FMath::FInterpTo(Moving, FMath::Clamp(Speed / (300.f * SizeK), 0.f, 1.5f), DeltaSeconds, 6.f);
	// Las patas dan un ciclo por zancada recorrida: el pie apoyado acompaña al cuerpo (no patina).
	Gait += Step / (TNBeachCrab::Stride * SizeK);

	FVector View = ShownLoc;
	TNBeachKit::LocalCamera(GetWorld(), View);
	const bool bNear = ViewDistance < GetVisualRange();
	if (bNear)
	{
		PoseCrab(DeltaSeconds);
	}

	using TNBeachCrab::EState;
	const EState State = static_cast<EState>(GetMoverState());
	// Espuma en la boca: más cuanto más enfadado.
	if (Body && Foam.ISM.IsValid())
	{
		const TNBeachMeshes::FCrabRig R = TNBeachMeshes::CrabRig();
		const FTransform BodyXf = Body->GetComponentTransform();
		Foam.Origin = BodyXf.TransformPosition(R.Mouth);
		Foam.Desc.Direction = (BodyXf.GetUnitAxis(EAxis::X) + FVector(0.0, 0.0, 0.6)).GetSafeNormal();
		float Rate = 0.6f;
		switch (State)
		{
		case EState::Chase: Rate = 1.8f; break;
		case EState::WindUp: Rate = 3.f; break;
		case EState::Dazed: Rate = 2.5f; break;
		case EState::ChargePrep:
		case EState::Charge: Rate = 3.f; break;
		default: break;
		}
		Foam.RateScale = bNear ? Rate : 0.f;
	}
	TickDrag(DeltaSeconds, bNear);
	TNBeachKit::TickEmitterIfBusy(Foam, DeltaSeconds, View);
	TNBeachKit::TickEmitterIfBusy(SandPuff, DeltaSeconds, View);
	TNBeachKit::TickEmitterIfBusy(SandGrains, DeltaSeconds, View);
	TNBeachKit::TickEmitterIfBusy(DragSand, DeltaSeconds, View);

	// Sombra de la pinza durante el aviso: crece hasta el radio del golpe.
	if (State == EState::WindUp || State == EState::Slam)
	{
		const float A = State == EState::Slam ? 1.f : TNBeachCrab::Smooth01(GetStateAge() / TNBeachCrab::WindUpTime);
		TNBeachKit::PlaceShadow(ClawShadow, Mover.Aim, TNBeachCrab::HitRadius * SizeK * FMath::Lerp(0.35f, 1.f, A));
	}
	else
	{
		TNBeachKit::PlaceShadow(ClawShadow, FVector::ZeroVector, 0.f);
	}

	// Patitas al andar (más seguidas cuanto más deprisa) y chasquidos al agitar la pinza.
	if (Voice && bNear)
	{
		SkitterTimer -= DeltaSeconds * Moving;
		if (SkitterTimer <= 0.f && Moving > 0.2f)
		{
			SkitterTimer = 0.45f;
			Voice->SetRelativeLocation(FVector::ZeroVector);
			Voice->Play(ETNBeachSfx::Skitter, (0.9f + 0.2f * FMath::FRand()) / FMath::Sqrt(SizeK), 0.45f * FMath::Min(1.f, Moving));
		}
		if (State == EState::Idle)
		{
			ClackTimer -= DeltaSeconds;
			if (ClackTimer <= 0.f)
			{
				// Paradas cortas del recorrido: chasquidos seguidos de la pinza.
				ClackTimer = 0.28f + 0.25f * FMath::FRand();
				Voice->Play(ETNBeachSfx::Clack, 1.1f / FMath::Sqrt(SizeK), 0.6f);
			}
		}
	}
}

void ATN_BeachGiantCrab::TickDrag(float DeltaSeconds, bool bNear)
{
	using TNBeachCrab::EState;
	const EState State = static_cast<EState>(GetMoverState());
	const float Speed = static_cast<float>(ShownVel.Size2D());
	const double S = TNBeach::Scale * SizeK;
	// Hacia dónde va (o, parado, hacia su derecha) y el borde del cuerpo por ese lado.
	FVector Dir = Speed > 50.f ? ShownVel.GetSafeNormal2D() : FRotator(0.f, ShownYaw, 0.f).RotateVector(FVector::RightVector);
	Dir.Z = 0.0;
	// Arena que levantan las patas: al agacharse (poca), al embestir y, sobre todo, al derrapar.
	if (DragSand.ISM.IsValid())
	{
		float Rate = 0.f;
		switch (State)
		{
		case EState::ChargePrep: Rate = 1.2f; break;
		case EState::Charge: Rate = 2.5f; break;
		case EState::Skid: Rate = 6.f * FMath::Clamp(Speed / (600.f * SizeK), 0.3f, 1.f); break;
		default: break;
		}
		DragSand.Origin = ShownLoc + Dir * (TNBeachMeshes::CrabW * S * 0.95) + FVector(0.0, 0.0, 60.0);
		DragSand.Desc.Direction = (Dir + FVector(0.0, 0.0, 0.9)).GetSafeNormal();
		DragSand.RateScale = bNear ? Rate : 0.f;
	}

	// Surcos en la arena bajo las patas del lado hacia el que va (al derrapar y, más espaciados, al embestir).
	const bool bFurrows = bNear && (State == EState::Skid || State == EState::Charge) && Speed > 150.f;
	FurrowTimer -= DeltaSeconds;
	if (bFurrows && FurrowTimer <= 0.f)
	{
		FurrowTimer = State == EState::Skid ? 0.07f : 0.14f;
		const FRotator Facing(0.f, ShownYaw, 0.f);
		const float Lead = FVector::DotProduct(Dir, Facing.RotateVector(FVector::RightVector)) >= 0.0 ? 1.f : -1.f;
		const float Yaw = static_cast<float>(Dir.Rotation().Yaw);
		const float Length = FMath::Clamp(Speed * 0.12f, 80.f, 260.f) * SizeK;
		for (const double FrontBack : { 0.45, -0.45 })
		{
			const FVector Foot = ShownLoc + Facing.RotateVector(FVector(TNBeachMeshes::CrabD * S * FrontBack, Lead * TNBeachMeshes::CrabW * S * 1.02, 0.0));
			UStaticMeshComponent* Comp = nullptr;
			int32 Index = INDEX_NONE;
			if (Furrows.Num() < TNBeachCrab::MaxFurrows)
			{
				Comp = TNBeachKit::AddPart(this, GetRootComponent(), TNBeachKit::FurrowStrip(), FVector::ZeroVector, false);
				if (Comp)
				{
					Comp->SetAbsolute(true, true, true);
					Comp->SetTranslucentSortPriority(1);
				}
				Index = Furrows.Add(Comp);
				FurrowAge.Add(0.f);
			}
			else
			{
				Index = NextFurrow;
				NextFurrow = (NextFurrow + 1) % Furrows.Num();
				Comp = Furrows[Index];
			}
			if (!Comp)
			{
				continue;
			}
			FurrowAge[Index] = 0.f;
			Comp->SetWorldTransform(FTransform(FRotator(0.f, Yaw, 0.f), FVector(Foot.X, Foot.Y, ShownLoc.Z + 3.0),
				FVector(Length / 100.f, 1.4f * SizeK, 1.f)));
			Comp->SetVisibility(true);
			TNBeachKit::SetOpacity(TNBeachKit::SoftMID(Comp), 1.f);
		}
	}
	// Se borran solos: enteros un rato y luego se desvanecen.
	for (int32 i = 0; i < Furrows.Num(); ++i)
	{
		UStaticMeshComponent* Comp = Furrows[i];
		if (!Comp || !Comp->IsVisible())
		{
			continue;
		}
		FurrowAge[i] += DeltaSeconds;
		const float Left = TNBeachCrab::FurrowLife - FurrowAge[i];
		if (Left <= 0.f)
		{
			Comp->SetVisibility(false);
		}
		else if (Left < 1.5f)
		{
			TNBeachKit::SetOpacity(TNBeachKit::SoftMID(Comp), Left / 1.5f);
		}
	}
}

void ATN_BeachGiantCrab::PoseCrab(float DeltaSeconds)
{
	using TNBeachCrab::EState;
	const TNBeachMeshes::FCrabRig R = TNBeachMeshes::CrabRig();
	const EState State = static_cast<EState>(GetMoverState());
	const float T = VisualClock;
	const float Age = GetStateAge();
	const float Phase = Gait * 2.f * PI;
	const bool bDazed = State == EState::Dazed;
	const bool bPrep = State == EState::ChargePrep;
	const bool bCharge = State == EState::Charge;
	const bool bSkid = State == EState::Skid;
	const float Walk = FMath::Min(1.f, Moving);
	// Hacia qué costado anda (en su marco, +Y = su derecha) y cuánto; y hacia delante o atrás.
	const float Lateral = FMath::Clamp(static_cast<float>(LocalVel.Y) / (300.f * SizeK), -2.f, 2.f);
	const float Forward = FMath::Clamp(static_cast<float>(LocalVel.X) / (300.f * SizeK), -2.f, 2.f);
	const float MoveSign = FMath::Abs(Lateral) > 0.05f ? FMath::Sign(Lateral) : 0.f;

	// Cuerpo: se inclina hacia donde va (más al embestir; al derrapar, hacia atrás), se balancea de un lado a otro con cada
	// paso y bota dos veces por ciclo (una por grupo de patas).
	const float WantRoll = bSkid ? -MoveSign * 10.f : Lateral * (bCharge ? 7.f : 5.f);
	const float WantPitch = bSkid ? 0.f : -Forward * 4.f;
	LeanRoll = FMath::FInterpTo(LeanRoll, FMath::Clamp(WantRoll, -16.f, 16.f), DeltaSeconds, 5.f);
	LeanPitch = FMath::FInterpTo(LeanPitch, FMath::Clamp(WantPitch, -8.f, 8.f), DeltaSeconds, 5.f);
	float BodyBob = 6.f * FMath::Sin(T * 1.7f) + 12.f * Walk * FMath::Sin(Phase * 2.f);
	float BodyRoll = LeanRoll + 4.f * Walk * FMath::Sin(Phase);
	const float BodyPitch = LeanPitch;
	if (State == EState::WindUp)
	{
		BodyRoll += 2.f * FMath::Sin(T * 43.f);
	}
	if (bPrep)
	{
		// Agachado y temblando, a punto de salir disparado.
		BodyBob -= 45.f * FMath::Clamp(Age / 0.2f, 0.f, 1.f);
		BodyRoll += 2.5f * FMath::Sin(T * 38.f);
	}
	if (bDazed)
	{
		BodyRoll += 6.f * FMath::Sin(T * 3.f);
		BodyBob -= 25.f;
	}
	TNBeachKit::Pose(Body, FVector(0.0, 0.0, R.BodyZ + BodyBob), FRotator(BodyPitch, 0.f, BodyRoll));

	// Patas en dos grupos que se alternan: la 1 y la 3 de un lado con la 2 y la 4 del otro. En el aire, la pata se levanta;
	// apoyada, se estira o se encoge según el cuerpo se aleja o se acerca a su pie (y al revés en el aire), así el pie no
	// patina por la arena.
	const float LiftAmp = bCharge ? 24.f : 16.f;
	for (int32 k = 0; k < Legs.Num(); ++k)
	{
		const float Side = k < 4 ? -1.f : 1.f;
		const int32 InSide = k % 4;
		const bool bGroupA = (Side < 0.f) == (InSide % 2 == 0);
		const float P = Phase + (bGroupA ? 0.f : PI);
		float Lift = LiftAmp * Walk * FMath::Max(0.f, FMath::Sin(P));
		float Stretch = -FMath::Cos(P) * Side * MoveSign * 0.1f * Walk;
		float Swing = 4.f * Walk * FMath::Sin(P + static_cast<float>(InSide));
		if (bPrep)
		{
			// Abiertas y clavadas en la arena.
			Lift = -8.f;
			Stretch = 0.12f;
			Swing = 0.f;
		}
		else if (bSkid)
		{
			// Las del lado hacia el que va, clavadas y estiradas (frenan); las otras, en el aire.
			const bool bLeading = Side == MoveSign;
			Lift = bLeading ? -6.f : 10.f;
			Stretch = bLeading ? 0.16f : -0.05f;
			Swing = 0.f;
		}
		else if (bDazed)
		{
			Lift = 10.f * FMath::Sin(T * 9.f + static_cast<float>(k));
			Stretch = 0.f;
		}
		TNBeachKit::Pose(Legs[k], R.LegPivot[k], FRotator(0.f, R.LegSplay[k] + Swing, -Side * Lift), FVector(1.0, 1.0 + Stretch, 1.0));
	}

	// Ojos: se balancean; en el aviso y al embestir miran al frente; mareado, dan vueltas.
	for (int32 e = 0; e < Eyes.Num(); ++e)
	{
		const float Sign = e == 0 ? -1.f : 1.f;
		float Yaw = 8.f * FMath::Sin(T * 1.1f + e * 2.f);
		float Pitch = 6.f * FMath::Sin(T * 1.3f + e);
		if (State == EState::WindUp || State == EState::Chase || bPrep || bCharge)
		{
			Pitch -= 10.f;
			Yaw *= 0.3f;
		}
		if (bDazed)
		{
			Yaw = Sign * T * 540.f;
			Pitch = 15.f * FMath::Sin(T * 5.f);
		}
		const FVector Pivot = e == 0 ? R.EyeL : R.EyeR;
		TNBeachKit::Pose(Eyes[e], Pivot, FRotator(Pitch, Yaw, Sign * 6.f));
	}

	// Pinza grande: en reposo, agitándola, levantada temblando, cayendo, clavada, en guardia al embestir y volviendo.
	float ArmPitch = 8.f + 4.f * Walk * FMath::Sin(Phase);
	float ArmRoll = 0.f;
	float HandPitch = -10.f;
	float FingerOpen = 10.f;
	switch (State)
	{
	case EState::Idle:
		// Parada del recorrido: pinza en alto chasqueando deprisa.
		ArmPitch = 38.f + 12.f * FMath::Sin(T * 4.f);
		FingerOpen = 32.f * FMath::Abs(FMath::Sin(T * 11.f));
		break;
	case EState::Chase:
		FingerOpen = 10.f + 25.f * FMath::Abs(FMath::Sin(T * 6.f));
		break;
	case EState::WindUp:
	{
		const float A = FMath::Clamp(Age / TNBeachCrab::WindUpTime, 0.f, 1.f);
		ArmPitch = FMath::Lerp(8.f, TNBeachCrab::RaisePitch, TNBeachCrab::Smooth01(A * 2.2f)) + 3.5f * A * FMath::Sin(T * 47.f);
		ArmRoll = 3.f * A * FMath::Sin(T * 39.f);
		HandPitch = -25.f;
		FingerOpen = 40.f;
		break;
	}
	case EState::Slam:
	{
		const float U = FMath::Clamp(Age / TNBeachCrab::SlamTime, 0.f, 1.f);
		ArmPitch = FMath::Lerp(TNBeachCrab::RaisePitch, static_cast<float>(R.SlamPitch), U * U);
		HandPitch = FMath::Lerp(-25.f, 0.f, U);
		FingerOpen = FMath::Lerp(40.f, 0.f, U);
		break;
	}
	case EState::Recover:
	{
		const float Stuck = 0.7f;
		if (Age < Stuck)
		{
			ArmPitch = static_cast<float>(R.SlamPitch) + 2.f * FMath::Sin(T * 20.f);
			ArmRoll = 4.f * FMath::Sin(T * 17.f);
		}
		else
		{
			ArmPitch = FMath::Lerp(static_cast<float>(R.SlamPitch), 8.f, TNBeachCrab::Smooth01((Age - Stuck) / (TNBeachCrab::RecoverTime - Stuck)));
		}
		HandPitch = 0.f;
		FingerOpen = 5.f;
		break;
	}
	case EState::ChargePrep:
		// En guardia: pinza recogida delante, abriéndose.
		ArmPitch = 28.f + 2.f * FMath::Sin(T * 30.f);
		HandPitch = -20.f;
		FingerOpen = 20.f + 15.f * FMath::Clamp(Age / TNBeachCrab::ChargePrepTime, 0.f, 1.f);
		break;
	case EState::Charge:
		ArmPitch = 22.f + 3.f * FMath::Sin(T * 18.f);
		HandPitch = -15.f;
		FingerOpen = 35.f;
		break;
	case EState::Skid:
		ArmPitch = 4.f;
		HandPitch = 0.f;
		FingerOpen = 15.f;
		break;
	case EState::Dazed:
		ArmPitch = static_cast<float>(R.SlamPitch) * 0.6f + 3.f * FMath::Sin(T * 2.f);
		FingerOpen = 20.f;
		break;
	default:
		break;
	}
	TNBeachKit::Pose(BigArm, R.BigShoulder, FRotator(ArmPitch, static_cast<float>(R.ArmYaw), ArmRoll));
	TNBeachKit::Pose(BigHand, R.BigElbow, FRotator(HandPitch, 0.f, 0.f));
	TNBeachKit::Pose(BigFinger, R.BigKnuckle, FRotator(FingerOpen, 0.f, 0.f));

	// Pinza pequeña: se mueve sola, más nerviosa al perseguir.
	const float SmallRate = (State == EState::Chase || bCharge) ? 7.f : 2.3f;
	TNBeachKit::Pose(SmallClaw, R.SmallShoulder, FRotator(10.f + 12.f * FMath::Sin(T * SmallRate), 8.f, 0.f));
}
