#include "World/Beach/TN_BeachUrchinSpikes.h"

#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "TN_BeachTrapKit.h"
#include "World/Beach/TN_BeachTrapStatusComponent.h"

namespace TNBeachUrchinSpikesDetail
{
	using namespace TNBeachCreatureRules::UrchinSpikes;

	/** Montículo de arena removida con el lomo oscuro del erizo apenas asomando. */
	void BuildMound(TNBeachTrapKit::FBuffers& B, double R)
	{
		TNPlaygroundKit::AddFrustum(B, FVector(0.0, 0.0, -4.0), FVector(0.0, 0.0, 16.0), R, R * 0.7, 20, TNBeachTrapKit::SandSide(),
			TNBeachTrapKit::SandMark(), false, true);
		TNPlaygroundKit::AddEllipsoid(B, FVector(0.0, 0.0, 10.0), FVector::ForwardVector, FVector::RightVector, FVector::UpVector,
			FVector(R * 0.45, R * 0.45, 18.0), 10, 4, TNPlaygroundKit::Rgb(0x3B2347, 0.3f));
	}

	/** Pinchos: conos morados oscuros repartidos por el lomo (origen en la arena; la malla sube y baja entera). */
	void BuildSpikes(TNBeachTrapKit::FBuffers& B, double R, double H, uint32 Seed)
	{
		const FLinearColor Dark = TNPlaygroundKit::Rgb(0x2E1A3A, 0.35f);
		const FLinearColor Tip = TNPlaygroundKit::Rgb(0xC9A3E0, 0.4f);
		constexpr int32 Count = 26;
		for (int32 k = 0; k < Count; ++k)
		{
			const double A = TNPlaygroundKit::KitTwoPi * 3.0 * (k + 0.5 * TNPlaygroundKit::Hash01(k, 1, Seed)) / Count;
			const double D = R * 0.75 * FMath::Sqrt(TNPlaygroundKit::Hash01(k, 2, Seed));
			const FVector Base(FMath::Cos(A) * D, FMath::Sin(A) * D, 0.0);
			const FVector Lean = Base.GetSafeNormal2D() * (0.35 * D / FMath::Max(1.0, R));
			const double Len = H * (0.7 + 0.3 * TNPlaygroundKit::Hash01(k, 3, Seed));
			const FVector End = Base + (FVector::UpVector + Lean).GetSafeNormal() * Len;
			const FVector Mid = FMath::Lerp(Base, End, 0.8);
			TNPlaygroundKit::AddFrustum(B, Base, Mid, 7.0, 2.5, 5, Dark, Dark, false, false);
			TNPlaygroundKit::AddFrustum(B, Mid, End, 2.5, 0.3, 5, Tip, Tip, false, false);
		}
	}

	/** Altura de los pinchos (cm respecto a su posición fuera) en cada fase. */
	double SpikeOffset(EPhase Phase, double Age, const FTimes& T, double H)
	{
		const double Hidden = -0.85 * H;
		switch (Phase)
		{
			case EPhase::Tell:     return Hidden + 0.12 * H + 6.0 * FMath::Sin(Age * 90.0);
			case EPhase::Out:      return FMath::Lerp(Hidden, 0.0, FMath::Clamp((Age - T.Tell) / 0.06, 0.0, 1.0));
			case EPhase::Recharge: return FMath::Lerp(0.0, Hidden, FMath::Clamp((Age - T.Tell - T.Out) / 0.5, 0.0, 1.0));
			default:               return Hidden;
		}
	}
}

ATN_BeachUrchinSpikes::ATN_BeachUrchinSpikes()
{
	PrimaryActorTick.bCanEverTick = true;

	MoundMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MoundMesh"));
	MoundMesh->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureVisual(MoundMesh);

	SpikesMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SpikesMesh"));
	SpikesMesh->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureVisual(SpikesMesh);
}

void ATN_BeachUrchinSpikes::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_BeachUrchinSpikes, TriggerAt);
}

TNBeachCreatureRules::UrchinSpikes::FTimes ATN_BeachUrchinSpikes::Times() const
{
	TNBeachCreatureRules::UrchinSpikes::FTimes T;
	T.Tell = TellSeconds;
	T.Out = OutSeconds;
	T.Recharge = RechargeSeconds;
	return T;
}

void ATN_BeachUrchinSpikes::ApplySpec()
{
	using namespace TNBeachUrchinSpikesDetail;
	Radius = 0.85 * TNBeachTrapKit::FitRadius(Spec.Element, Spec.SizeScale);
	SpikeHeight = FMath::Clamp(Radius * 0.45, 80.0, 150.0);
	const uint32 Seed = TNBeachTrapKit::SeedOf(Spec.Seed, 687u);
	TNBeachTrapKit::FBuffers Mound;
	BuildMound(Mound, Radius);
	TNBeachTrapKit::SetMesh(MoundMesh, this, Mound);
	TNBeachTrapKit::FBuffers Spikes;
	BuildSpikes(Spikes, Radius, SpikeHeight, Seed);
	TNBeachTrapKit::SetMesh(SpikesMesh, this, Spikes);
	SpikesMesh->SetRelativeLocation(FVector(0.0, 0.0, -0.85 * SpikeHeight));
}

bool ATN_BeachUrchinSpikes::IsTickBusy() const
{
	return TriggerAt >= 0.f && !TNBeachCreatureRules::UrchinSpikes::CanTrigger(TNBeachTrapKit::ServerNow(GetWorld()), TriggerAt, Times());
}

bool ATN_BeachUrchinSpikes::IsOnSpikes(const ACharacter* Turtle, double Reach) const
{
	if (!IsValid(Turtle))
	{
		return false;
	}
	const FVector Local = GetActorTransform().InverseTransformPositionNoScale(Turtle->GetActorLocation());
	const double FeetZ = Local.Z - Turtle->GetSimpleCollisionHalfHeight();
	return FVector2D(Local.X, Local.Y).Size() <= Reach && FeetZ <= SpikeHeight * 0.6 && FeetZ >= -80.0;
}

void ATN_BeachUrchinSpikes::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (GetNetMode() != NM_Client)
	{
		ServerTick(TNBeachTrapKit::ServerNow(World));
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		VisualTick(DeltaSeconds, Clock.Advance(World, DeltaSeconds));
	}
}

void ATN_BeachUrchinSpikes::ServerTick(double Now)
{
	using namespace TNBeachCreatureRules::UrchinSpikes;
	const FTimes T = Times();
	TArray<ATortugaCharacter*> Turtles;
	ATN_BeachEnemy::GatherTurtles(this, Turtles);
	if (CanTrigger(Now, TriggerAt, T))
	{
		for (const ATortugaCharacter* Turtle : Turtles)
		{
			if (TNBeachTrapKit::IsFreeTurtle(Turtle) && IsOnSpikes(Turtle, Radius * 0.8))
			{
				TriggerAt = static_cast<float>(Now);
				Struck.Reset();
				ForceNetUpdate();
				break;
			}
		}
		return;
	}
	if (!Strikes(PhaseAt(Now, TriggerAt, T)))
	{
		return;
	}
	for (ATortugaCharacter* Turtle : Turtles)
	{
		if (Struck.Contains(Turtle) || !IsOnSpikes(Turtle, Radius) || !ATN_BeachEnemy::CanBeHit(Turtle))
		{
			continue;
		}
		Struck.Add(Turtle);
		const FVector Out = (Turtle->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		TNBeach::KnockDownTurtle(Turtle, KnockSeconds, Out * 350.f + FVector(0.0, 0.0, 520.f));
		if (UTN_BeachTrapStatusComponent* Status = UTN_BeachTrapStatusComponent::FindOrAddTo(Turtle))
		{
			Status->ServerSlow(SlowFactor, SlowSeconds);
		}
	}
}

void ATN_BeachUrchinSpikes::VisualTick(float DeltaSeconds, double Now)
{
	using namespace TNBeachCreatureRules::UrchinSpikes;
	const FTimes T = Times();
	const EPhase Phase = PhaseAt(Now, TriggerAt, T);
	const double Age = TriggerAt >= 0.f ? Now - TriggerAt : 0.0;
	SpikesMesh->SetRelativeLocation(FVector(0.0, 0.0, TNBeachUrchinSpikesDetail::SpikeOffset(Phase, Age, T, SpikeHeight)));
	if (Phase == EPhase::Out && ShownPhase != EPhase::Out)
	{
		const FVector At = GetActorLocation() + FVector(0.0, 0.0, 40.0);
		if (SpringSound) { UGameplayStatics::SpawnSoundAtLocation(this, SpringSound, At); }
		if (SpringVFX) { UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, SpringVFX, At); }
	}
	ShownPhase = Phase;
}

void ATN_BeachUrchinSpikes::OnRep_TriggerAt()
{
	Struck.Reset();
}
