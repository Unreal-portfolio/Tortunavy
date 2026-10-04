#include "Game/TN_UnderTerrainGuard.h"
#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_Log.h"
#include "Player/TortugaCharacter.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_ShellBody.h"
#include "Player/TN_ShellComponent.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "World/Beach/TN_BeachSandWorm.h"
#include "World/Beach/TN_BeachStun.h"
#include "World/TN_DeathZoneVolume.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/KillZVolume.h"
#include "GameFramework/PhysicsVolume.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace TNUnderTerrainDetail
{
	TAutoConsoleVariable<int32> CVarUnderTerrainGuard(TEXT("TN.SafetyNet.UnderTerrain"), 1,
		TEXT("Coop y Clásico (servidor): 1 = red de seguridad bajo el terreno: una tortuga hundida bajo el terreno vuelve a la superficie, con aviso «[Red de seguridad]» en el registro; 0 = apagada (para comparar). La carrera de la playa tiene la suya (TN.Race.SafetyNet)."));

	/** Una superficie donde ponerse de pie: hacia arriba al menos esto (el suelo andable de la tortuga). */
	constexpr double MinStandNormalZ = 0.6;

	/** Una cara vista desde abajo a menos de esto (cm) de la superficie de encima es esa misma superficie (malla de doble cara). */
	constexpr double SameSurfaceTolerance = 5.0;

	/** Anillos alrededor (cm entre uno y otro) y direcciones por anillo al buscar sitio de pie. */
	constexpr float RingStep = 250.f;
	constexpr int32 RingDirections = 8;

	/** Superficies que se miran como mucho en una vertical (de arriba abajo) para quedarse con la más cercana a la tortuga. */
	constexpr int32 MaxSurfacesPerColumn = 4;

	/** Cuánto (cm) por encima del suelo se pone la cápsula de pie. */
	constexpr double StandLift = 5.0;

	FCollisionQueryParams MakeQuery(const ATortugaCharacter& Turtle)
	{
		FCollisionQueryParams Query(SCENE_QUERY_STAT(TNUnderTerrainGuard), false, &Turtle);
		if (const UTN_ShellComponent* Shell = Turtle.GetShellComponent())
		{
			Query.AddIgnoredActor(Shell->GetBody());
		}
		return Query;
	}

	/** La cápsula de pie de la clase (en el panzazo, la de la tortuga es más baja). */
	void StandingCapsule(const ATortugaCharacter& Turtle, float& OutRadius, float& OutHalfHeight)
	{
		const ACharacter* Defaults = Turtle.GetClass()->GetDefaultObject<ACharacter>();
		const UCapsuleComponent* Capsule = Defaults ? Defaults->GetCapsuleComponent() : Turtle.GetCapsuleComponent();
		OutRadius = Capsule ? Capsule->GetUnscaledCapsuleRadius() : 34.f;
		OutHalfHeight = Capsule ? Capsule->GetUnscaledCapsuleHalfHeight() : 90.f;
	}

	/**
	 * TN.SafetyNet.Bury [metros = 3] [jugador = 0] (en el anfitrión): mete a la tortuga Metros bajo el suelo que pisa, cayendo,
	 * para probar la red de seguridad en Coop y Clásico. La red la tiene que devolver encima en ~0,2 s con un aviso en el
	 * registro. En la playa, TN.Race.Bury.
	 */
	void BuryCommand(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Red de seguridad] TN.SafetyNet.Bury: solo en el anfitrión."));
			return;
		}
		const float Meters = FMath::Max(0.5f, Args.Num() > 0 ? FCString::Atof(*Args[0]) : 3.f);
		const int32 PlayerIndex = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 0;
		int32 Index = 0;
		ATortugaCharacter* Turtle = nullptr;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It, ++Index)
		{
			if (Index == PlayerIndex && It->Get())
			{
				Turtle = Cast<ATortugaCharacter>(It->Get()->GetPawn());
				break;
			}
		}
		FHitResult Floor;
		const FCollisionQueryParams Query = Turtle ? MakeQuery(*Turtle) : FCollisionQueryParams::DefaultQueryParam;
		const FVector Location = Turtle ? Turtle->GetActorLocation() : FVector::ZeroVector;
		if (!Turtle || !World->LineTraceSingleByChannel(Floor, Location, Location - FVector(0.0, 0.0, 1000.0), ECC_Pawn, Query))
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Red de seguridad] TN.SafetyNet.Bury: hace falta la tortuga del jugador %d de pie sobre el suelo."), PlayerIndex);
			return;
		}
		const double HalfHeight = Turtle->GetCapsuleComponent() ? Turtle->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0;
		const FVector Buried(Location.X, Location.Y, Floor.ImpactPoint.Z - Meters * 100.0 + HalfHeight);
		TNBeach::RelocateTurtle(Turtle, FTransform(Turtle->GetActorRotation(), Buried));
		UE_LOG(LogTortunabo, Log, TEXT("[Red de seguridad] TN.SafetyNet.Bury: %s, %.1f m bajo el suelo."), *GetNameSafe(Turtle), Meters);
	}

	FAutoConsoleCommandWithWorldAndArgs CmdBury(TEXT("TN.SafetyNet.Bury"),
		TEXT("Coop y Clásico (anfitrión): mete a la tortuga bajo el suelo que pisa para probar la red de seguridad bajo el terreno (vuelve encima con un aviso «[Red de seguridad]» en el registro). TN.SafetyNet.Bury [metros = 3] [jugador = 0]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BuryCommand));
}

FVector TNUnderTerrain::BodyProbe(const ATortugaCharacter& Turtle, FVector& OutVelocity, FString* OutDriver)
{
	const UCapsuleComponent* Capsule = Turtle.GetCapsuleComponent();
	const double HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0;
	const UTN_ShellComponent* Shell = Turtle.GetShellComponent();
	const ATN_ShellBody* Body = Shell ? Shell->GetBody() : nullptr;
	if (Shell && Shell->HasLocalBody() && Body && Body->GetBox())
	{
		UBoxComponent* Box = Body->GetBox();
		if (OutDriver)
		{
			*OutDriver = FString::Printf(TEXT("la caja de la bola del caparazón (%s)"), *Body->GetName());
		}
		OutVelocity = Box->GetPhysicsLinearVelocity();
		return Box->GetComponentLocation() - FVector(0.0, 0.0, ATN_ShellBody::BoxHalfExtent().Z);
	}
	const USkeletalMeshComponent* Mesh = Turtle.GetMesh();
	const FBodyInstance* Root = Mesh && Mesh->IsSimulatingPhysics() ? Mesh->GetBodyInstance() : nullptr;
	if (Root && Root->IsValidBodyInstance())
	{
		if (OutDriver)
		{
			*OutDriver = TEXT("el ragdoll del derribo (física del esqueleto)");
		}
		OutVelocity = Root->GetUnrealWorldVelocity();
		return Root->GetUnrealWorldTransform().GetLocation() - FVector(0.0, 0.0, 30.0);
	}
	if (OutDriver)
	{
		if (const ATN_BeachEnemy* Holder = ATN_BeachEnemy::FindHolder(&Turtle))
		{
			*OutDriver = FString::Printf(TEXT("%s (la sujeta)"), *Holder->GetName());
		}
		else if (const UTN_CarryComponent* Carry = Turtle.GetCarryComponent(); Carry && Carry->GetCarrier())
		{
			*OutDriver = FString::Printf(TEXT("%s (la lleva en brazos)"), *Carry->GetCarrier()->GetName());
		}
		else if (ATN_BeachSandWorm::IsBeingEaten(&Turtle))
		{
			*OutDriver = TEXT("un gusano de arena");
		}
		else
		{
			const UCharacterMovementComponent* Move = Turtle.GetCharacterMovement();
			*OutDriver = FString::Printf(TEXT("su movimiento (%s)"), Move ? *Move->GetMovementName() : TEXT("sin componente"));
		}
	}
	OutVelocity = Turtle.GetVelocity();
	return Turtle.GetActorLocation() - FVector(0.0, 0.0, HalfHeight);
}

UTN_UnderTerrainGuardComponent::UTN_UnderTerrainGuardComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void UTN_UnderTerrainGuardComponent::BeginPlay()
{
	Super::BeginPlay();
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!bGuardEnabled || !Owner || !Owner->HasAuthority() || !World || !World->IsGameWorld())
	{
		return;
	}
	World->GetTimerManager().SetTimer(WatchHandle, this, &UTN_UnderTerrainGuardComponent::WatchAll, TNUnderTerrain::WatchInterval, true);
}

void UTN_UnderTerrainGuardComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WatchHandle);
	}
	Watches.Reset();
	Super::EndPlay(EndPlayReason);
}

void UTN_UnderTerrainGuardComponent::WatchAll()
{
	UWorld* World = GetWorld();
	if (!World || TNUnderTerrainDetail::CVarUnderTerrainGuard.GetValueOnGameThread() == 0)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PlayerController = It->Get();
		if (ATortugaCharacter* Turtle = PlayerController ? Cast<ATortugaCharacter>(PlayerController->GetPawn()) : nullptr)
		{
			WatchTurtle(Turtle, Now);
		}
	}
	for (auto It = Watches.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

bool UTN_UnderTerrainGuardComponent::WatchTurtle(ATortugaCharacter* Turtle, float Now)
{
	if (!bGuardEnabled || !Turtle || !Turtle->HasAuthority() || !GetWorld())
	{
		return false;
	}
	FTNUnderTerrainWatch& Watch = Watches.FindOrAdd(Turtle);
	if (ShouldSkip(*Turtle))
	{
		Watch.Strikes = 0;
		return false;
	}
	SampleSafeSpot(*Turtle, Watch, Now);

	FVector Velocity = FVector::ZeroVector;
	const FVector Probe = TNUnderTerrain::BodyProbe(*Turtle, Velocity);
	FHitResult Surface;
	double Depth = -1.0;
	if (TraceSurfaceAbove(*Turtle, Probe, Surface))
	{
		Depth = Surface.ImpactPoint.Z - Probe.Z;
	}
	// Algo encima, pero en aire libre: bajo un puente, una cornisa, un árbol o en una cueva (aunque debajo haya una sima). No
	// está bajo el mapa. Dentro de la geometría sí, aunque haya suelo debajo (el fondo del río bajo una meseta).
	if (Depth > Margin && IsInOpenSpace(*Turtle, Probe, Surface.ImpactPoint.Z))
	{
		Depth = -1.0;
	}
	if (!TNUnderTerrain::RegisterLook(Watch.Strikes, Depth, Margin))
	{
		return false;
	}
	// Las zonas de muerte (el fondo de un barranco, un volumen de muerte) matan a propósito: no se la salva.
	if (IsInDeathZone(*Turtle, Probe))
	{
		Watch.Strikes = 0;
		return false;
	}
	Rescue(*Turtle, Watch, Probe, Depth, Now);
	return true;
}

bool UTN_UnderTerrainGuardComponent::ShouldSkip(const ATortugaCharacter& Turtle) const
{
	const UCharacterMovementComponent* Move = Turtle.GetCharacterMovement();
	if (Turtle.IsDead() || !Move || Move->IsSwimming())
	{
		return true;
	}
	if (const APhysicsVolume* Volume = Turtle.GetPawnPhysicsVolume(); Volume && (Volume->bWaterVolume || Volume->IsA<AKillZVolume>()))
	{
		return true;
	}
	// Con el movimiento parado (esperando la ronda) no se la despierta; el derribo y la bola también paran la cápsula, y ahí sí se mira.
	if (Move->MovementMode == MOVE_None && !Turtle.IsKnockedDown() && !Turtle.IsInShell())
	{
		return true;
	}
	if (const UTN_CarryComponent* Carry = Turtle.GetCarryComponent(); Carry && Carry->IsBeingCarried())
	{
		return true;
	}
	const ATN_CoopPlayerState* PlayerState = Turtle.GetPlayerState<ATN_CoopPlayerState>();
	return PlayerState && (!PlayerState->bIsAlive || PlayerState->bIsDBNO || PlayerState->bHasFinishedRun);
}

bool UTN_UnderTerrainGuardComponent::IsInDeathZone(const ATortugaCharacter& Turtle, const FVector& Probe) const
{
	if (const ATN_CoopPlayerState* PlayerState = Turtle.GetPlayerState<ATN_CoopPlayerState>(); PlayerState && PlayerState->DeathZoneTimeRemaining >= 0.f)
	{
		return true;
	}
	const FVector Center = Turtle.GetActorLocation();
	for (TActorIterator<ATN_DeathZoneVolume> It(GetWorld()); It; ++It)
	{
		const FBox Bounds = It->GetComponentsBoundingBox(true);
		if (Bounds.IsInsideOrOn(Probe) || Bounds.IsInsideOrOn(Center) || Turtle.IsOverlappingActor(*It))
		{
			return true;
		}
	}
	return false;
}

bool UTN_UnderTerrainGuardComponent::TraceSurfaceAbove(const ATortugaCharacter& Turtle, const FVector& Probe, FHitResult& OutHit) const
{
	const FCollisionQueryParams Query = TNUnderTerrainDetail::MakeQuery(Turtle);
	const FVector Top = Probe + FVector(0.0, 0.0, SurfaceSearchUp);
	return GetWorld()->LineTraceSingleByChannel(OutHit, Top, Probe, ECC_Pawn, Query) && !OutHit.bStartPenetrating;
}

bool UTN_UnderTerrainGuardComponent::IsInOpenSpace(const ATortugaCharacter& Turtle, const FVector& Probe, double SurfaceZ) const
{
	using namespace TNUnderTerrainDetail;
	const FCollisionQueryParams Query = MakeQuery(Turtle);
	FHitResult Hit;
	// Hacia arriba, hasta un poco por encima de la superficie: sin depender de hacia dónde mira la cara (mallas de una o doble cara).
	const FVector End(Probe.X, Probe.Y, SurfaceZ + SameSurfaceTolerance);
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Probe, End, ECC_Pawn, Query) || Hit.bStartPenetrating)
	{
		return false;
	}
	return Hit.ImpactPoint.Z < SurfaceZ - SameSurfaceTolerance;
}

bool UTN_UnderTerrainGuardComponent::FindStandInColumn(const ATortugaCharacter& Turtle, const FVector2D& Column, double FromZ, double ToZ,
	FTransform& OutTransform) const
{
	using namespace TNUnderTerrainDetail;
	UWorld* World = GetWorld();
	const FCollisionQueryParams Query = MakeQuery(Turtle);
	float Radius = 0.f;
	float HalfHeight = 0.f;
	StandingCapsule(Turtle, Radius, HalfHeight);
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
	const FRotator Facing(0.f, Turtle.GetActorRotation().Yaw, 0.f);
	bool bFound = false;
	double StartZ = FromZ;
	// De arriba abajo: se queda con la última superficie de pie que encuentra, la más cercana por encima de la tortuga.
	for (int32 Index = 0; Index < MaxSurfacesPerColumn && StartZ > ToZ; ++Index)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, FVector(Column.X, Column.Y, StartZ), FVector(Column.X, Column.Y, ToZ), ECC_Pawn, Query)
			|| Hit.bStartPenetrating)
		{
			break;
		}
		if (Hit.ImpactNormal.Z >= MinStandNormalZ)
		{
			const FVector Stand = Hit.ImpactPoint + FVector(0.0, 0.0, HalfHeight + StandLift);
			if (!World->OverlapBlockingTestByChannel(Stand, FQuat::Identity, ECC_Pawn, Shape, Query))
			{
				OutTransform = FTransform(Facing, Stand);
				bFound = true;
			}
		}
		StartZ = Hit.ImpactPoint.Z - 2.0;
	}
	return bFound;
}

bool UTN_UnderTerrainGuardComponent::FindSurfaceSpot(const ATortugaCharacter& Turtle, const FVector& Probe, FTransform& OutTransform) const
{
	using namespace TNUnderTerrainDetail;
	const double FromZ = Probe.Z + SurfaceSearchUp;
	const double ToZ = Probe.Z;
	if (FindStandInColumn(Turtle, FVector2D(Probe.X, Probe.Y), FromZ, ToZ, OutTransform))
	{
		return true;
	}
	for (float Ring = RingStep; Ring <= RingSearchRadius + KINDA_SMALL_NUMBER; Ring += RingStep)
	{
		for (int32 Direction = 0; Direction < RingDirections; ++Direction)
		{
			const float Angle = 2.f * UE_PI * static_cast<float>(Direction) / static_cast<float>(RingDirections);
			const FVector2D Column(Probe.X + Ring * FMath::Cos(Angle), Probe.Y + Ring * FMath::Sin(Angle));
			if (FindStandInColumn(Turtle, Column, FromZ, ToZ, OutTransform))
			{
				return true;
			}
		}
	}
	return false;
}

void UTN_UnderTerrainGuardComponent::SampleSafeSpot(const ATortugaCharacter& Turtle, FTNUnderTerrainWatch& Watch, float Now) const
{
	if (Watch.SafeTime >= 0.f && Now - Watch.SafeTime < SafeSpotSampleSeconds)
	{
		return;
	}
	const UCharacterMovementComponent* Move = Turtle.GetCharacterMovement();
	if (!Move || !Move->IsMovingOnGround() || !Move->CurrentFloor.IsWalkableFloor() || Turtle.IsInShell() || Turtle.IsKnockedDown())
	{
		return;
	}
	Watch.SafeLocation = Turtle.GetActorLocation();
	Watch.SafeRotation = FRotator(0.f, Turtle.GetActorRotation().Yaw, 0.f);
	Watch.SafeTime = Now;
}

void UTN_UnderTerrainGuardComponent::Rescue(ATortugaCharacter& Turtle, FTNUnderTerrainWatch& Watch, const FVector& Probe, double Depth, float Now)
{
	// Si se vuelve a hundir enseguida cerca del mismo sitio, la superficie de ahí no la sostiene: a su último sitio seguro.
	const bool bRepeat = Watch.LastRescueTime >= 0.f && Now - Watch.LastRescueTime <= RepeatSeconds
		&& FVector::DistSquared2D(Watch.LastRescueAt, Probe) < FMath::Square(static_cast<double>(RingSearchRadius));
	FTransform Target;
	const TCHAR* Where = nullptr;
	bool bElsewhere = false;
	if (!bRepeat && FindSurfaceSpot(Turtle, Probe, Target))
	{
		Where = TEXT("a la superficie de encima");
	}
	else if (Watch.SafeTime >= 0.f)
	{
		Target = FTransform(Watch.SafeRotation, Watch.SafeLocation);
		Where = TEXT("a su último sitio seguro");
		bElsewhere = true;
	}
	else if (FindSurfaceSpot(Turtle, Probe, Target))
	{
		Where = TEXT("a la superficie de encima (sin sitio seguro apuntado)");
	}
	Watch.Strikes = 0;
	if (!Where)
	{
		UE_LOG(LogTortunabo, Error, TEXT("[Red de seguridad] %s %.1f m bajo el terreno en (%.1f, %.1f, %.1f) m y sin sitio de pie encima ni sitio seguro: no se la mueve."),
			*GetNameSafe(&Turtle), Depth / 100.0, Probe.X / 100.0, Probe.Y / 100.0, Probe.Z / 100.0);
		return;
	}
	FVector Velocity = FVector::ZeroVector;
	FString Driver;
	TNUnderTerrain::BodyProbe(Turtle, Velocity, &Driver);
	TNBeach::RelocateTurtle(&Turtle, Target);
	if (APlayerController* PlayerController = Cast<APlayerController>(Turtle.GetController()); PlayerController && bElsewhere)
	{
		// En otro sitio: la cámara, detrás de ella.
		PlayerController->ClientSetRotation(Target.Rotator(), true);
	}
	Watch.LastRescueAt = Probe;
	Watch.LastRescueTime = Now;
	const FVector At = Target.GetLocation();
	UE_LOG(LogTortunabo, Warning, TEXT("[Red de seguridad] %s %.1f m bajo el terreno en (%.1f, %.1f, %.1f) m · velocidad %.0f cm/s · la movía %s · vuelve %s en (%.1f, %.1f, %.1f) m."),
		*GetNameSafe(&Turtle), Depth / 100.0, Probe.X / 100.0, Probe.Y / 100.0, Probe.Z / 100.0, Velocity.Size(), *Driver, Where,
		At.X / 100.0, At.Y / 100.0, At.Z / 100.0);
}
