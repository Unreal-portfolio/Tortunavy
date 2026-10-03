#include "World/TN_PickupGlowComponent.h"
#include "Multiplayer/TN_LocalViews.h"
#include "TN_LootGlowKit.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto
// del bloque.
namespace TNPickupGlowDetail
{
	/** Intervalo del tick lejos de la cámara (s): solo mira la distancia. */
	constexpr float FarTickInterval = 0.35f;
	/** Ritmo de la respiración (la del anillo es la del kit, TNLootGlow::RingPose; la columna y la luz respiran con ella). */
	constexpr float BreathRate = TNLootGlow::RingBreathRate;
	/** Lo que tarda en aparecer al pararse y en esconderse al moverse (1/s). */
	constexpr float AppearSpeed = 3.f;
	constexpr float HideSpeed = 10.f;
	/** Chispitas: por segundo, cuántas a la vez y a qué altura sobre el anillo nacen (cm). */
	constexpr float SparkleRate = 4.5f;
	constexpr int32 SparkleMax = 10;
	constexpr float SparkleLift = 8.f;
	/** Luz: altura sobre el suelo (cm). */
	constexpr float LightHeight = 55.f;
}

UTN_PickupGlowComponent::UTN_PickupGlowComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickInterval = TNPickupGlowDetail::FarTickInterval;
	SetIsReplicatedByDefault(false);
	SetCanEverAffectNavigation(false);
}

bool UTN_PickupGlowComponent::HasScreen() const
{
	const UWorld* World = GetWorld();
	return World && World->IsGameWorld() && World->GetNetMode() != NM_DedicatedServer;
}

void UTN_PickupGlowComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!HasScreen())
	{
		// Sin pantalla no hay nada que ver: ni tick ni componentes.
		SetComponentTickEnabled(false);
		return;
	}
	if (const AActor* Actor = GetOwner())
	{
		LastOwnerLocation = Actor->GetActorLocation();
	}
	// Cada objeto desfasado (no giran ni respiran todos a la vez).
	Clock = FMath::FRandRange(0.f, 10.f);
}

void UTN_PickupGlowComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (SparkleFx != INDEX_NONE)
	{
		TNAmbientFX::RemoveOwner(GetOwner());
		SparkleFx = INDEX_NONE;
	}
	Super::EndPlay(EndPlayReason);
}

void UTN_PickupGlowComponent::SetFloatTarget(USceneComponent* InTarget, float InRestZ)
{
	if (FloatTarget.Get() != InTarget)
	{
		// Giro de reposo: el que traiga la malla al engancharla (el del Blueprint).
		RestRotation = InTarget ? InTarget->GetRelativeRotation().Quaternion() : FQuat::Identity;
	}
	FloatTarget = InTarget;
	RestZ = InRestZ;
	bHasRest = InTarget != nullptr;
	// La malla puede haber cambiado de tamaño: el anillo se recalcula.
	if (bVisualsBuilt)
	{
		RingScaleBase = ComputeRingRadius() / TNLootGlow::RingUnitRadius;
	}
}

void UTN_PickupGlowComponent::SetGlowEnabled(bool bOn)
{
	if (bGlowEnabled == bOn)
	{
		return;
	}
	bGlowEnabled = bOn;
	if (!bOn)
	{
		HideVisuals();
		RestoreFloatTarget();
	}
}

float UTN_PickupGlowComponent::ComputeRingRadius() const
{
	if (RingRadius > 0.f)
	{
		return RingRadius;
	}
	// Algo más ancho que el objeto en planta, entre 55 y 110 cm.
	float Half = 30.f;
	if (const UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(FloatTarget.Get()))
	{
		const FBoxSphereBounds Local = Prim->CalcLocalBounds();
		const FVector S = Prim->GetRelativeScale3D().GetAbs();
		Half = static_cast<float>(FMath::Max(Local.BoxExtent.X * S.X, Local.BoxExtent.Y * S.Y));
	}
	return FMath::Clamp(Half * 1.25f + 25.f, 55.f, 110.f);
}

void UTN_PickupGlowComponent::BuildVisuals()
{
	AActor* Actor = GetOwner();
	if (bVisualsBuilt || !Actor)
	{
		return;
	}
	bVisualsBuilt = true;
	RingScaleBase = ComputeRingRadius() / TNLootGlow::RingUnitRadius;

	// Anillo y columna: en el mundo (absolutos), apoyados en el suelo que haya bajo el objeto.
	auto MakeMeshComp = [this, Actor](UStaticMesh* Asset, float DrawDistance) -> UStaticMeshComponent*
	{
		if (!Asset)
		{
			return nullptr;
		}
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Actor, NAME_None, RF_Transient);
		Comp->SetupAttachment(this);
		Comp->SetAbsolute(true, true, true);
		Comp->SetStaticMesh(Asset);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetGenerateOverlapEvents(false);
		Comp->SetCanEverAffectNavigation(false);
		Comp->SetCastShadow(false);
		Comp->SetReceivesDecals(false);
		Comp->SetCullDistance(DrawDistance);
		Comp->SetVisibility(false);
		Comp->RegisterComponent();
		return Comp;
	};
	RingComp = MakeMeshComp(TNLootGlow::RingMesh(), RingDrawDistance);
	if (BeamHeight > 0.f)
	{
		BeamComp = MakeMeshComp(TNLootGlow::BeamMesh(), BeamDrawDistance);
	}

	// Luz suave sin sombras (encendida solo cerca de la cámara).
	if (LightLumens > 0.f)
	{
		LightComp = NewObject<UPointLightComponent>(Actor, NAME_None, RF_Transient);
		LightComp->SetupAttachment(this);
		LightComp->SetAbsolute(true, true, true);
		LightComp->SetIntensityUnits(ELightUnits::Lumens);
		LightComp->SetIntensity(0.f);
		LightComp->SetAttenuationRadius(LightRadius);
		LightComp->SetSourceRadius(12.f);
		LightComp->SetLightColor(TNLootGlow::Gold());
		LightComp->SetCastShadows(false);
		LightComp->SetVolumetricScatteringIntensity(0.f);
		LightComp->SetVisibility(false);
		LightComp->RegisterComponent();
	}
}

void UTN_PickupGlowComponent::HideVisuals()
{
	if (RingComp) { RingComp->SetVisibility(false); }
	if (BeamComp) { BeamComp->SetVisibility(false); }
	if (LightComp) { LightComp->SetVisibility(false); }
	AppliedLight = -1.f;
	Appear = 0.f;
	if (SparkleFx != INDEX_NONE)
	{
		TNAmbientFX::RemoveOwner(GetOwner());
		SparkleFx = INDEX_NONE;
	}
}

void UTN_PickupGlowComponent::RestoreFloatTarget()
{
	if (USceneComponent* Target = FloatTarget.Get())
	{
		if (bHasRest)
		{
			const FVector Rel = Target->GetRelativeLocation();
			Target->SetRelativeLocationAndRotation(FVector(Rel.X, Rel.Y, RestZ), RestRotation);
		}
	}
}

void UTN_PickupGlowComponent::SnapToGround()
{
	const AActor* Actor = GetOwner();
	const UWorld* World = GetWorld();
	if (!Actor || !World)
	{
		return;
	}
	bGrounded = true;
	const FVector Base = Actor->GetActorLocation();
	GroundPoint = Base;
	GroundTilt = FQuat::Identity;
	// El suelo firme bajo el objeto (terreno y decorados; no otros objetos ni tortugas). Si no hay suelo cerca, el anillo
	// va a los pies del objeto.
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(TN_PickupGlowGround), false, Actor);
	if (World->LineTraceSingleByObjectType(Hit, Base + FVector(0.f, 0.f, 60.f), Base - FVector(0.f, 0.f, 150.f),
		FCollisionObjectQueryParams(ECC_WorldStatic), Query))
	{
		GroundPoint = Hit.ImpactPoint;
		if (Hit.ImpactNormal.Z > 0.6)
		{
			GroundTilt = FQuat::FindBetweenNormals(FVector::UpVector, Hit.ImpactNormal.GetSafeNormal());
		}
	}
}

void UTN_PickupGlowComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	using namespace TNPickupGlowDetail;
	AActor* Actor = GetOwner();
	if (!Actor || !HasScreen())
	{
		return;
	}
	if (!bGlowEnabled || Actor->IsHidden())
	{
		if (Appear > 0.f || SparkleFx != INDEX_NONE) { HideVisuals(); }
		return;
	}
	BuildVisuals();

	// Distancia a la cámara local: lejos, tick lento y nada que mover.
	// Con la pantalla partida (#311), la cámara local más cercana.
	const FVector Location = Actor->GetActorLocation();
	FVector CameraAt = Location;
	const float ViewDistance = TNLocalViews::ClosestCamera(GetWorld(), Location, CameraAt) ? static_cast<float>(FVector::Dist(CameraAt, Location)) : 0.f;
	const bool bNear = ViewDistance < AnimRange;
	if (bNear != bNearView)
	{
		bNearView = bNear;
		SetComponentTickInterval(bNear ? 0.f : FarTickInterval);
	}

	// Quieto o moviéndose (el saltito al salir de un rebuscable, o si lo recolocan): moviéndose, la marca se esconde.
	const bool bMoving = !Location.Equals(LastOwnerLocation, 1.0) || !Actor->GetActorScale3D().Equals(FVector::OneVector, 0.02);
	LastOwnerLocation = Location;
	if (bMoving)
	{
		StillTime = 0.f;
		bGrounded = false;
	}
	else
	{
		StillTime += DeltaTime;
		if (!bGrounded)
		{
			SnapToGround();
		}
	}
	const float Wanted = bMoving || !bGrounded ? 0.f : 1.f;
	Appear = Wanted > Appear ? FMath::Min(Wanted, Appear + AppearSpeed * DeltaTime) : FMath::Max(Wanted, Appear - HideSpeed * DeltaTime);
	Clock += DeltaTime;

	const bool bShow = Appear > 0.01f;
	const float Grow = 1.f - FMath::Square(1.f - Appear);
	const float Breath = FMath::Sin(Clock * BreathRate);
	const FVector Up = GroundTilt.GetUpVector();

	// Anillo que gira despacio y respira, a ras del suelo.
	if (RingComp)
	{
		RingComp->SetVisibility(bShow);
		if (bShow)
		{
			RingComp->SetWorldTransform(TNLootGlow::RingPose(GroundPoint, GroundTilt, RingScaleBase * TNLootGlow::RingUnitRadius, Clock, Grow,
				TNLootGlow::RingBreathDepth * Breath));
		}
	}

	// Columna tenue que sube del anillo (derecha, aunque el suelo esté inclinado).
	if (BeamComp)
	{
		BeamComp->SetVisibility(bShow);
		if (bShow)
		{
			const float Width = RingScaleBase * TNLootGlow::RingUnitRadius * 0.42f / TNLootGlow::BeamUnitRadius;
			BeamComp->SetWorldTransform(FTransform(FQuat::Identity, GroundPoint,
				FVector(Width * Grow, Width * Grow, BeamHeight / TNLootGlow::BeamUnitHeight * Grow * (1.f + 0.06f * Breath))));
		}
	}

	// Luz suave, solo muy cerca de la cámara.
	if (LightComp)
	{
		const float Near = 1.f - FMath::SmoothStep(LightRange * 0.7f, LightRange, ViewDistance);
		const float Lumens = LightLumens * Near * Appear * (0.85f + 0.15f * Breath);
		const bool bLit = Lumens > LightLumens * 0.02f;
		if (bLit != LightComp->IsVisible())
		{
			LightComp->SetVisibility(bLit);
		}
		if (bLit && FMath::Abs(Lumens - AppliedLight) > LightLumens * 0.02f)
		{
			AppliedLight = Lumens;
			LightComp->SetWorldLocation(GroundPoint + Up * LightHeight);
			LightComp->SetIntensity(Lumens);
		}
	}

	// Chispitas que suben del anillo (las mismas que las de los decorados que se rebuscan).
	const bool bSparkles = ViewDistance < SparkleRange;
	if (bSparkles && SparkleFx == INDEX_NONE && bShow)
	{
		TNAmbientFX::FEmitterDesc Desc = TNLootGlow::SparkleDesc(SparkleMax, SparkleRange);
		Desc.Rate = SparkleRate;
		Desc.SpawnRadius = RingScaleBase * TNLootGlow::RingUnitRadius * 0.8f;
		Desc.SpawnHeight = 10.f;
		Desc.Spread = 0.3f;
		Desc.Speed = 45.f;
		Desc.Buoyancy = 30.f;
		Desc.Drag = 0.9f;
		Desc.LifeMin = 0.9f;
		Desc.LifeMax = 1.5f;
		Desc.SizeStart = 8.f;
		SparkleFx = TNAmbientFX::AddEmitter(Actor, Desc, GroundPoint);
	}
	else if (!bSparkles && SparkleFx != INDEX_NONE && ViewDistance > SparkleRange * 1.3f)
	{
		// Lejos: fuera el emisor (sus instancias), para que no se acumulen por todo el mapa.
		TNAmbientFX::RemoveOwner(Actor);
		SparkleFx = INDEX_NONE;
	}
	if (SparkleFx != INDEX_NONE)
	{
		if (TNAmbientFX::FEmitter* Emitter = TNAmbientFX::GetEmitter(Actor, SparkleFx))
		{
			Emitter->Origin = GroundPoint + Up * SparkleLift;
			Emitter->RateScale = Appear;
		}
		TNAmbientFX::TickOwner(Actor, DeltaTime);
	}

	// El objeto sube un poco, flota y gira (cerca de la cámara).
	if (bNear && bFloatAndSpin && bHasRest)
	{
		if (USceneComponent* Target = FloatTarget.Get())
		{
			const FVector Rel = Target->GetRelativeLocation();
			const float Z = RestZ + (FloatLift + FloatBob * FMath::Sin(Clock * 2.1f)) * Grow;
			const FQuat Spin(FVector::UpVector, FMath::DegreesToRadians(FMath::Fmod(Clock * SpinTurnsPerSecond * 360.f, 360.f)));
			Target->SetRelativeLocationAndRotation(FVector(Rel.X, Rel.Y, Z), Spin * RestRotation);
		}
	}
}
