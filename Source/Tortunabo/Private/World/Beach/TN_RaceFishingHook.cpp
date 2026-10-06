#include "World/Beach/TN_RaceFishingHook.h"
#include "TN_BeachEnemyKit.h"
#include "TN_RaceItemArtExtra.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "World/Beach/TN_BeachRaceGenerator.h"
#include "World/Beach/TN_RaceItemRules.h"
#include "World/Beach/TN_RaceItems.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TN_Log.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Player/TN_TurtleMovementComponent.h"
#include "Player/TortugaCharacter.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto del
// bloque.
namespace TNRaceFishingHookDetail
{
	/** Tras soltarse o rebotar, lo que tarda en desaparecer (s) y la vida máxima del anzuelo (s). */
	constexpr float SnapSeconds = 0.6f;
	constexpr float MaxLifeSeconds = 5.f;
	/** La caña en la aleta: escala de la pieza (120 cm), a la derecha y por encima del centro de la cápsula, y su cabeceo. */
	constexpr double RodScale = 0.5;
	constexpr double RodSide = 28.0;
	constexpr double RodUp = 10.0;
	constexpr double RodPitch = 35.0;
	/** Altura (cm) del punto de la espalda donde engancha, sobre el centro de la cápsula. */
	constexpr double BackUp = 40.0;
	/** Arco del anzuelo en vuelo (cm de flecha en el centro). */
	constexpr double CastArc = 180.0;

	/** Media cápsula (cm) de una tortuga. */
	inline float HalfHeightOf(const ACharacter* Turtle)
	{
		const UCapsuleComponent* Capsule = Turtle ? Turtle->GetCapsuleComponent() : nullptr;
		return Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.f;
	}

	/** La pescadora puede ser remolcada ahora (de pie, sin aturdir ni derribar, sin que la lleve nada). */
	inline bool CanTow(const ATortugaCharacter* Fisher)
	{
		return TNRaceItems::CanUseNow(Fisher);
	}

	/** Coloca el sedal (cilindro de 1 cm hacia +X) de From a To. */
	inline void PlaceLine(UStaticMeshComponent* Line, const FVector& From, const FVector& To)
	{
		if (!Line)
		{
			return;
		}
		const FVector Delta = To - From;
		const double Length = Delta.Size();
		Line->SetVisibility(Length > 1.0);
		if (Length > 1.0)
		{
			Line->SetWorldLocationAndRotation(From, Delta.Rotation());
			Line->SetWorldScale3D(FVector(Length, 1.0, 1.0));
		}
	}
}

ATN_RaceFishingHook::ATN_RaceFishingHook()
{
	SetNetUpdateFrequency(10.f);
}

void ATN_RaceFishingHook::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_RaceFishingHook, Target);
	DOREPLIFETIME(ATN_RaceFishingHook, Phase);
	DOREPLIFETIME(ATN_RaceFishingHook, PhaseTime);
}

bool ATN_RaceFishingHook::ServerCast(ATortugaCharacter* Fisher)
{
	using namespace TNRaceItemRules;
	UWorld* World = Fisher ? Fisher->GetWorld() : nullptr;
	if (!World || !Fisher->HasAuthority())
	{
		return false;
	}
	if (CountOf(World, StaticClass()) >= TNRaceFishingHook::MaxInWorld)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Caña: ya hay %d anzuelos en la playa."), TNRaceFishingHook::MaxInWorld);
		return false;
	}
	// La tortuga de delante más cercana a su alcance (la primera nunca tiene a nadie).
	TArray<ATortugaCharacter*> Racers;
	TNRaceItems::GatherRacers(Fisher, Racers);
	TArray<ATortugaCharacter*> Others;
	TArray<FRodCandidate> Candidates;
	const FVector From = Fisher->GetActorLocation();
	for (ATortugaCharacter* Other : Racers)
	{
		if (!Other || Other == Fisher)
		{
			continue;
		}
		FRodCandidate Candidate;
		Candidate.Progress = TNRaceItems::CourseProgress(Fisher, Other->GetActorLocation());
		Candidate.Distance = static_cast<float>(FVector::Dist(From, Other->GetActorLocation()));
		Candidates.Add(Candidate);
		Others.Add(Other);
	}
	const int32 Pick = PickRodTarget(TNRaceItems::CourseProgress(Fisher, From), Candidates, RodRange);
	if (Pick == INDEX_NONE)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Caña: %s no tiene a nadie por delante a menos de %.0f m."), *GetNameSafe(Fisher), RodRange / 100.f);
		return false;
	}
	const FTransform SpawnXf(Fisher->GetActorRotation(), From);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	ATN_RaceFishingHook* Hook = World->SpawnActor<ATN_RaceFishingHook>(StaticClass(), SpawnXf, Params);
	if (!Hook)
	{
		return false;
	}
	Hook->SetOwnerTurtle(Fisher);
	Hook->Target = Others[Pick];
	Hook->Phase = TNRaceFishingHook::PhaseFlying;
	Hook->FinishSpawning(SpawnXf);
	Hook->PhaseTime = Hook->StartServerTime;
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] %s lanza la caña a %s (%.1f m)."), *GetNameSafe(Fisher), *GetNameSafe(Others[Pick]),
		Candidates[Pick].Distance / 100.f);
	return true;
}

void ATN_RaceFishingHook::ServerSetPhase(uint8 NewPhase)
{
	Phase = NewPhase;
	PhaseTime = static_cast<float>(ServerNow());
	OnRep_Phase();
	ForceNetUpdate();
}

void ATN_RaceFishingHook::ServerTick(float DeltaSeconds)
{
	using namespace TNRaceItemRules;
	const double Age = GetAge();
	if (Age > TNRaceFishingHookDetail::MaxLifeSeconds)
	{
		ServerFinish(0.2f);
		return;
	}
	const double InPhase = ServerNow() - static_cast<double>(PhaseTime);
	switch (Phase)
	{
		case TNRaceFishingHook::PhaseFlying:
			if (!IsValid(Target) || !IsValid(OwnerTurtle))
			{
				ServerSetPhase(TNRaceFishingHook::PhaseSnapped);
			}
			else if (Age >= HookFlightSeconds)
			{
				ServerHookArrives();
			}
			break;
		case TNRaceFishingHook::PhaseTowing:
			if (InPhase >= TowSeconds)
			{
				// Al acabar, la suelta (ya va cayendo por delante de la enganchada).
				ServerFinish(0.3f);
			}
			break;
		default:
			if (InPhase >= TNRaceFishingHookDetail::SnapSeconds)
			{
				ServerFinish(0.2f);
			}
			break;
	}
}

void ATN_RaceFishingHook::ServerHookArrives()
{
	using namespace TNRaceItemRules;
	ATortugaCharacter* Fisher = OwnerTurtle;
	// El protector solar (o el vuelo en el pelícano) lo anula: rebota. Con la carrera parada, tampoco engancha.
	if (TNRaceItems::IsInvulnerable(Target) || !ATN_BeachEnemy::IsRaceLive(this) || !TNRaceFishingHookDetail::CanTow(Fisher))
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] El anzuelo de %s no engancha a %s%s."), *GetNameSafe(Fisher), *GetNameSafe(Target),
			TNRaceItems::IsInvulnerable(Target) ? TEXT(" (protector solar)") : TEXT(""));
		ServerSetPhase(TNRaceFishingHook::PhaseSnapped);
		return;
	}
	const ATN_BeachRaceGenerator* Generator = GetGenerator();
	FVector Course = Generator ? Generator->GetSeaDirection() : FVector::ForwardVector;
	Course = Flat(Course).IsNearlyZero() ? FVector::ForwardVector : Flat(Course);
	const FVector Right(-Course.Y, Course.X, 0.0);
	const FVector From = Fisher->GetActorLocation();
	const FVector TargetAt = Target->GetActorLocation();
	const float Side = FVector::DotProduct(From - TargetAt, Right) >= 0.0 ? 1.f : -1.f;
	FVector Landing = TowLanding(TargetAt, Target->GetVelocity(), Course, Side, TowSeconds, TowOvertake, TowSide);
	const float HalfHeight = TNRaceFishingHookDetail::HalfHeightOf(Fisher);
	Landing.Z = GroundHeightAt(Landing, static_cast<float>(TargetAt.Z) - HalfHeight) + HalfHeight + 10.f;
	const UCharacterMovementComponent* Move = Fisher->GetCharacterMovement();
	const float GravityZ = Move ? Move->GetGravityZ() : -980.f;
	const FVector Launch = BallisticVelocity(From, Landing, TowSeconds, GravityZ, TowMaxSpeed);
	UTN_TurtleMovementComponent::LaunchFromServer(Fisher, Launch);
	Fisher->SetFallImmuneUntilLanded();
	ServerSetPhase(TNRaceFishingHook::PhaseTowing);
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] %s engancha a %s y se remolca %.1f m."), *GetNameSafe(Fisher), *GetNameSafe(Target),
		FVector::Dist2D(From, Landing) / 100.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Visual
// ─────────────────────────────────────────────────────────────────────────────

FVector ATN_RaceFishingHook::RodTip() const
{
	using namespace TNRaceFishingHookDetail;
	const ATortugaCharacter* Fisher = OwnerTurtle;
	if (!Fisher)
	{
		return GetActorLocation();
	}
	const FVector Grip = Fisher->GetActorLocation() + Fisher->GetActorRightVector() * RodSide + FVector(0.0, 0.0, RodUp);
	const FRotator Aim(RodPitch, Fisher->GetActorRotation().Yaw, 0.f);
	return Grip + Aim.Vector() * (120.0 * RodScale);
}

FVector ATN_RaceFishingHook::TargetBack() const
{
	const ATortugaCharacter* Victim = Target;
	return Victim ? Victim->GetActorLocation() + FVector(0.0, 0.0, TNRaceFishingHookDetail::BackUp) : GetActorLocation();
}

void ATN_RaceFishingHook::BuildVisuals()
{
	using namespace TNRaceItemArtExtra;
	USceneComponent* Root = GetRootComponent();
	if (!Root)
	{
		return;
	}
	RodMesh = TNBeachKit::AddPart(this, Root, GetRidePiece(ERidePiece::Rod), FVector::ZeroVector, true);
	LineMesh = TNBeachKit::AddPart(this, Root, GetRidePiece(ERidePiece::FishLine), FVector::ZeroVector, false);
	HookMesh = TNBeachKit::AddPart(this, Root, GetRidePiece(ERidePiece::Hook), FVector::ZeroVector, false);
	for (UStaticMeshComponent* Comp : { RodMesh.Get(), LineMesh.Get(), HookMesh.Get() })
	{
		if (Comp)
		{
			Comp->SetAbsolute(true, true, true);
		}
	}
	if (RodMesh)
	{
		RodMesh->SetWorldScale3D(FVector(TNRaceFishingHookDetail::RodScale));
	}
	PlaySfx(ETNRaceSound::Throw, 0.9f);
}

void ATN_RaceFishingHook::OnRep_Phase()
{
	if (!bHasScreen || PlayedPhase == Phase)
	{
		return;
	}
	PlayedPhase = Phase;
	if (Phase == TNRaceFishingHook::PhaseTowing)
	{
		PlaySfx(ETNRaceSound::Reel, 1.f, 1.2f);
	}
	else if (Phase == TNRaceFishingHook::PhaseSnapped)
	{
		SnapFrom = HookMesh ? HookMesh->GetComponentLocation() : TargetBack();
		PlaySfx(ETNRaceSound::Bonk, 1.5f, 0.8f);
	}
}

void ATN_RaceFishingHook::VisualTick(float DeltaSeconds)
{
	using namespace TNRaceFishingHookDetail;
	const ATortugaCharacter* Fisher = OwnerTurtle;
	if (!Fisher)
	{
		return;
	}
	const FVector Tip = RodTip();
	if (RodMesh)
	{
		const FVector Grip = Fisher->GetActorLocation() + Fisher->GetActorRightVector() * RodSide + FVector(0.0, 0.0, RodUp);
		RodMesh->SetWorldLocationAndRotation(Grip, FRotator(RodPitch, Fisher->GetActorRotation().Yaw, 0.f));
	}
	FVector HookAt = TargetBack();
	const double InPhase = ServerNow() - static_cast<double>(PhaseTime);
	if (Phase == TNRaceFishingHook::PhaseFlying)
	{
		// Vuela en arco de la punta de la caña a la espalda de la enganchada.
		const double Alpha = FMath::Clamp(GetAge() / static_cast<double>(TNRaceItemRules::HookFlightSeconds), 0.0, 1.0);
		HookAt = FMath::Lerp(Tip, TargetBack(), Alpha) + FVector(0.0, 0.0, CastArc * 4.0 * Alpha * (1.0 - Alpha));
	}
	else if (Phase == TNRaceFishingHook::PhaseSnapped)
	{
		// Se suelta y cae.
		const double Fall = FMath::Clamp(InPhase / static_cast<double>(SnapSeconds), 0.0, 1.0);
		HookAt = SnapFrom - FVector(0.0, 0.0, 160.0 * Fall * Fall);
	}
	SetActorLocation(HookAt);
	if (HookMesh)
	{
		HookMesh->SetWorldLocation(HookAt);
	}
	PlaceLine(LineMesh, Tip, HookAt);
}

void ATN_RaceFishingHook::OnFinished()
{
	for (UStaticMeshComponent* Comp : { RodMesh.Get(), LineMesh.Get(), HookMesh.Get() })
	{
		if (Comp)
		{
			Comp->SetVisibility(false);
		}
	}
}
