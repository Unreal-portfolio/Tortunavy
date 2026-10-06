// Efectos de los objetos del coop en una tortuga (pez globo). Ver TN_CoopItemComponent.h.

#include "Game/TN_CoopItemComponent.h"
#include "TN_CoopItemArt.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Game/TN_ItemRuntime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Net/UnrealNetwork.h"
#include "Player/TN_DizzyBirdsComponent.h"
#include "Player/TN_StaminaComponent.h"
#include "Player/TortugaCharacter.h"

namespace TNCoopItemComponentDetail
{
	/** Quién pone el tope del mareo del pez globo en UTN_StaminaComponent. */
	FName DizzySource()
	{
		static const FName Name(TEXT("CoopPufferDizzy"));
		return Name;
	}

	bool CanRender()
	{
		return !IsRunningDedicatedServer() && FApp::CanEverRender();
	}

	/** Cable del arpón: grosor (escala del cilindro del motor), segundos que tarda en desaparecer y color. */
	constexpr float RopeWidth = 0.035f;
	constexpr float RopeLife = 0.45f;
	const FLinearColor RopeColor(0.45f, 0.32f, 0.2f);
}

UTN_CoopItemComponent::UTN_CoopItemComponent()
{
	// El tick solo hace falta mientras dure algún efecto; se enciende con ApplyEffects.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

void UTN_CoopItemComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UTN_CoopItemComponent, Effects);
}

UTN_CoopItemComponent* UTN_CoopItemComponent::FindOn(const AActor* Turtle)
{
	return Turtle ? Turtle->FindComponentByClass<UTN_CoopItemComponent>() : nullptr;
}

UTN_CoopItemComponent* UTN_CoopItemComponent::FindOrAddOn(ACharacter* Turtle)
{
	if (UTN_CoopItemComponent* Existing = FindOn(Turtle))
	{
		return Existing;
	}
	if (!Turtle || !Turtle->HasAuthority())
	{
		return nullptr;
	}
	// Componente dinámico replicado: el servidor lo crea y cada cliente recibe el suyo.
	UTN_CoopItemComponent* Comp = NewObject<UTN_CoopItemComponent>(Turtle, UTN_CoopItemComponent::StaticClass(), TEXT("CoopItems"));
	if (!Comp)
	{
		return nullptr;
	}
	Turtle->AddInstanceComponent(Comp);
	Comp->RegisterComponent();
	return Comp;
}

bool UTN_CoopItemComponent::IsTurtleProtected(const AActor* Turtle)
{
	const UTN_CoopItemComponent* Comp = FindOn(Turtle);
	return Comp && Comp->IsProtected();
}

double UTN_CoopItemComponent::Now() const
{
	return TNItemRuntime::ServerNow(GetWorld());
}

FTNPufferState UTN_CoopItemComponent::PufferState() const
{
	FTNPufferState State;
	State.ProtectEnd = Effects.ProtectEnd;
	State.DizzyEnd = Effects.DizzyEnd;
	return State;
}

bool UTN_CoopItemComponent::IsProtected() const
{
	return PufferState().IsProtected(Now());
}

bool UTN_CoopItemComponent::IsDizzy() const
{
	return PufferState().IsDizzy(Now());
}

bool UTN_CoopItemComponent::GrantPuffer()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	FTNPufferState State = PufferState();
	if (!State.Start(Now(), TNCoopItemTuning::PufferSeconds, TNCoopItemTuning::PufferDizzySeconds))
	{
		return false;
	}
	Effects.ProtectEnd = static_cast<float>(State.ProtectEnd);
	Effects.DizzyEnd = static_cast<float>(State.DizzyEnd);
	GetOwner()->ForceNetUpdate();
	ApplyEffects();
	return true;
}

void UTN_CoopItemComponent::ClearEffects()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	Effects = FTNCoopEffectNet();
	GetOwner()->ForceNetUpdate();
	ApplyEffects();
}

void UTN_CoopItemComponent::OnRep_Effects()
{
	ApplyEffects();
}

void UTN_CoopItemComponent::ApplyEffects()
{
	using namespace TNCoopItemComponentDetail;
	ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(GetOwner());
	const bool bProtected = IsProtected();
	const bool bDizzy = IsDizzy();

	// Mareo: el tope de velocidad en cada máquina (el dueño lo estrena en su movimiento igual que el servidor) y los pajaritos.
	if (bDizzy != bDizzyApplied)
	{
		if (UTN_StaminaComponent* Stamina = Turtle ? Turtle->GetStaminaComponent() : nullptr)
		{
			if (bDizzy)
			{
				Stamina->SetSpeedCap(DizzySource(), TNCoopItemTuning::PufferDizzySpeedCap);
			}
			else
			{
				Stamina->ClearSpeedCap(DizzySource());
			}
		}
		UTN_DizzyBirdsComponent* Birds = Turtle ? Turtle->FindComponentByClass<UTN_DizzyBirdsComponent>() : nullptr;
		if (Birds && CanRender() && (bDizzy || !Turtle->IsKnockedDown()))
		{
			Birds->SetDizzy(bDizzy);
		}
		bDizzyApplied = bDizzy;
	}

	// Protección: la tortuga inflada de pinchos (con pantalla).
	if (bProtected != bProtectShown)
	{
		if (Turtle && Turtle->HasAuthority())
		{
			// Sonido de empezar y de acabar, una vez para todas las máquinas.
			TNItemRuntime::PlayCue(Turtle, bProtected ? ETNRaceSound::StarUp : ETNRaceSound::StarDown, bProtected ? 0.8f : 0.7f);
		}
		if (CanRender() && Turtle)
		{
			if (!Spikes && bProtected)
			{
				Spikes = NewObject<UStaticMeshComponent>(Turtle, NAME_None, RF_Transient);
				Spikes->SetStaticMesh(TNCoopItemArt::GetPufferSpikesMesh());
				Spikes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Spikes->SetCanEverAffectNavigation(false);
				Spikes->SetCastShadow(false);
				Spikes->SetupAttachment(Turtle->GetRootComponent());
				Spikes->RegisterComponent();
			}
			if (Spikes)
			{
				Spikes->SetVisibility(bProtected);
			}
		}
		bProtectShown = bProtected;
		PulseClock = 0.f;
	}
	RefreshTick();
}

void UTN_CoopItemComponent::RefreshTick()
{
	SetComponentTickEnabled(IsProtected() || IsDizzy() || bDizzyApplied || bProtectShown || Ropes.Num() > 0);
}

void UTN_CoopItemComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (Spikes && bProtectShown)
	{
		// Se infla al empezar y late; parpadea el último segundo para avisar de que se acaba.
		PulseClock += DeltaTime;
		const float Grow = FMath::Min(1.f, PulseClock / 0.25f);
		const float Beat = 1.f + 0.06f * FMath::Sin(PulseClock * 9.f);
		Spikes->SetRelativeScale3D(FVector(Grow * Beat));
		const float Left = static_cast<float>(Effects.ProtectEnd - Now());
		Spikes->SetVisibility(Left > 1.f || FMath::Fmod(PulseClock, 0.2f) < 0.12f);
	}
	TickRopes(DeltaTime);
	// Se acaba la protección o el mareo a la hora del servidor, en cada máquina (sin esperar otra réplica).
	ApplyEffects();
}

UTN_RaceItemSynthComponent* UTN_CoopItemComponent::GetSfx()
{
	AActor* Owner = GetOwner();
	if (!Owner || Owner->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	if (!Sfx)
	{
		Sfx = UTN_RaceItemSynthComponent::AttachTo(Owner, Owner->GetActorLocation(), 1800.f, 14000.f);
	}
	return Sfx;
}

void UTN_CoopItemComponent::MulticastCue_Implementation(ETNRaceSound Sound, float Pitch)
{
	if (UTN_RaceItemSynthComponent* Synth = GetSfx())
	{
		Synth->Play(Sound, Pitch, 1.f);
	}
}

void UTN_CoopItemComponent::MulticastRope_Implementation(FVector_NetQuantize From, FVector_NetQuantize To)
{
	using namespace TNCoopItemComponentDetail;
	AActor* Owner = GetOwner();
	UStaticMesh* Cylinder = CanRender() ? LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")) : nullptr;
	const FVector Start(From);
	const FVector End(To);
	const double Length = FVector::Dist(Start, End);
	if (!Owner || !Cylinder || Length < 1.0)
	{
		return;
	}
	UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transient);
	Mesh->SetStaticMesh(Cylinder);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->SetUsingAbsoluteLocation(true);
	Mesh->SetUsingAbsoluteRotation(true);
	Mesh->SetUsingAbsoluteScale(true);
	Mesh->RegisterComponent();
	// El cilindro del motor mide 100 uu y va a lo largo de Z: se tumba en la dirección del cable.
	const FVector Dir = (End - Start) / Length;
	Mesh->SetWorldLocationAndRotation((Start + End) * 0.5, FRotationMatrix::MakeFromZ(Dir).Rotator());
	Mesh->SetWorldScale3D(FVector(RopeWidth, RopeWidth, Length / 100.0));
	if (UMaterialInstanceDynamic* Material = Mesh->CreateDynamicMaterialInstance(0))
	{
		Material->SetVectorParameterValue(TEXT("Color"), RopeColor);
	}
	FRope Rope;
	Rope.Mesh = Mesh;
	Ropes.Add(Rope);
	RefreshTick();
}

void UTN_CoopItemComponent::TickRopes(float DeltaTime)
{
	using namespace TNCoopItemComponentDetail;
	for (int32 Index = Ropes.Num() - 1; Index >= 0; --Index)
	{
		FRope& Rope = Ropes[Index];
		Rope.Age += DeltaTime;
		UStaticMeshComponent* Mesh = Rope.Mesh.Get();
		if (!Mesh || Rope.Age >= RopeLife)
		{
			if (Mesh)
			{
				Mesh->DestroyComponent();
			}
			Ropes.RemoveAtSwap(Index);
			continue;
		}
		// Se adelgaza hasta desaparecer.
		const float Left = 1.f - Rope.Age / RopeLife;
		const FVector Scale = Mesh->GetComponentScale();
		Mesh->SetWorldScale3D(FVector(RopeWidth * Left, RopeWidth * Left, Scale.Z));
	}
}

void UTN_CoopItemComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Effects = FTNCoopEffectNet();
	if (bDizzyApplied)
	{
		if (const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(GetOwner()))
		{
			if (UTN_StaminaComponent* Stamina = Turtle->GetStaminaComponent())
			{
				Stamina->ClearSpeedCap(TNCoopItemComponentDetail::DizzySource());
			}
		}
		bDizzyApplied = false;
	}
	if (Spikes)
	{
		Spikes->DestroyComponent();
		Spikes = nullptr;
	}
	for (const FRope& Rope : Ropes)
	{
		if (UStaticMeshComponent* Mesh = Rope.Mesh.Get())
		{
			Mesh->DestroyComponent();
		}
	}
	Ropes.Reset();
	Super::EndPlay(EndPlayReason);
}
