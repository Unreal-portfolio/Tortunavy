#include "World/TN_SeagullDroppingActor.h"
#include "World/TN_HazardEffects.h"
#include "Core/TN_Log.h"
#include "Player/TortugaCharacter.h"
#include "Core/TN_CoopPlayerState.h"
#include "World/Beach/TN_BeachStun.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Net/UnrealNetwork.h"
#include "CollisionQueryParams.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"          // FOverlapResult full definition
#include "UObject/ConstructorHelpers.h"

ATN_SeagullDroppingActor::ATN_SeagullDroppingActor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(true);

	DroppingMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DroppingMesh"));
	SetRootComponent(DroppingMesh);
	DroppingMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DroppingMesh->SetIsReplicated(false);

	// Mesh por defecto: esfera del engine — la caca es una bola.
	// El Decal (sombra en el suelo) ya era circular.
	// BP hijo puede sobreescribir con un mesh personalizado.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshAsset(
		TEXT("/Engine/BasicShapes/Sphere"));
	if (SphereMeshAsset.Succeeded())
	{
		DroppingMesh->SetStaticMesh(SphereMeshAsset.Object);
	}

	// Decal attached to root but with absolute transforms — stays pinned to ground
	ShadowDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("ShadowDecal"));
	ShadowDecal->SetupAttachment(DroppingMesh);
	ShadowDecal->SetAbsolute(true, true, true);
	ShadowDecal->SetWorldRotation(FRotator(-90.f, 0.f, 0.f));
	ShadowDecal->DecalSize = FVector(DecalDepth, MaxShadowRadius, MaxShadowRadius);
}

void ATN_SeagullDroppingActor::BeginPlay()
{
	Super::BeginPlay();
	// Tick on ALL machines: server runs gameplay, clients update shadow visual
	// Shadow position is set once GroundTargetZ replicates (see UpdateShadowScale)

	// Capturamos la escala que el BP haya configurado para el DroppingMesh, para
	// interpolar entre ella (top) y ella * MinMeshScaleFactor (impacto). Sin esta
	// cache, al modificar el scale en Tick perderíamos el valor original del BP.
	if (DroppingMesh)
	{
		InitialMeshScaleCache = DroppingMesh->GetRelativeScale3D();
	}
}

void ATN_SeagullDroppingActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void ATN_SeagullDroppingActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_SeagullDroppingActor, GroundTargetZ);
	DOREPLIFETIME(ATN_SeagullDroppingActor, ImpactXY);
}

// ── Tick ────────────────────────────────────────────────────────────────────────

void ATN_SeagullDroppingActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bImpacted) { return; }

	// Visual: todos los clientes actualizan la sombra en el suelo
	UpdateShadowScale();

	if (!HasAuthority()) { return; }

	// Servidor: caída + detección de impacto
	FVector Loc = GetActorLocation();
	Loc.Z -= FallSpeed * DeltaTime;
	SetActorLocation(Loc);

	if (Loc.Z <= GroundTargetZ)
	{
		Loc.Z = GroundTargetZ;
		SetActorLocation(Loc);
		ResolveImpact();
	}
}

// ── Lógica ─────────────────────────────────────────────────────────────────────

void ATN_SeagullDroppingActor::UpdateShadowScale()
{
	if (!ShadowDecal) { return; }

	// Guard: en clientes la repl de ImpactXY/GroundTargetZ puede llegar tras el
	// primer tick. Si aún no ha llegado, no reubicamos el decal (evita el salto
	// al origen del mundo durante 1–2 frames).
	if (ImpactXY.IsNearlyZero() && GroundTargetZ == 0.f) { return; }

	// Pin decal at ground level (absolute transform, doesn't follow mesh)
	ShadowDecal->SetWorldLocation(FVector(ImpactXY.X, ImpactXY.Y, GroundTargetZ + 5.f));

	const float TotalHeight  = DropSpawnHeight;
	const float CurrentHeight = FMath::Max(0.f, GetActorLocation().Z - GroundTargetZ);
	const float NormalizedHeight = FMath::Clamp(CurrentHeight / TotalHeight, 0.f, 1.f);

	// SmoothStep en lugar de Lerp lineal: el sombreado se mantiene grande durante
	// el principio del trayecto y se cierra rápido cerca del impacto, telegrafiando
	// claramente "ya casi llega". Con Lerp lineal el cierre era proporcional a la
	// caída → sentía 'plano' y poco urgente.
	const float SmoothAlpha = FMath::SmoothStep(0.f, 1.f, NormalizedHeight);
	const float Radius = FMath::Lerp(MinShadowRadius, MaxShadowRadius, SmoothAlpha);

	ShadowDecal->DecalSize = FVector(DecalDepth, Radius, Radius);

	// Mesh shrink: spawn scale → MinMeshScaleFactor * spawn scale en el impacto.
	// NormalizedHeight=1 al spawn, 0 en suelo, por eso Lerp(min, 1, h) aumenta con h.
	if (DroppingMesh)
	{
		const float Factor = FMath::Lerp(MinMeshScaleFactor, 1.f, SmoothAlpha);
		DroppingMesh->SetRelativeScale3D(InitialMeshScaleCache * Factor);
	}
}

void ATN_SeagullDroppingActor::ResolveImpact()
{
	if (bImpacted) { return; }
	bImpacted = true;

	bool bHitPlayer = false;

	// Hitbox esférico en el punto de impacto
	const FVector ImpactPoint = FVector(ImpactXY.X, ImpactXY.Y, GroundTargetZ);

	// G4 fix: OverlapMultiByChannel en vez de TActorIterator. El iterator visita
	// TODOS los ATortugaCharacter del mundo (8 jugadores × N cadáveres ragdoll
	// que también son ATortugaCharacter); el overlap con sphere ECC_Pawn solo
	// devuelve los que tocan el hitbox real → O(few) en vez de O(all).
	TArray<FOverlapResult> Overlaps;
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(ImpactRadius);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SeagullDroppingImpact), false);
	GetWorld()->OverlapMultiByChannel(Overlaps, ImpactPoint, FQuat::Identity, ECC_Pawn, Sphere, Params);

	for (const FOverlapResult& Hit : Overlaps)
	{
		ATortugaCharacter* C = Cast<ATortugaCharacter>(Hit.GetActor());
		if (!C || !C->GetController()) { continue; }

		ATN_CoopPlayerState* PS = C->GetPlayerState<ATN_CoopPlayerState>();
		if (!PS || !PS->IsAliveAndPlaying()) { continue; }

		// Respetar la protección de sombrilla: si el jugador está bajo una sombrilla
		// abierta, la caca no lo mata (igual que la gaviota en TN_EnemySeagull).
		if (C->HasUmbrellaProtection())
		{
			UE_LOG(LogTortunabo, Log, TEXT("[SeagullDropping] %s protegido por sombrilla — impacto ignorado"),
				*GetNameSafe(C));
			continue;
		}
		// Tirado en plancha en el momento justo: la caca le pasa por encima.
		if (bBellyDiveDodges && TNBeach::IsDodgingByBellyDive(C))
		{
			UE_LOG(LogTortunabo, Log, TEXT("[SeagullDropping] %s la esquiva en plancha — impacto ignorado"),
				*GetNameSafe(C));
			continue;
		}
		// Caca de gaviota: 30 de daño (hoja EnemyAndObstacleData, #871); ya no mata de un golpe.
		TNHazard::Apply(UTN_HazardTuning::Get().SeagullDropping, C, this);
		bHitPlayer = true;
	}

	MulticastOnImpact(bHitPlayer);
	Destroy();
}

// ── Multicast ─────────────────────────────────────────────────────────────────

void ATN_SeagullDroppingActor::MulticastOnImpact_Implementation(bool bHitPlayer)
{
	OnImpact(bHitPlayer);
}

// ── Spawn helpers ──────────────────────────────────────────────────────────────

ATN_SeagullDroppingActor* ATN_SeagullDroppingActor::SpawnDroppingOnPlayer(
	const UObject* WorldContextObject, TSubclassOf<ATN_SeagullDroppingActor> DroppingClass,
	ATortugaCharacter* Target)
{
	if (!WorldContextObject || !DroppingClass || !Target) { return nullptr; }

	return SpawnDroppingAtLocation(WorldContextObject, DroppingClass, Target->GetActorLocation());
}

ATN_SeagullDroppingActor* ATN_SeagullDroppingActor::SpawnDroppingAtLocation(
	const UObject* WorldContextObject, TSubclassOf<ATN_SeagullDroppingActor> DroppingClass,
	FVector GroundTarget)
{
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World || !DroppingClass || World->GetNetMode() == NM_Client) { return nullptr; }

	// Obtener la altura del suelo real mediante line trace
	float GroundZ = GroundTarget.Z;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SeagullDropping), true);
	if (World->LineTraceSingleByChannel(Hit, GroundTarget + FVector(0,0,500.f),
		GroundTarget - FVector(0,0,5000.f), ECC_WorldStatic, Params))
	{
		GroundZ = Hit.ImpactPoint.Z;
	}

	// Spawn de la caca a DropSpawnHeight sobre el suelo
	// Usamos el default de DropSpawnHeight — recuperamos el CDO para leerlo
	float SpawnHeightOffset = 1200.f;
	if (ATN_SeagullDroppingActor* CDO = DroppingClass->GetDefaultObject<ATN_SeagullDroppingActor>())
	{
		SpawnHeightOffset = CDO->DropSpawnHeight;
	}

	FVector SpawnLoc = FVector(GroundTarget.X, GroundTarget.Y, GroundZ + SpawnHeightOffset);

	FActorSpawnParameters Params2;
	Params2.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ATN_SeagullDroppingActor* Dropping = World->SpawnActor<ATN_SeagullDroppingActor>(
		DroppingClass, SpawnLoc, FRotator::ZeroRotator, Params2);

	if (Dropping)
	{
		Dropping->GroundTargetZ = GroundZ;
		Dropping->ImpactXY      = FVector2D(GroundTarget.X, GroundTarget.Y);
	}

	return Dropping;
}
