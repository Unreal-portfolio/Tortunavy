#include "Vehicles/TN_BuggyDustComponent.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyEngineAudioComponent.h"
#include "Vehicles/TN_BuggyMath.h"
#include "Vehicles/TN_RallyFXParticles.h"
#include "ChaosVehicleWheel.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Rally/TN_RallyGameState.h"
#include "Rally/TN_RallyTrack.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

// ── Reglas puras ──────────────────────────────────────────────────────────────

float TNBuggyFX::DustRate(float SpeedCms, float Skid01)
{
	const float Speed = FMath::Abs(SpeedCms);
	if (Speed < MinDustSpeedCms)
	{
		return 0.f;
	}
	const float Roll = FMath::Clamp((Speed - MinDustSpeedCms) / (FullDustSpeedCms - MinDustSpeedCms), 0.f, 1.f);
	return RollDustRate * Roll + SkidDustRate * FMath::Clamp(Skid01, 0.f, 1.f);
}

bool TNBuggyFX::LeavesMark(float Skid01, bool bRearWheel)
{
	return Skid01 >= (bRearWheel ? RearMarkSkid : FrontMarkSkid);
}

float TNBuggyFX::LandingVolume(float FallSpeedCms)
{
	return FMath::Clamp((FallSpeedCms - MinLandingFallCms) / (FullLandingFallCms - MinLandingFallCms), 0.f, 1.f);
}

bool TNBuggyFX::IsWading(float WheelBottomZ, bool bHasWater, double WaterZ)
{
	return bHasWater && static_cast<double>(WheelBottomZ) < WaterZ;
}

float TNBuggyFX::SplashRateAt(float SpeedCms)
{
	const float Speed = FMath::Abs(SpeedCms);
	if (Speed < MinSplashSpeedCms)
	{
		return 0.f;
	}
	return SplashRate * FMath::Clamp(Speed / FullSplashSpeedCms, 0.f, 1.f);
}

// ── Estado y ayudas del componente ───────────────────────────────────────────

namespace TNBuggyDust
{
	using TNAmbientFX::EShape;
	using TNAmbientFX::FEmitterDesc;

	constexpr int32 WheelCount = 4;
	/** Ruedas traseras en el orden de ATN_Buggy::WheelBoneNames. */
	constexpr int32 FirstRearWheel = 2;
	constexpr float DefaultWheelRadiusCm = 38.f;
	/** La rueda toca el suelo si este queda a menos de su radio más esto (cm); la traza mira TraceBelowCm más abajo. */
	constexpr float ContactMarginCm = 15.f;
	constexpr float TraceBelowCm = 45.f;
	/** Tramos de marca: largo mínimo, ancho y altura sobre el suelo (cm); la malla Flake mide 100 × 60. */
	constexpr float MarkSegmentCm = 45.f;
	constexpr float MarkWidthCm = 22.f;
	constexpr float MarkLiftCm = 2.f;
	constexpr float FlakeLengthCm = 100.f;
	constexpr float FlakeWidthCm = 60.f;
	/** Un tramo más largo que esto (teletransporte, reaparición) no se pinta (cm). */
	constexpr float MaxMarkSegmentCm = 300.f;
	constexpr int32 MaxMarks = 192;
	/** Edad de un hueco de marca libre. */
	constexpr float DeadMarkAge = 1.0e6f;
	constexpr float MarkUpdateSeconds = 0.1f;
	/** Tiempo mínimo en el aire para que la vuelta al suelo cuente como aterrizaje (s). */
	constexpr float MinAirSeconds = 0.25f;
	/** Polvo de un aterrizaje entero por rueda. */
	constexpr float LandDustPerWheel = 10.f;
	/** Granos de arena por bocanada de polvo con el derrape entero. */
	constexpr float GrainsPerPuff = 1.5f;
	/** Velocidad (cm/s) de referencia del derrape entero, como el sonido. */
	constexpr float SkidMinSlipDeg = 12.f;
	constexpr float SkidFullSlipDeg = 35.f;
	constexpr float SkidFullSpeedCms = 1500.f;

	// Más clara que la arena del suelo: si no, la nube no se distingue de la playa.
	const FLinearColor DustColor(0.98f, 0.95f, 0.86f);
	const FLinearColor GrainColor(0.62f, 0.5f, 0.32f);
	const FLinearColor WaterColor(0.85f, 0.95f, 1.f);
	const FLinearColor MarkColor(0.22f, 0.17f, 0.11f);
	constexpr float MarkAlpha = 0.7f;
	const TCHAR* const LandSoundPath = TEXT("/Game/Audio/Rally/SFX_Buggy_Land.SFX_Buggy_Land");

	struct FWheelProbe
	{
		bool bContact = false;
		bool bWading = false;
		FVector Point = FVector::ZeroVector;
		FVector Normal = FVector::UpVector;
	};

	struct FState
	{
		TNRallyParticles::FEmitterSet Set;
		int32 Dust = INDEX_NONE;
		int32 Grains = INDEX_NONE;
		int32 Splash = INDEX_NONE;
		/** Actor local sin física que aloja las mallas de partículas y marcas (no el buggy: su raíz simula física). */
		TWeakObjectPtr<AActor> Host;
		TWeakObjectPtr<UInstancedStaticMeshComponent> MarkISM;
		TArray<FTransform> MarkXf;
		TArray<float> MarkAge;
		int32 NextMark = 0;
		float MarkClock = 0.f;
		FVector LastMark[WheelCount];
		bool bHasLastMark[WheelCount] = {};
		float DustAccum[WheelCount] = {};
		float SplashAccum[WheelCount] = {};
		float AirSeconds = 0.f;
		float FallPeak = 0.f;
	};

	FEmitterDesc DustDesc()
	{
		FEmitterDesc D;
		D.Shape = EShape::Puff;
		D.bSoft = true;
		D.bCloud = true;
		D.Color = DustColor;
		D.Alpha = 0.7f;
		D.MaxParticles = 90;
		D.Rate = 0.f;
		D.SpawnRadius = 25.f;
		D.SpawnHeight = 8.f;
		D.Speed = 260.f;
		D.SpeedJitter = 0.5f;
		D.Spread = 0.7f;
		D.Gravity = -40.f;
		D.Buoyancy = 25.f;
		D.Drag = 1.8f;
		D.LifeMin = 0.7f;
		D.LifeMax = 1.3f;
		D.SizeStart = 24.f;
		D.SizeEnd = 120.f;
		D.WakeDistance = 12000.f;
		return D;
	}

	FEmitterDesc GrainsDesc()
	{
		FEmitterDesc D = DustDesc();
		D.Shape = EShape::Ember;
		D.bSoft = false;
		D.bCloud = false;
		D.Color = GrainColor;
		D.MaxParticles = 60;
		D.Speed = 420.f;
		D.Gravity = -980.f;
		D.Buoyancy = 0.f;
		D.Drag = 0.4f;
		D.LifeMin = 0.35f;
		D.LifeMax = 0.6f;
		D.SizeStart = 3.f;
		D.SizeEnd = 2.f;
		return D;
	}

	FEmitterDesc SplashDesc()
	{
		FEmitterDesc D = GrainsDesc();
		D.Shape = EShape::Drop;
		D.bSoft = true;
		D.Alpha = 0.7f;
		D.Color = WaterColor;
		D.MaxParticles = 90;
		D.Speed = 520.f;
		D.Spread = 0.6f;
		D.LifeMin = 0.4f;
		D.LifeMax = 0.75f;
		D.SizeStart = 7.f;
		D.SizeEnd = 4.f;
		return D;
	}

	void EnsureEmitters(AActor* Buggy, FState& S)
	{
		if (S.Dust != INDEX_NONE)
		{
			return;
		}
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Owner = Buggy->GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		if (!Owner)
		{
			return;
		}
		S.Host = Owner;
		for (const FEmitterDesc& Desc : { DustDesc(), GrainsDesc(), SplashDesc() })
		{
			if (!TNRallyParticles::AddEmitter(Owner, S.Set, Desc, Buggy->GetActorLocation()))
			{
				return;
			}
		}
		S.Dust = 0;
		S.Grains = 1;
		S.Splash = 2;
		UStaticMesh* MarkMesh = TNAmbientFX::ShapeMesh(EShape::Flake, MarkColor, true, MarkAlpha);
		S.MarkISM = TNAmbientFX::MakeISM(Owner, MarkMesh, MaxMarks, false);
		S.MarkXf.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), MaxMarks);
		S.MarkAge.Init(DeadMarkAge, MaxMarks);
	}

	/** Suelo bajo la rueda Index: traza desde su centro hacia abajo; si no hay suelo cerca, la rueda va en el aire. */
	FWheelProbe ProbeWheel(const ATN_Buggy& Buggy, int32 Index, bool bHasWater, double WaterZ)
	{
		FWheelProbe Probe;
		const USkeletalMeshComponent* Chassis = Buggy.GetMesh();
		const UChaosWheeledVehicleMovementComponent* Move = Buggy.GetWheeledMovement();
		const UChaosVehicleWheel* Wheel = Move && Move->Wheels.IsValidIndex(Index) ? Move->Wheels[Index].Get() : nullptr;
		const float Radius = Wheel ? Wheel->GetWheelRadius() : DefaultWheelRadiusCm;
		const FVector Center = Chassis->GetBoneLocation(ATN_Buggy::WheelBoneNames[Index]);
		const FVector Down = -Buggy.GetActorUpVector();
		FHitResult Hit;
		FCollisionQueryParams Query(FName(TEXT("TNBuggyDust")), false, &Buggy);
		const bool bHit = Buggy.GetWorld()->LineTraceSingleByChannel(Hit, Center, Center + Down * (Radius + TraceBelowCm), ECC_Visibility, Query);
		Probe.bWading = TNBuggyFX::IsWading(static_cast<float>(Center.Z) - Radius, bHasWater, WaterZ);
		Probe.bContact = bHit && Hit.Distance <= Radius + ContactMarginCm;
		Probe.Point = bHit ? FVector(Hit.ImpactPoint) : Center + Down * Radius;
		Probe.Normal = bHit ? FVector(Hit.ImpactNormal) : FVector::UpVector;
		if (Probe.bWading)
		{
			// En el agua, la rueda salpica en la superficie aunque el fondo quede más abajo.
			Probe.Point.Z = FMath::Max(Probe.Point.Z, WaterZ);
		}
		return Probe;
	}

	/** Lanza las partículas que tocan a una rueda este fotograma (acumulando las fracciones). Devuelve cuántas. */
	int32 EmitFrom(TNAmbientFX::FEmitter& Emitter, float& Accum, float Rate, float Dt, const FVector& Where, const FVector& Direction)
	{
		Accum += Rate * Dt;
		const int32 Count = FMath::FloorToInt(Accum);
		if (Count <= 0)
		{
			return 0;
		}
		Accum -= static_cast<float>(Count);
		TNRallyParticles::BurstAt(Emitter, Where, Direction, Count);
		return Count;
	}

	/** Pone un tramo de marca de From a To sobre el suelo de normal Normal (anillo: se recicla el más viejo). */
	void AddMark(FState& S, const FVector& From, const FVector& To, const FVector& Normal)
	{
		// La base tiene que ser ortonormal: el tramo no es exactamente perpendicular a la normal del suelo y
		// una base sesgada da un cuaternio sin normalizar que hace saltar IsRotationNormalized en la ISM.
		const FVector Up = Normal.GetSafeNormal();
		const FVector Along = FVector::VectorPlaneProject(To - From, Up).GetSafeNormal();
		if (Up.IsNearlyZero() || Along.IsNearlyZero())
		{
			return;
		}
		FQuat Rotation = FRotationMatrix::MakeFromXZ(Along, Up).ToQuat();
		Rotation.Normalize();
		const float Length = static_cast<float>(FVector::Dist(From, To));
		S.MarkXf[S.NextMark] = FTransform(Rotation, (From + To) * 0.5 + Up * MarkLiftCm,
			FVector(Length / FlakeLengthCm, MarkWidthCm / FlakeWidthCm, 1.f));
		S.MarkAge[S.NextMark] = 0.f;
		S.NextMark = (S.NextMark + 1) % MaxMarks;
	}

	/** Envejece las marcas y, cada MarkUpdateSeconds, estrecha las del último segundo y esconde las acabadas. */
	void AgeMarks(FState& S, float Dt, float MarkSeconds, bool bForce)
	{
		UInstancedStaticMeshComponent* ISM = S.MarkISM.Get();
		S.MarkClock += Dt;
		if (!ISM || (!bForce && S.MarkClock < MarkUpdateSeconds))
		{
			return;
		}
		bool bChanged = bForce;
		for (int32 Index = 0; Index < S.MarkAge.Num(); ++Index)
		{
			if (S.MarkAge[Index] >= DeadMarkAge)
			{
				continue;
			}
			S.MarkAge[Index] += S.MarkClock;
			const float Left = MarkSeconds - S.MarkAge[Index];
			if (Left <= 1.f)
			{
				FVector Scale = S.MarkXf[Index].GetScale3D();
				Scale.Y = Left <= 0.f ? 0.f : (MarkWidthCm / FlakeWidthCm) * Left;
				S.MarkXf[Index].SetScale3D(Left <= 0.f ? FVector::ZeroVector : Scale);
				S.MarkAge[Index] = Left <= 0.f ? DeadMarkAge : S.MarkAge[Index];
				bChanged = true;
			}
		}
		S.MarkClock = 0.f;
		if (bChanged)
		{
			ISM->BatchUpdateInstancesTransforms(0, S.MarkXf, true, true, false);
		}
	}
}

// ── Componente ────────────────────────────────────────────────────────────────

UTN_BuggyDustComponent::UTN_BuggyDustComponent()
{
	// Tras la física: el polvo sale de donde ya están las ruedas en este fotograma.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	SetIsReplicatedByDefault(false);
	static ConstructorHelpers::FObjectFinder<USoundBase> LandFinder(TNBuggyDust::LandSoundPath);
	LandSound = LandFinder.Object;
}

ATN_Buggy* UTN_BuggyDustComponent::GetBuggy() const
{
	return Cast<ATN_Buggy>(GetOwner());
}

void UTN_BuggyDustComponent::BeginPlay()
{
	Super::BeginPlay();
	// Sin pantalla (servidor dedicado, -nullrhi) no hay nada que pintar.
	if (GetNetMode() == NM_DedicatedServer || !FApp::CanEverRender() || !GetBuggy())
	{
		return;
	}
	State = MakeShared<TNBuggyDust::FState>();
	SetComponentTickEnabled(true);
}

void UTN_BuggyDustComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (State.IsValid())
	{
		UE_LOG(LogTNBuggy, Log, TEXT("[RallyFX] %s: %d partículas de las ruedas, %d tramos de marca, %d aterrizajes"),
			*GetNameSafe(GetOwner()), ParticlesSpawned, MarksPlaced, Landings);
	}
	if (State.IsValid() && State->Host.IsValid())
	{
		State->Host->Destroy();
	}
	State.Reset();
	Super::EndPlay(EndPlayReason);
}

void UTN_BuggyDustComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ATN_Buggy* Buggy = GetBuggy();
	if (!Buggy || !State.IsValid())
	{
		return;
	}
	TNBuggyDust::FState& S = *State;
	TNBuggyDust::EnsureEmitters(Buggy, S);
	if (S.Dust == INDEX_NONE)
	{
		return;
	}
	const float Dt = FMath::Min(DeltaTime, 0.1f);
	// Los emisores se duermen lejos de la cámara según su origen: que siga al buggy (si no, se quedaba en la parrilla y,
	// a 120 m de ella, el polvo se apagaba).
	for (TNAmbientFX::FEmitter& Emitter : S.Set.Emitters)
	{
		Emitter.Origin = Buggy->GetActorLocation();
	}
	const FVector View = TNRallyParticles::LocalView(GetWorld(), Buggy->GetActorLocation());
	if (FVector::DistSquared(View, Buggy->GetActorLocation()) < FMath::Square(static_cast<double>(MaxViewDistanceCm)))
	{
		UpdateWheels(*Buggy, Dt);
	}
	TNRallyParticles::Tick(S.Set, Dt, View);
	TNBuggyDust::AgeMarks(S, Dt, MarkSeconds, false);
}

void UTN_BuggyDustComponent::UpdateWheels(ATN_Buggy& Buggy, float Dt)
{
	using namespace TNBuggyDust;
	const ATN_RallyGameState* RallyState = GetWorld()->GetGameState<ATN_RallyGameState>();
	const ATN_RallyTrack* Track = RallyState ? RallyState->GetTrack() : nullptr;
	const bool bHasWater = Track && Track->HasWaterZ();
	const double WaterZ = bHasWater ? Track->GetWaterZ() : 0.0;
	FWheelProbe Probes[WheelCount];
	bool bAnyContact = false;
	for (int32 Index = 0; Index < WheelCount; ++Index)
	{
		Probes[Index] = ProbeWheel(Buggy, Index, bHasWater, WaterZ);
		bAnyContact |= Probes[Index].bContact || Probes[Index].bWading;
	}
	const FVector Velocity = Buggy.GetVelocity();
	const float Speed = static_cast<float>(Velocity.Size2D());
	// El mismo derrape que suena (UTN_BuggyEngineAudioComponent): deriva entre el morro y la velocidad.
	const float Skid01 = TNBuggyAudio::SkidVolume(TNBuggy::SlipAngleDeg(Buggy.GetActorForwardVector(), Velocity), Speed, bAnyContact,
		SkidMinSlipDeg, SkidFullSlipDeg, SkidFullSpeedCms);
	const FVector Flat(Velocity.X, Velocity.Y, 0.0);
	const FVector Back = Speed > 1.f ? -Flat.GetSafeNormal() : -Buggy.GetActorForwardVector();
	for (int32 Index = 0; Index < WheelCount; ++Index)
	{
		UpdateWheel(Index, Probes[Index], Speed, Skid01, Back, Dt);
	}
	UpdateLanding(Buggy, Probes, bAnyContact, Dt);
}

void UTN_BuggyDustComponent::UpdateWheel(int32 Index, const TNBuggyDust::FWheelProbe& Probe, float SpeedCms, float Skid01,
	const FVector& Back, float Dt)
{
	using namespace TNBuggyDust;
	FState& S = *State;
	const bool bRear = Index >= FirstRearWheel;
	const FVector Spray = (Back * 0.7 + FVector::UpVector * 0.7).GetSafeNormal();
	if (Probe.bWading)
	{
		ParticlesSpawned += EmitFrom(S.Set.Emitters[S.Splash], S.SplashAccum[Index], TNBuggyFX::SplashRateAt(SpeedCms) * Amount, Dt,
			Probe.Point, Spray);
	}
	else if (Probe.bContact && bRear)
	{
		const float Rate = TNBuggyFX::DustRate(SpeedCms, Skid01) * Amount;
		const int32 Puffs = EmitFrom(S.Set.Emitters[S.Dust], S.DustAccum[Index], Rate, Dt, Probe.Point, Spray);
		// Granos de arena solo al derrapar: el rodar normal levanta nube, no tierra.
		const int32 Grains = FMath::RoundToInt(static_cast<float>(Puffs) * Skid01 * GrainsPerPuff);
		if (Grains > 0)
		{
			TNRallyParticles::BurstAt(S.Set.Emitters[S.Grains], Probe.Point, Spray, Grains);
		}
		ParticlesSpawned += Puffs + Grains;
	}
	const bool bMark = Probe.bContact && !Probe.bWading && TNBuggyFX::LeavesMark(Skid01, bRear);
	if (!bMark)
	{
		S.bHasLastMark[Index] = false;
		return;
	}
	if (!S.bHasLastMark[Index])
	{
		S.LastMark[Index] = Probe.Point;
		S.bHasLastMark[Index] = true;
		return;
	}
	const double Step = FVector::Dist(S.LastMark[Index], Probe.Point);
	if (Step >= MarkSegmentCm)
	{
		if (Step <= MaxMarkSegmentCm)
		{
			AddMark(S, S.LastMark[Index], Probe.Point, Probe.Normal);
			++MarksPlaced;
			AgeMarks(S, 0.f, MarkSeconds, true);
		}
		S.LastMark[Index] = Probe.Point;
	}
}

void UTN_BuggyDustComponent::UpdateLanding(const ATN_Buggy& Buggy, const TNBuggyDust::FWheelProbe* Probes, bool bAnyContact, float Dt)
{
	using namespace TNBuggyDust;
	FState& S = *State;
	if (!bAnyContact)
	{
		S.AirSeconds += Dt;
		S.FallPeak = FMath::Max(S.FallPeak, static_cast<float>(-Buggy.GetVelocity().Z));
		return;
	}
	const float Volume = S.AirSeconds >= MinAirSeconds ? TNBuggyFX::LandingVolume(S.FallPeak) : 0.f;
	S.AirSeconds = 0.f;
	S.FallPeak = 0.f;
	if (Volume <= 0.f)
	{
		return;
	}
	++Landings;
	if (LandSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, LandSound, Buggy.GetActorLocation(), FRotator::ZeroRotator, Volume);
	}
	const int32 PerWheel = FMath::RoundToInt(LandDustPerWheel * Volume * Amount);
	for (int32 Index = 0; Index < WheelCount; ++Index)
	{
		if (Probes[Index].bContact || Probes[Index].bWading)
		{
			TNAmbientFX::FEmitter& Emitter = S.Set.Emitters[Probes[Index].bWading ? S.Splash : S.Dust];
			TNRallyParticles::BurstAt(Emitter, Probes[Index].Point, FVector::UpVector, PerWheel);
			ParticlesSpawned += PerWheel;
		}
	}
	UE_LOG(LogTNBuggy, Verbose, TEXT("%s: aterriza (volumen %.2f)"), *Buggy.GetName(), Volume);
}
