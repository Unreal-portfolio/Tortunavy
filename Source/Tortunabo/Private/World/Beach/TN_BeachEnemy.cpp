#include "World/Beach/TN_BeachEnemy.h"
#include "World/Beach/TN_BeachCameraShake.h"
#include "World/Beach/TN_BeachEnemySynth.h"
#include "World/Beach/TN_BeachEnemyLod.h"
#include "World/Beach/TN_BeachStun.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_Log.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Net/UnrealNetwork.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_ShellBody.h"
#include "Player/TN_ShellComponent.h"
#include "Player/TN_ShellDecisions.h"
#include "Player/TN_TurtleAnimInstance.h"
#include "Player/TortugaCharacter.h"
#include "World/Beach/TN_BeachSandWorm.h"
#include "Game/TN_CoopItemComponent.h"
#include "World/Beach/TN_BeachShelterVolume.h"

namespace TNBeachEnemyDebug
{
	/** TN.Beach.Enemy.Debug: 1 dibuja en el servidor el estado de cada enemigo y sus radios. */
	static int32 DebugDraw = 0;
	static FAutoConsoleVariableRef CVarBeachEnemyDebug(TEXT("TN.Beach.Enemy.Debug"), DebugDraw,
		TEXT("1 = radios y estados de los enemigos de la playa (servidor)."), ECVF_Cheat);

	/** Diferencia con signo entre dos giros comprimidos a 16 bits. */
	inline int32 YawDelta(uint16 A, uint16 B)
	{
		return static_cast<int32>(static_cast<int16>(static_cast<uint16>(A - B)));
	}
}

namespace TNBeachEnemyShared
{
	/**
	 * Tortugas que lleva en el pico alguna gaviota (en cualquier mundo del proceso: en el PIE, las copias de cada máquina
	 * son objetos distintos, así que no se mezclan).
	 */
	TArray<TWeakObjectPtr<ATortugaCharacter>>& HeldTurtles()
	{
		static TArray<TWeakObjectPtr<ATortugaCharacter>> List;
		return List;
	}

	/** Enemigos vivos (todos los mundos del proceso; se filtra por mundo al usarlos). */
	TArray<TWeakObjectPtr<ATN_BeachEnemy>>& AllEnemies()
	{
		static TArray<TWeakObjectPtr<ATN_BeachEnemy>> List;
		return List;
	}

	/** Velocidad mínima hacia abajo que se pasa al derribo: el empujón de verdad va al ragdoll (ver ServerKnockDown). */
	constexpr float KnockSettle = 60.f;

	/**
	 * Seguro de la sujeción: una tortuga soltada se vigila estos segundos; una sujeción que dura más de MaxHoldSeconds se
	 * suelta sola y esa tortuga no se puede volver a sujetar en HoldBlockSeconds.
	 */
	constexpr float ReleaseWatchSeconds = 3.f;
	constexpr double MaxHoldSeconds = 6.0;
	constexpr double HoldBlockSeconds = 2.0;

	/**
	 * Soltada ya en su caparazón según el servidor, pero su caja aún no ha llegado a esta máquina: estos segundos se la deja
	 * esperando la caja en vez de hacerla caer por su cuenta (si no, un cliente caería con su movimiento mientras el servidor
	 * la tiene en bola y le corrige al modo sin movimiento: bola y caída a la vez). Si la caja no llega, cae.
	 */
	constexpr float BallArrivalGrace = 0.6f;

	/**
	 * Si un enemigo puede colocar ahora a Turtle en esta máquina (ATN_BeachEnemy::CanHoldTurtle con lo que se ve aquí): ni
	 * en su caparazón, ni con su caja enganchada, ni en ragdoll, ni en brazos de otra, ni muerta.
	 */
	bool CanHoldHere(const ATortugaCharacter* Turtle)
	{
		if (!Turtle)
		{
			return false;
		}
		const UTN_ShellComponent* Shell = Turtle->GetShellComponent();
		const USkeletalMeshComponent* Mesh = Turtle->GetMesh();
		const UTN_CarryComponent* Carry = Turtle->GetCarryComponent();
		return ATN_BeachEnemy::CanHoldTurtle(Turtle->IsInShell(), Shell && Shell->HasLocalBody(), Mesh && Mesh->IsSimulatingPhysics(),
			Carry && Carry->IsBeingCarried(), Turtle->IsDead());
	}

	/** Revisión del nivel de detalle (s); los ritmos de cada nivel y el tope están en TN_BeachEnemyLod.h. */
	constexpr float LodPeriod = 0.5f;


	/**
	 * Distancia al cuadrado entre los segmentos P0-P1 y Q0-Q1 (Ericson, «Real-Time Collision Detection»). OutS es el
	 * parámetro (0-1) del punto más cercano sobre P y OutOnQ, el punto más cercano sobre Q.
	 */
	double SegmentDistSq(const FVector& P0, const FVector& P1, const FVector& Q0, const FVector& Q1, double& OutS, FVector& OutOnQ)
	{
		const FVector D1 = P1 - P0;
		const FVector D2 = Q1 - Q0;
		const FVector R = P0 - Q0;
		const double A = FVector::DotProduct(D1, D1);
		const double E = FVector::DotProduct(D2, D2);
		const double F = FVector::DotProduct(D2, R);
		constexpr double Tiny = 1.0e-6;
		double S = 0.0;
		double T = 0.0;
		if (A <= Tiny && E <= Tiny)
		{
			OutS = 0.0;
			OutOnQ = Q0;
			return R.SizeSquared();
		}
		if (A <= Tiny)
		{
			T = FMath::Clamp(F / E, 0.0, 1.0);
		}
		else
		{
			const double C = FVector::DotProduct(D1, R);
			if (E <= Tiny)
			{
				S = FMath::Clamp(-C / A, 0.0, 1.0);
			}
			else
			{
				const double B = FVector::DotProduct(D1, D2);
				const double Denom = A * E - B * B;
				S = Denom > Tiny * A * E ? FMath::Clamp((B * F - C * E) / Denom, 0.0, 1.0) : 0.0;
				T = (B * S + F) / E;
				if (T < 0.0)
				{
					T = 0.0;
					S = FMath::Clamp(-C / A, 0.0, 1.0);
				}
				else if (T > 1.0)
				{
					T = 1.0;
					S = FMath::Clamp((B - C) / A, 0.0, 1.0);
				}
			}
		}
		OutS = S;
		OutOnQ = Q0 + D2 * T;
		return FVector::DistSquared(P0 + D1 * S, OutOnQ);
	}

	/**
	 * Servidor, una vez por fotograma y mundo: las bolas de caparazón que van deprisa (una tortuga lanzada, o en bola
	 * rodando a toda velocidad) marean al enemigo al que dan, como un objeto lanzado. Así no hay que tocar la bola.
	 */
	void ScanThrownShells(UWorld* World)
	{
		static TWeakObjectPtr<UWorld> ScanWorld;
		static uint64 ScanFrame = 0;
		if (!World || (ScanFrame == GFrameCounter && ScanWorld.Get() == World))
		{
			return;
		}
		ScanFrame = GFrameCounter;
		ScanWorld = World;
		const float Dt = FMath::Clamp(World->GetDeltaSeconds(), 0.005f, 0.05f);
		for (TActorIterator<ATN_ShellBody> It(World); It; ++It)
		{
			ATN_ShellBody* Body = *It;
			UBoxComponent* Box = Body ? Body->GetBox() : nullptr;
			if (!Box || !Box->IsSimulatingPhysics())
			{
				continue;
			}
			const FVector Vel = Box->GetPhysicsLinearVelocity();
			if (Vel.SizeSquared() < FMath::Square(static_cast<double>(TNBeachHitStun::ShellMinSpeed)))
			{
				continue;
			}
			const FVector At = Box->GetComponentLocation();
			AActor* Thrower = Body->GetTurtle() ? static_cast<AActor*>(Body->GetTurtle()) : static_cast<AActor*>(Body);
			ATN_BeachEnemy::ServerHitWithProjectile(Thrower, At - Vel * Dt, At, 40.f, TNBeachHitStun::ShellSeconds);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Empujones del ragdoll
// ─────────────────────────────────────────────────────────────────────────────

bool FTNBeachRagdollPushes::TryApply(ACharacter* Turtle, const FVector& Push, const FVector& Spin)
{
	USkeletalMeshComponent* Mesh = Turtle ? Turtle->GetMesh() : nullptr;
	if (!Mesh || !Mesh->IsSimulatingPhysics())
	{
		return false;
	}
	Mesh->SetAllPhysicsLinearVelocity(Push);
	if (!Spin.IsNearlyZero())
	{
		Mesh->SetAllPhysicsAngularVelocityInRadians(FVector(FMath::DegreesToRadians(Spin.X), FMath::DegreesToRadians(Spin.Y), FMath::DegreesToRadians(Spin.Z)));
	}
	Mesh->WakeAllRigidBodies();
	return true;
}

void FTNBeachRagdollPushes::Add(ACharacter* Turtle, const FVector& Push, const FVector& Spin)
{
	if (!Turtle || TryApply(Turtle, Push, Spin))
	{
		return;
	}
	FItem& Item = Items.AddDefaulted_GetRef();
	Item.Turtle = Turtle;
	Item.Push = Push;
	Item.Spin = Spin;
	Item.Left = 1.f;
}

void FTNBeachRagdollPushes::Tick(float DeltaSeconds)
{
	for (int32 i = Items.Num() - 1; i >= 0; --i)
	{
		FItem& Item = Items[i];
		Item.Left -= DeltaSeconds;
		ACharacter* Turtle = Item.Turtle.Get();
		if (!Turtle || Item.Left <= 0.f || TryApply(Turtle, Item.Push, Item.Spin))
		{
			Items.RemoveAtSwap(i);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// ATN_BeachEnemy
// ─────────────────────────────────────────────────────────────────────────────

ATN_BeachEnemy::ATN_BeachEnemy()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	// Siempre relevantes: con la distancia por defecto (150 m) un cliente destruiría el enemigo (y rehará sus mallas)
	// cada vez que se aleje por la playa. Solo se manda lo que cambia, a 10 Hz como mucho.
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(10.f);
	SetMinNetUpdateFrequency(2.f);
}

void ATN_BeachEnemy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_BeachEnemy, Mover);
	DOREPLIFETIME(ATN_BeachEnemy, HitStunEndTime);
}

void ATN_BeachEnemy::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// Antes de las primeras réplicas y de BeginPlay (ApplySpec puede llegar antes que BeginPlay en un cliente).
	bHasScreen = GetNetMode() != NM_DedicatedServer;
	Home = GetActorLocation();
	SimLoc = Home;
	SimYaw = static_cast<float>(GetActorRotation().Yaw);
	if (!bHasRep)
	{
		ShownLoc = Home;
		ShownYaw = SimYaw;
	}
}

void ATN_BeachEnemy::BeginPlay()
{
	if (HasAuthority())
	{
		ServerRng.Initialize(Spec.Seed ^ 0x5EED1234);
		Mover.Location = SimLoc;
		Mover.Yaw = FRotator::CompressAxisToShort(SimYaw);
		Mover.StateTime = static_cast<float>(ServerNow(this));
		SetNetUpdateFrequency(NetFrequencyNear);
	}
	TNBeachEnemyShared::AllEnemies().Add(this);
	// Cada uno revisa su nivel de detalle en un momento distinto (no todos en el mismo fotograma).
	LodTimer = TNBeachEnemyShared::LodPeriod * static_cast<float>((static_cast<uint32>(Spec.Seed) * 2654435761u) % 1000u) / 1000.f;
	Super::BeginPlay();
}

void ATN_BeachEnemy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	EndHoldTurtle();
	IgnoreUntil.Reset();
	TArray<TWeakObjectPtr<ATN_BeachEnemy>>& All = TNBeachEnemyShared::AllEnemies();
	for (int32 i = All.Num() - 1; i >= 0; --i)
	{
		if (!All[i].IsValid() || All[i].Get() == this)
		{
			All.RemoveAtSwap(i);
		}
	}
	Super::EndPlay(EndPlayReason);
}

bool ATN_BeachEnemy::IsTurtleHeld(const ATortugaCharacter* Turtle)
{
	if (!Turtle)
	{
		return false;
	}
	for (const TWeakObjectPtr<ATortugaCharacter>& Held : TNBeachEnemyShared::HeldTurtles())
	{
		if (Held.Get() == Turtle)
		{
			return true;
		}
	}
	return false;
}

void ATN_BeachEnemy::SetTurtleHeld(ATortugaCharacter* Turtle, bool bHeld)
{
	TArray<TWeakObjectPtr<ATortugaCharacter>>& List = TNBeachEnemyShared::HeldTurtles();
	for (int32 i = List.Num() - 1; i >= 0; --i)
	{
		if (!List[i].IsValid() || List[i].Get() == Turtle)
		{
			List.RemoveAtSwap(i);
		}
	}
	if (bHeld && Turtle)
	{
		List.Add(Turtle);
	}
}

ATN_BeachEnemy* ATN_BeachEnemy::FindHolder(const ATortugaCharacter* Turtle)
{
	if (!Turtle)
	{
		return nullptr;
	}
	for (const TWeakObjectPtr<ATN_BeachEnemy>& Weak : TNBeachEnemyShared::AllEnemies())
	{
		ATN_BeachEnemy* Enemy = Weak.Get();
		if (Enemy && Enemy->GetWorld() == Turtle->GetWorld() && Enemy->HeldTurtle.Get() == Turtle)
		{
			return Enemy;
		}
	}
	return nullptr;
}

bool ATN_BeachEnemy::IsTurtleCarriedThroughAir(const ATortugaCharacter* Turtle)
{
	// La lista de llevadas es corta: solo se busca quién la sujeta si está en ella.
	if (!IsTurtleHeld(Turtle))
	{
		return false;
	}
	const ATN_BeachEnemy* Holder = FindHolder(Turtle);
	return Holder && Holder->CarriesHeldTurtleThroughAir();
}

bool ATN_BeachEnemy::ServerReleaseHeldTurtle(ATortugaCharacter* Turtle, const TCHAR* Reason)
{
	if (!Turtle || !Turtle->HasAuthority())
	{
		return false;
	}
	ATN_BeachEnemy* Holder = FindHolder(Turtle);
	if (!Holder)
	{
		return false;
	}
	// Como el seguro de tiempo de PlaceHeldTurtle: suelta ya y no la vuelve a coger en un momento (que caiga de verdad).
	const UWorld* World = Holder->GetWorld();
	Holder->HoldBlocked = Turtle;
	Holder->HoldBlockedUntil = (World ? World->GetTimeSeconds() : 0.0) + TNBeachEnemyShared::HoldBlockSeconds;
	// Que deje el ataque (sin lanzarla desde donde la ponen ni volver a cogerla) antes de soltarla.
	Holder->OnHoldAborted(Turtle);
	Holder->EndHoldTurtle();
	UE_LOG(LogTortunabo, Warning, TEXT("[Playa] %s suelta a %s (%s)."), *Holder->GetName(), *Turtle->GetName(), Reason ? Reason : TEXT("sin motivo"));
	return true;
}

bool ATN_BeachEnemy::ServerSlipHeldTurtle(ATortugaCharacter* Turtle)
{
	if (!Turtle || !Turtle->HasAuthority())
	{
		return false;
	}
	ATN_BeachEnemy* Holder = FindHolder(Turtle);
	if (!Holder)
	{
		return false;
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Playa] A %s se le escurre %s: se mete en su caparazón."), *Holder->GetName(), *Turtle->GetName());
	Holder->OnHeldTurtleSlips(Turtle);
	// Pase lo que pase en la subclase, suelta ya: la bola no nace con nadie sujetándola.
	if (Holder->GetHeldTurtle() == Turtle)
	{
		Holder->EndHoldTurtle();
	}
	return true;
}

void ATN_BeachEnemy::OnHeldTurtleSlips(ATortugaCharacter* Turtle)
{
	if (GetHeldTurtle() == Turtle)
	{
		EndHoldTurtle();
	}
}

bool ATN_BeachEnemy::CanBeHit(const ATortugaCharacter* Turtle)
{
	// Tampoco mientras la patada de la tormenta la recoloca (TNBeach::IsTurtleRelocating), ni protegida por el pez globo.
	return IsValid(Turtle) && !Turtle->IsDead() && !Turtle->IsKnockedDown() && !TNBeach::IsTurtleStunned(Turtle) && !IsTurtleHeld(Turtle)
		&& !TNBeach::IsTurtleRelocating(Turtle) && !UTN_CoopItemComponent::IsTurtleProtected(Turtle)
		// Dentro de un búnker (#689): zona segura, ningún enemigo la marca ni la agarra.
		&& !ATN_BeachShelterVolume::IsSheltered(Turtle);
}

bool ATN_BeachEnemy::ServerKnockDown(ATortugaCharacter* Turtle, float Seconds, const FVector& Push, const FVector& Spin)
{
	if (!IsValid(Turtle) || !Turtle->HasAuthority() || Turtle->IsDead())
	{
		return false;
	}
	// Al derribo solo se le pasa un pelo hacia abajo: lo que se le pasa lo lanza la cápsula al levantarse (queda pendiente
	// en el movimiento mientras dura el ragdoll). El empujón de verdad va a los cuerpos del ragdoll.
	TNBeach::KnockDownTurtle(Turtle, Seconds, FVector(0.0, 0.0, -TNBeachEnemyShared::KnockSettle));
	FTNBeachRagdollPushes::TryApply(Turtle, Push, Spin);
	return Turtle->IsKnockedDown();
}

void ATN_BeachEnemy::GatherStats(const UObject* WorldContext, int32& OutTotal, int32& OutThrottled, int32& OutMovers)
{
	OutTotal = 0;
	OutThrottled = 0;
	OutMovers = 0;
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	for (const TWeakObjectPtr<ATN_BeachEnemy>& Weak : TNBeachEnemyShared::AllEnemies())
	{
		const ATN_BeachEnemy* Enemy = Weak.Get();
		if (!Enemy || Enemy->GetWorld() != World)
		{
			continue;
		}
		++OutTotal;
		OutThrottled += Enemy->bThrottled ? 1 : 0;
		OutMovers += (Enemy->bUsesMover && Enemy->GetBodyRadius() > 0.f) ? 1 : 0;
	}
}

bool ATN_BeachEnemy::IsDebugDraw()
{
	return TNBeachEnemyDebug::DebugDraw != 0;
}

double ATN_BeachEnemy::ServerNow(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return 0.0;
	}
	if (const AGameStateBase* GS = World->GetGameState())
	{
		return GS->GetServerWorldTimeSeconds();
	}
	return World->GetTimeSeconds();
}

void ATN_BeachEnemy::GatherTurtles(const UObject* WorldContext, TArray<ATortugaCharacter*>& Out)
{
	Out.Reset();
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
	if (!GS)
	{
		return;
	}
	for (APlayerState* PS : GS->PlayerArray)
	{
		ATortugaCharacter* Turtle = PS ? Cast<ATortugaCharacter>(PS->GetPawn()) : nullptr;
		if (!IsValid(Turtle) || Turtle->IsDead() || Turtle->IsActorBeingDestroyed())
		{
			continue;
		}
		if (const ATN_CoopPlayerState* Coop = Cast<ATN_CoopPlayerState>(PS))
		{
			if (!Coop->IsAliveAndPlaying() || Coop->bHasFinishedRun)
			{
				continue;
			}
		}
		Out.Add(Turtle);
	}
}


bool ATN_BeachEnemy::TraceGround(const UObject* WorldContext, const FVector& Where, float& OutZ, FVector* OutNormal, float Up, float Down)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BeachEnemyGround), false);
	const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	if (World->LineTraceSingleByObjectType(Hit, Where + FVector(0.0, 0.0, Up), Where - FVector(0.0, 0.0, Down), Objects, Params))
	{
		OutZ = static_cast<float>(Hit.ImpactPoint.Z);
		if (OutNormal)
		{
			*OutNormal = Hit.ImpactNormal;
		}
		return true;
	}
	return false;
}

bool ATN_BeachEnemy::TraceDropSurface(const UObject* WorldContext, const FVector& Where, float& OutZ, FVector* OutNormal, const AActor* Ignore,
	float Up, float Down)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}
	// Por el canal de visibilidad: lo que se ve (la arena, el decorado y las fortalezas, que son colisión dinámica y una traza
	// de solo lo estático cruzaba), sin los muros invisibles ni los volúmenes. Las tortugas y sus bolas no paran lo que cae
	// sobre ellas: lo que importa es dónde están de pie.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BeachDropSurface), false, Ignore);
	TArray<ATortugaCharacter*> Turtles;
	GatherTurtles(WorldContext, Turtles);
	for (const ATortugaCharacter* Turtle : Turtles)
	{
		Params.AddIgnoredActor(Turtle);
		if (const UTN_ShellComponent* Shell = Turtle->GetShellComponent())
		{
			Params.AddIgnoredActor(Shell->GetBody());
		}
	}
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Where + FVector(0.0, 0.0, Up), Where - FVector(0.0, 0.0, Down), ECC_Visibility, Params) || Hit.bStartPenetrating)
	{
		return false;
	}
	OutZ = static_cast<float>(Hit.ImpactPoint.Z);
	if (OutNormal)
	{
		*OutNormal = Hit.ImpactNormal;
	}
	return true;
}

float ATN_BeachEnemy::LocalViewDistance(const UObject* WorldContext, const FVector& Where)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return 1.0e9f;
	}
	float Best = 1.0e9f;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalController() || !PC->PlayerCameraManager)
		{
			continue;
		}
		Best = FMath::Min(Best, static_cast<float>(FVector::Dist(PC->PlayerCameraManager->GetCameraLocation(), Where)));
	}
	return Best;
}

void ATN_BeachEnemy::OnRep_Mover()
{
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const FVector NewLoc = Mover.Location;
	if (!bHasRep)
	{
		// Primera réplica: se coloca sin más y sin efectos de cambio de estado (quien entra a mitad no oye golpes viejos).
		bHasRep = true;
		ShownLoc = NewLoc;
		ShownYaw = static_cast<float>(FRotator::DecompressAxisFromShort(Mover.Yaw));
		RepVelocity = FVector::ZeroVector;
		LastRepLoc = NewLoc;
		LastRepTime = Now;
		LastSerial = Mover.Serial;
		LastState = Mover.State;
		return;
	}
	if (!NewLoc.Equals(LastRepLoc, 0.5))
	{
		const double Dt = Now - LastRepTime;
		if (FVector::DistSquared(NewLoc, LastRepLoc) > FMath::Square(4000.0))
		{
			// Salto (reaparición, teletransporte): sin interpolar.
			ShownLoc = NewLoc;
			RepVelocity = FVector::ZeroVector;
		}
		else if (Dt > 0.02)
		{
			RepVelocity = ((NewLoc - LastRepLoc) / Dt).GetClampedToMaxSize(4500.0);
		}
		LastRepLoc = NewLoc;
		LastRepTime = Now;
	}
	if (Mover.Serial != LastSerial)
	{
		const uint8 OldState = LastState;
		LastSerial = Mover.Serial;
		LastState = Mover.State;
		OnMoverStateChanged(OldState);
	}
}

void ATN_BeachEnemy::ServerMoveTo(const FVector& Location, float YawDeg)
{
	SimLoc = Location;
	SimYaw = static_cast<float>(FRotator::NormalizeAxis(YawDeg));
	if (FVector::DistSquared(FVector(Mover.Location), Location) > 4.0)
	{
		Mover.Location = Location;
	}
	const uint16 NewYaw = FRotator::CompressAxisToShort(SimYaw);
	// Medio grado de umbral (65536 / 720): el giro no manda nada mientras no cambie de verdad.
	if (FMath::Abs(TNBeachEnemyDebug::YawDelta(NewYaw, Mover.Yaw)) > 91)
	{
		Mover.Yaw = NewYaw;
	}
}

void ATN_BeachEnemy::ServerSetState(uint8 NewState, const FVector& Aim)
{
	const uint8 OldState = Mover.State;
	Mover.State = NewState;
	Mover.Aim = Aim;
	++Mover.Serial;
	Mover.StateTime = static_cast<float>(ServerNow(this));
	LastSerial = Mover.Serial;
	LastState = NewState;
	ForceNetUpdate();
	OnMoverStateChanged(OldState);
}

void ATN_BeachEnemy::ServerSetAim(const FVector& Aim)
{
	Mover.Aim = Aim;
}

float ATN_BeachEnemy::GetStateAge() const
{
	return FMath::Max(0.f, static_cast<float>(ServerNow(this) - static_cast<double>(Mover.StateTime)));
}

USceneComponent* ATN_BeachEnemy::MakeRig()
{
	if (Rig)
	{
		return Rig;
	}
	Rig = NewObject<USceneComponent>(this, TEXT("Rig"));
	Rig->SetMobility(EComponentMobility::Movable);
	Rig->SetupAttachment(GetRootComponent());
	Rig->SetAbsolute(true, true, false);
	Rig->RegisterComponent();
	Rig->SetWorldLocationAndRotation(ShownLoc, FRotator(0.f, ShownYaw, 0.f));
	return Rig;
}

UTN_BeachEnemySynthComponent* ATN_BeachEnemy::GetVoice(USceneComponent* Parent, float InnerRadius, float Falloff)
{
	if (!Voice && !bVoiceTried && bHasScreen)
	{
		bVoiceTried = true;
		Voice = UTN_BeachEnemySynthComponent::AttachTo(this, Parent, InnerRadius, Falloff);
	}
	return Voice;
}

void ATN_BeachEnemy::StunTurtle(ATortugaCharacter* Turtle, float Seconds, const FVector& Launch)
{
	if (!HasAuthority() || !IsValid(Turtle))
	{
		return;
	}
	TNBeach::StunTurtle(Turtle, Seconds, Launch);
	UE_LOG(LogTortunabo, Log, TEXT("[Playa] %s aturde a %s %.1f s."), *GetName(), *GetNameSafe(Turtle), Seconds);
}

void ATN_BeachEnemy::KnockDownTurtle(ATortugaCharacter* Turtle, float Seconds, const FVector& Push, const FVector& Spin)
{
	if (!HasAuthority() || !ServerKnockDown(Turtle, Seconds, Push, Spin))
	{
		return;
	}
	MulticastRagdollPush(Turtle, Push, Spin);
	UE_LOG(LogTortunabo, Log, TEXT("[Playa] %s derriba a %s %.1f s (empujón %.0f cm/s)."), *GetName(), *GetNameSafe(Turtle), Seconds, Push.Size());
}

void ATN_BeachEnemy::MulticastRagdollPush_Implementation(ACharacter* Turtle, FVector_NetQuantize10 Push, FVector_NetQuantize10 Spin)
{
	// En el servidor ya se ha dado (ServerKnockDown); en los clientes, en cuanto su ragdoll simule.
	if (!HasAuthority() && Turtle)
	{
		RagdollPushes.Add(Turtle, Push, Spin);
	}
}

void ATN_BeachEnemy::IgnoreTurtle(ATortugaCharacter* Turtle, float Seconds)
{
	if (!Turtle || !GetWorld())
	{
		return;
	}
	IgnoreUntil.Add(Turtle, GetWorld()->GetTimeSeconds() + Seconds);
}

bool ATN_BeachEnemy::IsIgnored(const ATortugaCharacter* Turtle) const
{
	const UWorld* World = GetWorld();
	if (!Turtle || !World)
	{
		return false;
	}
	for (const TPair<TWeakObjectPtr<ATortugaCharacter>, double>& Pair : IgnoreUntil)
	{
		if (Pair.Key.Get() == Turtle)
		{
			return World->GetTimeSeconds() < Pair.Value;
		}
	}
	return false;
}

bool ATN_BeachEnemy::IsTargetable(const ATortugaCharacter* Turtle) const
{
	return CanBeHit(Turtle) && !IsIgnored(Turtle);
}

ATortugaCharacter* ATN_BeachEnemy::FindTarget(const FVector& From, float MaxDist, const FVector& InHome, float Leash) const
{
	TArray<ATortugaCharacter*> Turtles;
	GatherTurtles(this, Turtles);
	ATortugaCharacter* Best = nullptr;
	double BestSq = FMath::Square(static_cast<double>(MaxDist));
	for (ATortugaCharacter* Turtle : Turtles)
	{
		if (!IsTargetable(Turtle))
		{
			continue;
		}
		const FVector At = Turtle->GetActorLocation();
		if (Leash > 0.f && FVector::Dist2D(At, InHome) > Leash)
		{
			continue;
		}
		const double DistSq = FVector::DistSquared2D(At, From);
		if (DistSq < BestSq)
		{
			BestSq = DistSq;
			Best = Turtle;
		}
	}
	return Best;
}

// ─────────────────────────────────────────────────────────────────────────────
// Muchos enemigos: apartarse, suelo y nivel de detalle
// ─────────────────────────────────────────────────────────────────────────────


bool ATN_BeachEnemy::GroundHeightAt(const FVector& Where, float& OutZ) const
{
	return TraceGround(this, Where, OutZ);
}

FVector ATN_BeachEnemy::ResolveStep(const FVector& Next, float SelfRadius)
{
	FVector2D P(Next.X, Next.Y);
	// Otros enemigos que andan: cada uno se aparta la mitad del solape (el otro hace lo mismo en su paso).
	if (SelfRadius > 0.f)
	{
		const UWorld* World = GetWorld();
		for (const TWeakObjectPtr<ATN_BeachEnemy>& Weak : TNBeachEnemyShared::AllEnemies())
		{
			const ATN_BeachEnemy* Other = Weak.Get();
			if (!Other || Other == this || Other->GetWorld() != World || !Other->bUsesMover)
			{
				continue;
			}
			const float OtherRadius = Other->GetBodyRadius();
			if (OtherRadius <= 0.f)
			{
				continue;
			}
			const FVector2D Delta = P - FVector2D(Other->SimLoc.X, Other->SimLoc.Y);
			const double MinDist = static_cast<double>(SelfRadius + OtherRadius);
			const double DistSq = Delta.SizeSquared();
			if (DistSq >= MinDist * MinDist)
			{
				continue;
			}
			const double Dist = FMath::Sqrt(DistSq);
			// Encima del todo: cada uno hacia un lado, siempre el mismo.
			const FVector2D Dir = Dist > 1.0 ? Delta / Dist : FVector2D(this < Other ? 1.0 : -1.0, 0.0);
			P += Dir * ((MinDist - Dist) * 0.5);
		}
	}
	return FVector(P.X, P.Y, Next.Z);
}

void ATN_BeachEnemy::ShowPop(const FText& Text, const FColor& Color, const FVector& WorldAt, float Size)
{
	if (!bHasScreen || LocalViewDistance(this, WorldAt) > 6000.f)
	{
		return;
	}
	Pops[NextPop].Show(this, Text, Color, WorldAt, Size);
	NextPop = (NextPop + 1) % UE_ARRAY_COUNT(Pops);
	bPopsLive = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Mareo por lo que se le lanza
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachEnemy::ApplyHitStun(float Seconds, AActor* InstigatorActor)
{
	if (!HasAuthority() || Seconds <= 0.f || !AcceptsHitStun() || IsActorBeingDestroyed())
	{
		return;
	}
	const double Now = ServerNow(this);
	// El mismo objeto (o la misma bola) que sigue tocándolo unos fotogramas no lo vuelve a marear cada vez.
	if (InstigatorActor && LastHitInstigator.Get() == InstigatorActor && Now - LastHitTime < 0.6)
	{
		return;
	}
	LastHitInstigator = InstigatorActor;
	LastHitTime = Now;
	HitStunEndTime = FMath::Max(HitStunEndTime, static_cast<float>(Now) + Seconds);
	ForceNetUpdate();
	FVector A;
	FVector B;
	float Radius = 0.f;
	const FVector Where = GetHitCapsule(A, B, Radius) ? (A + B) * 0.5 : GetHitStunAnchor();
	MulticastHitStunFX(Where);
	UE_LOG(LogTortunabo, Log, TEXT("[Playa] %s mareado %.1f s por %s."), *GetName(), Seconds, *GetNameSafe(InstigatorActor));
}

bool ATN_BeachEnemy::IsHitStunned() const
{
	return HitStunEndTime > 0.f && ServerNow(this) < static_cast<double>(HitStunEndTime);
}

float ATN_BeachEnemy::GetHitStunLeft() const
{
	return HitStunEndTime > 0.f ? FMath::Max(0.f, static_cast<float>(static_cast<double>(HitStunEndTime) - ServerNow(this))) : 0.f;
}

bool ATN_BeachEnemy::GetHitCapsule(FVector& OutA, FVector& OutB, float& OutRadius) const
{
	const float Radius = GetBodyRadius();
	if (!bUsesMover || Radius <= 0.f)
	{
		return false;
	}
	OutA = ShownLoc + FVector(0.0, 0.0, Radius);
	OutB = OutA;
	OutRadius = Radius;
	return true;
}

FVector ATN_BeachEnemy::GetHitStunAnchor() const
{
	const FVector Base = bUsesMover ? ShownLoc : GetActorLocation();
	return Base + FVector(0.0, 0.0, GetBodyRadius() * 1.5f + 150.f);
}

float ATN_BeachEnemy::GetHitStunScale() const
{
	return FMath::Clamp(GetBodyRadius() / 70.f, 2.f, 9.f);
}

ATN_BeachEnemy* ATN_BeachEnemy::FindProjectileHit(const UObject* WorldContext, const FVector& From, const FVector& To, float Radius, FVector* OutAxisPoint)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	ATN_BeachEnemy* Best = nullptr;
	double BestS = 2.0;
	for (const TWeakObjectPtr<ATN_BeachEnemy>& Weak : TNBeachEnemyShared::AllEnemies())
	{
		ATN_BeachEnemy* Enemy = Weak.Get();
		if (!Enemy || Enemy->GetWorld() != World || !Enemy->AcceptsHitStun() || Enemy->IsActorBeingDestroyed())
		{
			continue;
		}
		FVector A;
		FVector B;
		float BodyRadius = 0.f;
		if (!Enemy->GetHitCapsule(A, B, BodyRadius))
		{
			continue;
		}
		// Descarte rápido: lejos del tramo del todo.
		const double Reach = static_cast<double>(BodyRadius + Radius);
		const FVector Mid = (From + To) * 0.5;
		const double Span = FVector::Dist(From, To) * 0.5 + FVector::Dist(A, B) * 0.5 + Reach;
		if (FVector::DistSquared(Mid, (A + B) * 0.5) > Span * Span)
		{
			continue;
		}
		double S = 0.0;
		FVector OnAxis;
		if (TNBeachEnemyShared::SegmentDistSq(From, To, A, B, S, OnAxis) < Reach * Reach && S < BestS)
		{
			BestS = S;
			Best = Enemy;
			if (OutAxisPoint)
			{
				*OutAxisPoint = OnAxis;
			}
		}
	}
	return Best;
}

ATN_BeachEnemy* ATN_BeachEnemy::ServerHitWithProjectile(AActor* Projectile, const FVector& From, const FVector& To, float Radius, float Seconds)
{
	if (!Projectile || !Projectile->HasAuthority())
	{
		return nullptr;
	}
	ATN_BeachEnemy* Hit = FindProjectileHit(Projectile, From, To, Radius);
	if (Hit)
	{
		Hit->ApplyHitStun(Seconds, Projectile);
	}
	return Hit;
}

void ATN_BeachEnemy::MulticastHitStunFX_Implementation(FVector_NetQuantize Where)
{
	if (!bHasScreen)
	{
		return;
	}
	const FVector At = Where;
	if (Voice)
	{
		// Golpe seco y un chasquido agudo: «¡TOING!».
		Voice->SetWorldLocation(At);
		Voice->Play(ETNBeachSfx::Slam, 1.9f, 0.75f);
		Voice->Play(ETNBeachSfx::Clack, 1.5f, 1.f);
	}
	UTN_BeachCameraShake::Kick(this, At, 0.3f, 600.f, 3000.f);
	ShowPop(NSLOCTEXT("TNBeach", "EnemyBonk", "¡TOING!"), FColor(255, 230, 90), At + FVector(0.0, 0.0, 250.0), 150.f);
}

void ATN_BeachEnemy::UpdateHitStunVisual()
{
	const bool bStunned = IsHitStunned();
	if (bStunned == bDizzyShown)
	{
		return;
	}
	bDizzyShown = bStunned;
	if (bStunned && !Dizzy)
	{
		Dizzy = NewObject<UTN_BeachEnemyDizzyComponent>(this, NAME_None, RF_Transient);
		Dizzy->SetupAttachment(GetRootComponent());
		Dizzy->RegisterComponent();
	}
	if (Dizzy)
	{
		// Absoluto (lo pone el propio componente): la escala es la del mundo y el corro crece con ella.
		Dizzy->SetWorldScale3D(FVector(GetHitStunScale()));
		Dizzy->SetWorldLocation(GetHitStunAnchor());
		Dizzy->SetDizzy(bStunned);
	}
}

void UTN_BeachEnemyDizzyComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// La base los pone sobre la cabeza de un personaje; aquí van donde diga el enemigo (su cabeza o encima del cuerpo).
	if (const ATN_BeachEnemy* Enemy = Cast<ATN_BeachEnemy>(GetOwner()))
	{
		SetWorldLocation(Enemy->GetHitStunAnchor());
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Tortuga sujeta (boca, pico...)
// ─────────────────────────────────────────────────────────────────────────────

double ATN_BeachEnemy::GetMaxHoldSeconds() const
{
	return TNBeachEnemyShared::MaxHoldSeconds;
}

void ATN_BeachEnemy::BeginHoldTurtle(ATortugaCharacter* Turtle)
{
	if (!Turtle || HeldTurtle.Get() == Turtle)
	{
		return;
	}
	// Otra cosa la mueve en esta máquina (su bola, su ragdoll, otra tortuga que la lleva): no se la sujeta encima. En un
	// cliente, quien la llama lo vuelve a intentar en el siguiente fotograma hasta que le llegue el estado del servidor.
	if (!TNBeachEnemyShared::CanHoldHere(Turtle))
	{
		return;
	}
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	// Recién soltada por el seguro de tiempo: un momento sin volver a sujetarla (que caiga de verdad).
	if (HoldBlocked.Get() == Turtle && Now < HoldBlockedUntil)
	{
		return;
	}
	if (HeldTurtle.IsValid())
	{
		EndHoldTurtle();
	}
	// Si se vigilaba porque la acababa de soltar, ya no: vuelve a ser suya.
	for (int32 i = ReleaseWatches.Num() - 1; i >= 0; --i)
	{
		if (!ReleaseWatches[i].Turtle.IsValid() || ReleaseWatches[i].Turtle.Get() == Turtle)
		{
			ReleaseWatches.RemoveAtSwap(i);
		}
	}
	HeldTurtle = Turtle;
	HoldStartTime = Now;
	if (UCharacterMovementComponent* Move = Turtle->GetCharacterMovement())
	{
		// Sin movimiento propio mientras cuelga: la coloca el enemigo en cada máquina con las mismas cuentas.
		Move->StopMovementImmediately();
		Move->DisableMovement();
		if (Turtle->GetLocalRole() == ROLE_SimulatedProxy)
		{
			// Si ya lo tenía apagado otro (su bola o su ragdoll, cuya vuelta llega aquí después que el agarre), se guarda el de
			// siempre de la tortuga: si no, al soltarla se quedaba sin suavizado (a saltitos) el resto de la ronda.
			const ENetworkSmoothingMode Current = Move->NetworkSmoothingMode;
			HeldSavedSmoothing = static_cast<uint8>(Current != ENetworkSmoothingMode::Disabled ? Current : ENetworkSmoothingMode::Exponential);
			bHeldSmoothingSaved = true;
			Move->NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;
		}
		if (HasAuthority())
		{
			// El dueño la coloca con su reloj: no se le corrige mientras cuelga.
			Move->bIgnoreClientMovementErrorChecksAndCorrection = true;
		}
		// El enemigo se actualiza después de su movimiento: la última palabra sobre dónde está la tiene él.
		PrimaryActorTick.AddPrerequisite(Move, Move->PrimaryComponentTick);
	}
	if (UTN_TurtleAnimInstance* Anim = Turtle->GetMesh() ? Cast<UTN_TurtleAnimInstance>(Turtle->GetMesh()->GetAnimInstance()) : nullptr)
	{
		Anim->SetCelebration(ETNTurtleCelebration::Tantrum);
	}
	SetTurtleHeld(Turtle, true);
}

void ATN_BeachEnemy::EndHoldTurtle()
{
	ATortugaCharacter* Turtle = HeldTurtle.Get();
	HeldTurtle.Reset();
	if (Turtle)
	{
		if (UCharacterMovementComponent* Move = Turtle->GetCharacterMovement())
		{
			PrimaryActorTick.RemovePrerequisite(Move, Move->PrimaryComponentTick);
		}
		Turtle->SetActorRotation(FRotator(0.f, Turtle->GetActorRotation().Yaw, 0.f));
		// Suelta antes de restaurar: si otro enemigo (o un gusano) la tiene ya, lo suyo manda.
		SetTurtleHeld(Turtle, false);
		// Lo que se restaura ya y lo que se vigila 3 s: otro sistema puede tocarla a la vez (la bola del mareo que aún no
		// ha llegado a esta máquina, una patada de la tormenta, un derribo...) y no puede quedarse colgada en el aire.
		FReleaseWatch& Watch = ReleaseWatches.AddDefaulted_GetRef();
		Watch.Turtle = Turtle;
		Watch.Left = TNBeachEnemyShared::ReleaseWatchSeconds;
		Watch.Smoothing = HeldSavedSmoothing;
		Watch.bSmoothingSaved = bHeldSmoothingSaved;
		RestoreReleasedTurtle(Watch, false);
	}
	bHeldSmoothingSaved = false;
}

void ATN_BeachEnemy::RestoreReleasedTurtle(FReleaseWatch& Watch, bool bFinal)
{
	ATortugaCharacter* Turtle = Watch.Turtle.Get();
	if (!Turtle || Turtle->IsActorBeingDestroyed())
	{
		Watch.Left = 0.f;
		return;
	}
	// Otro la sujeta ya (otro enemigo, un gusano): es suya, aquí no se toca nada.
	if (IsTurtleHeld(Turtle) || ATN_BeachSandWorm::IsBeingEaten(Turtle))
	{
		return;
	}
	UCharacterMovementComponent* Move = Turtle->GetCharacterMovement();
	if (!Move)
	{
		return;
	}
	// Servidor: el dueño vuelve a tener correcciones sí o sí (si no, su máquina manda sobre dónde está y puede quedarse
	// flotando donde la dejó el pico sin que nadie la corrija).
	if (Turtle->HasAuthority())
	{
		Move->bIgnoreClientMovementErrorChecksAndCorrection = false;
	}
	// Fuera la pataleta (la de la sujeción; si otra cosa ha puesto otra celebración, se respeta).
	if (UTN_TurtleAnimInstance* Anim = Turtle->GetMesh() ? Cast<UTN_TurtleAnimInstance>(Turtle->GetMesh()->GetAnimInstance()) : nullptr)
	{
		if (Anim->GetCelebration() == ETNTurtleCelebration::Tantrum)
		{
			Anim->SetCelebration(ETNTurtleCelebration::None);
		}
	}
	// Ya ha llegado a la meta (el GameMode la tiene en el agua): no se toca su movimiento.
	if (const ATN_CoopPlayerState* Coop = Turtle->GetPlayerState<ATN_CoopPlayerState>())
	{
		if (Coop->bHasFinishedRun)
		{
			Watch.Left = 0.f;
			return;
		}
	}
	// ¿La mueve otro sistema? La bola del caparazón (sigue a su caja), el ragdoll del derribo (se levanta sola), otra
	// tortuga que la lleva en brazos o la muerte: entonces, suyo. Un derribo sin ragdoll no la mueve: que caiga.
	const UTN_ShellComponent* Shell = Turtle->GetShellComponent();
	const USkeletalMeshComponent* Mesh = Turtle->GetMesh();
	const UTN_CarryComponent* Carry = Turtle->GetCarryComponent();
	// En su caparazón (estado replicado) y con la caja a punto de llegar a esta máquina: un momento, que la mueva su caja.
	const float SinceRelease = TNBeachEnemyShared::ReleaseWatchSeconds - Watch.Left;
	const bool bBallArriving = Shell && Shell->IsInShell() && !Shell->HasLocalBody() && SinceRelease < TNBeachEnemyShared::BallArrivalGrace;
	const bool bOtherDriver = (Shell && Shell->HasLocalBody()) || bBallArriving || (Mesh && Mesh->IsSimulatingPhysics())
		|| (Carry && Carry->IsBeingCarried()) || Turtle->IsDead();
	if (!bOtherDriver)
	{
		// Nadie la mueve: que caiga por su cuenta (también en bola sin caja en esta máquina o con la ronda parada: mejor en
		// el suelo que colgada del aire).
		if (!Move->IsComponentTickEnabled())
		{
			Move->SetComponentTickEnabled(true);
		}
		if (Move->MovementMode == MOVE_None)
		{
			Move->SetMovementMode(MOVE_Falling);
		}
		// El suavizado de red de los demás clientes, el que tenía (la bola o el ragdoll lo guardan apagado si llegan antes).
		if (Watch.bSmoothingSaved)
		{
			Move->NetworkSmoothingMode = static_cast<ENetworkSmoothingMode>(Watch.Smoothing);
			Watch.bSmoothingSaved = false;
		}
	}
	else if (bFinal && Watch.bSmoothingSaved && !(Shell && Shell->HasLocalBody()) && !(Mesh && Mesh->IsSimulatingPhysics()))
	{
		Move->NetworkSmoothingMode = static_cast<ENetworkSmoothingMode>(Watch.Smoothing);
		Watch.bSmoothingSaved = false;
	}
}

void ATN_BeachEnemy::TickReleaseWatches(float DeltaSeconds)
{
	for (int32 i = ReleaseWatches.Num() - 1; i >= 0; --i)
	{
		FReleaseWatch& Watch = ReleaseWatches[i];
		Watch.Left -= DeltaSeconds;
		const bool bFinal = Watch.Left <= 0.f;
		RestoreReleasedTurtle(Watch, bFinal);
		if (bFinal || Watch.Left <= 0.f)
		{
			ReleaseWatches.RemoveAtSwap(i);
		}
	}
}

void ATN_BeachEnemy::PlaceHeldTurtle(const FVector& Grip, float Yaw)
{
	ATortugaCharacter* Turtle = HeldTurtle.Get();
	if (!Turtle)
	{
		return;
	}
	// Seguro de tiempo: ninguna sujeción de la playa dura tanto; si pasa, algo ha fallado y se suelta ya.
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (Now - HoldStartTime > GetMaxHoldSeconds())
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Playa] %s llevaba %.1f s sujetando a %s: se suelta por seguridad."), *GetName(), Now - HoldStartTime, *GetNameSafe(Turtle));
		HoldBlocked = Turtle;
		HoldBlockedUntil = Now + TNBeachEnemyShared::HoldBlockSeconds;
		EndHoldTurtle();
		return;
	}
	// Otra cosa ha empezado a moverla en esta máquina (en un cliente, su bola o su ragdoll pueden llegar antes que la suelta
	// del enemigo): se suelta ya, sin colocarla. Colocarla a la vez que la mueve su caja es clavar la cápsula en el pico
	// mientras la caja la arrastra: la bola gira alrededor de ella y se mete en la arena.
	if (!TNBeachEnemyShared::CanHoldHere(Turtle))
	{
		EndHoldTurtle();
		return;
	}
	// Otro sistema le ha quitado la marca de llevada (se comparte): mientras la sujeta, sigue marcada.
	if (!IsTurtleHeld(Turtle))
	{
		SetTurtleHeld(Turtle, true);
	}
	// Una corrección de red que llegue tarde podría devolverle el movimiento: mientras cuelga, ninguno.
	if (UCharacterMovementComponent* Move = Turtle->GetCharacterMovement())
	{
		if (Move->MovementMode != MOVE_None)
		{
			Move->StopMovementImmediately();
			Move->DisableMovement();
		}
	}
	// La espalda del caparazón en Grip: con malla, por su hueso de la espalda (la pataleta baja el cuerpo); sin ella
	// (servidor dedicado), por la cápsula.
	static const FName SpineBone(TEXT("Spine2"));
	FVector Loc = Grip;
	const USkeletalMeshComponent* Mesh = Turtle->GetMesh();
	if (bHasScreen && Mesh && !Mesh->IsSimulatingPhysics() && Mesh->GetBoneIndex(SpineBone) != INDEX_NONE)
	{
		Loc -= Mesh->GetSocketLocation(SpineBone) - Turtle->GetActorLocation();
	}
	else
	{
		const UCapsuleComponent* Capsule = Turtle->GetCapsuleComponent();
		Loc -= FVector(0.0, 0.0, Capsule ? Capsule->GetScaledCapsuleHalfHeight() * 0.75f : 70.f);
	}
	Turtle->SetActorLocationAndRotation(Loc, FRotator(0.f, Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
}

int32 ATN_BeachEnemy::CountCloserFullRate() const
{
	const UWorld* World = GetWorld();
	int32 Closer = 0;
	for (const TWeakObjectPtr<ATN_BeachEnemy>& Weak : TNBeachEnemyShared::AllEnemies())
	{
		const ATN_BeachEnemy* Other = Weak.Get();
		if (!Other || Other == this || !Other->bThrottleWhenFar || !Other->bLodWantsFull || Other->GetWorld() != World)
		{
			continue;
		}
		// Empate: decide la dirección del objeto, igual vista desde los dos (no se quedan los dos fuera ni los dos dentro).
		if (Other->LodPriority < LodPriority || (Other->LodPriority == LodPriority && Other < this))
		{
			++Closer;
		}
	}
	return Closer;
}

void ATN_BeachEnemy::UpdateLod()
{
	if (!bThrottleWhenFar)
	{
		return;
	}
	// Distancia a la tortuga más cercana (servidor: la simulación; clientes: lo que se ve).
	const FVector Here = HasAuthority() ? SimLoc : ShownLoc;
	TArray<ATortugaCharacter*> Turtles;
	GatherTurtles(this, Turtles);
	double NearestSq = 1.0e18;
	for (const ATortugaCharacter* Turtle : Turtles)
	{
		NearestSq = FMath::Min(NearestSq, FVector::DistSquared2D(Turtle->GetActorLocation(), Here));
	}
	TNBeachEnemyLod::FInput In;
	In.bActive = HasAuthority() && NearestSq < FMath::Square(static_cast<double>(GetActiveRange()));
	In.bHasScreen = bHasScreen;
	In.ViewDistance = ViewDistance;
	In.VisualRange = GetVisualRange();
	const TNBeachEnemyLod::ETier Wanted = TNBeachEnemyLod::Classify(In);
	bLodWantsFull = Wanted == TNBeachEnemyLod::ETier::Full;
	LodPriority = TNBeachEnemyLod::Priority(HasAuthority(), static_cast<float>(FMath::Sqrt(NearestSq)), bHasScreen, ViewDistance);
	const TNBeachEnemyLod::ETier Tier = bLodWantsFull ? TNBeachEnemyLod::ApplyBudget(Wanted, CountCloserFullRate()) : Wanted;
	bThrottled = Tier != TNBeachEnemyLod::ETier::Full;
	const float Interval = TNBeachEnemyLod::TickInterval(Tier);
	if (!FMath::IsNearlyEqual(GetActorTickInterval(), Interval))
	{
		SetActorTickInterval(Interval);
	}
	if (HasAuthority())
	{
		const float Frequency = In.bActive ? NetFrequencyNear : FMath::Min(NetFrequencyNear, 3.f);
		if (!FMath::IsNearlyEqual(GetNetUpdateFrequency(), Frequency))
		{
			SetNetUpdateFrequency(Frequency);
		}
	}
}

void ATN_BeachEnemy::UpdateShown(float DeltaSeconds)
{
	if (HasAuthority())
	{
		ShownLoc = SimLoc;
		ShownYaw = SimYaw;
	}
	else if (bHasRep)
	{
		const UWorld* World = GetWorld();
		const double Now = World ? World->GetTimeSeconds() : 0.0;
		const double Ahead = FMath::Clamp(Now - LastRepTime, 0.0, 0.25);
		const FVector Target = LastRepLoc + RepVelocity * Ahead;
		const float K = 1.f - FMath::Exp(-DeltaSeconds / 0.08f);
		ShownLoc += (Target - ShownLoc) * K;
		const float TargetYaw = static_cast<float>(FRotator::DecompressAxisFromShort(Mover.Yaw));
		const float KYaw = 1.f - FMath::Exp(-DeltaSeconds / 0.1f);
		ShownYaw = FMath::UnwindDegrees(ShownYaw + FMath::FindDeltaAngleDegrees(ShownYaw, TargetYaw) * KYaw);
	}
	if (Rig)
	{
		Rig->SetWorldLocationAndRotation(ShownLoc, FRotator(0.f, ShownYaw, 0.f));
	}
}

namespace TNBeachEnemySolidBlock
{
	/** Radio con el que cuenta la bola del caparazón (cm; la caja mide 55 x 46 x 42) y velocidad mínima de salida (cm/s). */
	constexpr float BallRadius = 25.f;
	constexpr float PushSpeed = 350.f;
}

void ATN_BeachEnemy::RegisterSolidBlock(UBoxComponent* Block)
{
	if (!Block)
	{
		return;
	}
	// Tampoco en los clientes: allí el bloque va con la posición extrapolada del enemigo y empujaría una bola que es del
	// servidor. Las tortugas (Pawn) y los objetos lanzados (WorldDynamic) siguen chocando con él.
	Block->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
	SolidBlock = Block;
}

void ATN_BeachEnemy::ServerPushShellBalls() const
{
	using namespace TNBeachEnemySolidBlock;
	const UBoxComponent* Block = SolidBlock.Get();
	UWorld* World = GetWorld();
	if (!Block || !World || !Block->IsCollisionEnabled())
	{
		return;
	}
	const FTransform BlockXf = Block->GetComponentTransform();
	const FVector Extent = Block->GetScaledBoxExtent();
	const double ReachSq = FMath::Square(Extent.Size() + BallRadius);
	for (TActorIterator<ATN_ShellBody> It(World); It; ++It)
	{
		UBoxComponent* Box = It->GetBox();
		if (!Box || !Box->IsSimulatingPhysics())
		{
			continue;
		}
		const FVector At = Box->GetComponentLocation();
		if (FVector::DistSquared(At, BlockXf.GetLocation()) > ReachSq)
		{
			continue;
		}
		FVector LocalVelocity = BlockXf.InverseTransformVectorNoScale(Box->GetPhysicsLinearVelocity());
		if (TNShellLogic::PushBallOutOfBlock(BlockXf.InverseTransformPositionNoScale(At), Extent, BallRadius, PushSpeed, LocalVelocity))
		{
			Box->SetPhysicsLinearVelocity(BlockXf.TransformVectorNoScale(LocalVelocity));
		}
	}
}

void ATN_BeachEnemy::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	LodTimer -= DeltaSeconds;
	if (LodTimer <= 0.f)
	{
		LodTimer = TNBeachEnemyShared::LodPeriod;
		UpdateLod();
	}
	if (!RagdollPushes.IsEmpty())
	{
		RagdollPushes.Tick(DeltaSeconds);
	}
	if (ReleaseWatches.Num() > 0)
	{
		TickReleaseWatches(DeltaSeconds);
	}
	if (HasAuthority())
	{
		// Bolas de caparazón lanzadas contra los enemigos (una vez por fotograma para todo el mundo).
		TNBeachEnemyShared::ScanThrownShells(GetWorld());
		TRACE_CPUPROFILER_EVENT_SCOPE(TN_BeachEnemy_ServerTick);
		ServerTick(DeltaSeconds);
	}
	if (bUsesMover)
	{
		UpdateShown(DeltaSeconds);
	}
	if (HasAuthority() && SolidBlock.IsValid())
	{
		ServerPushShellBalls();
	}
	if (bHasScreen)
	{
		ViewDistance = LocalViewDistance(this, bUsesMover ? ShownLoc : GetActorLocation());
		TRACE_CPUPROFILER_EVENT_SCOPE(TN_BeachEnemy_VisualTick);
		VisualTick(DeltaSeconds);
		UpdateHitStunVisual();
		if (bPopsLive)
		{
			bool bAny = false;
			for (FTNTrapPopText& Pop : Pops)
			{
				if (Pop.Tick(DeltaSeconds, GetWorld()))
				{
					bAny = true;
				}
			}
			bPopsLive = bAny;
		}
	}
	if (HasAuthority() && IsDebugDraw())
	{
		const FVector At = bUsesMover ? ShownLoc : GetActorLocation();
		DrawDebugString(GetWorld(), At + FVector(0.0, 0.0, 900.0), FString::Printf(TEXT("%s · estado %d · %.1f s"), *GetClass()->GetName(),
			static_cast<int32>(Mover.State), GetStateAge()), nullptr, FColor::White, 0.f, true);
		DrawDebugCircle(GetWorld(), GetActorLocation() + FVector(0.0, 0.0, 30.0), GetFootprintRadius(), 48, FColor::Cyan, false, -1.f, 0, 8.f,
			FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false);
	}
}
