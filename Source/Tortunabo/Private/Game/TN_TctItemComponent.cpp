#include "Game/TN_TctItemComponent.h"
#include "Game/TN_TctItemRules.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Net/UnrealNetwork.h"
#include "Player/TN_MovementLimits.h"
#include "Player/TN_StaminaComponent.h"
#include "Player/TortugaCharacter.h"
#include "World/Beach/TN_RaceItems.h"

namespace TNTctItemComponentDetail
{
	/** Quién pone el tope del lastre en UTN_StaminaComponent. */
	FName HeavySource()
	{
		static const FName Name(TEXT("TctAnchor"));
		return Name;
	}

	bool CanRender()
	{
		return !IsRunningDedicatedServer() && FApp::CanEverRender();
	}

	FLinearColor TrailColor(ETNTctItem Kind)
	{
		switch (Kind)
		{
		case ETNTctItem::KnockoutPistol: return FLinearColor(1.f, 0.35f, 0.85f);
		case ETNTctItem::AirBlunderbuss: return FLinearColor(0.75f, 0.95f, 1.f);
		case ETNTctItem::Grapple:        return FLinearColor(0.45f, 0.32f, 0.2f);
		default:                         return FLinearColor::White;
		}
	}
}

UTN_TctItemComponent::UTN_TctItemComponent()
{
	// El tick solo hace falta mientras haya lastre o estelas; se enciende con RefreshTick.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

void UTN_TctItemComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UTN_TctItemComponent, HeavyEnd);
}

UTN_TctItemComponent* UTN_TctItemComponent::FindOn(const AActor* Turtle)
{
	return Turtle ? Turtle->FindComponentByClass<UTN_TctItemComponent>() : nullptr;
}

UTN_TctItemComponent* UTN_TctItemComponent::FindOrAddOn(ACharacter* Turtle)
{
	if (UTN_TctItemComponent* Existing = FindOn(Turtle))
	{
		return Existing;
	}
	if (!Turtle || !Turtle->HasAuthority())
	{
		return nullptr;
	}
	// Componente dinámico replicado: el servidor lo crea y cada cliente recibe el suyo.
	UTN_TctItemComponent* Comp = NewObject<UTN_TctItemComponent>(Turtle, UTN_TctItemComponent::StaticClass(), TEXT("TctItems"));
	if (!Comp)
	{
		return nullptr;
	}
	Turtle->AddInstanceComponent(Comp);
	Comp->RegisterComponent();
	return Comp;
}

double UTN_TctItemComponent::Now() const
{
	return TNRaceItems::ServerNow(GetWorld());
}

// ─────────────────────────────────────────────────────────────────────────────
// Lastre
// ─────────────────────────────────────────────────────────────────────────────

void UTN_TctItemComponent::GrantHeavy(float Seconds)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Seconds <= 0.f)
	{
		return;
	}
	HeavyEnd = FMath::Max(HeavyEnd, static_cast<float>(Now() + Seconds));
	GetOwner()->ForceNetUpdate();
	ApplyHeavy();
}

void UTN_TctItemComponent::ClearEffects()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	HeavyEnd = 0.f;
	GetOwner()->ForceNetUpdate();
	ApplyHeavy();
}

bool UTN_TctItemComponent::IsHeavy() const
{
	return HeavyEnd > 0.f && Now() < HeavyEnd;
}

void UTN_TctItemComponent::OnRep_HeavyEnd()
{
	ApplyHeavy();
}

void UTN_TctItemComponent::ApplyHeavy()
{
	const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(GetOwner());
	UTN_StaminaComponent* Stamina = Turtle ? Turtle->GetStaminaComponent() : nullptr;
	const bool bWant = IsHeavy();
	if (Stamina && bWant != bHeavyApplied)
	{
		if (bWant)
		{
			Stamina->SetSpeedCap(TNTctItemComponentDetail::HeavySource(), TNTctItemTuning::AnchorHeavySpeedCap);
			Stamina->SetJumpLimit(TNTctItemComponentDetail::HeavySource(), TNMovementLimits::NoCap, TNTctItemTuning::AnchorHeavyJumpMultiplier);
		}
		else
		{
			Stamina->ClearSpeedCap(TNTctItemComponentDetail::HeavySource());
			Stamina->ClearJumpLimit(TNTctItemComponentDetail::HeavySource());
		}
	}
	bHeavyApplied = bWant && Stamina;
	RefreshTick();
}

// ─────────────────────────────────────────────────────────────────────────────
// Estelas
// ─────────────────────────────────────────────────────────────────────────────

void UTN_TctItemComponent::MulticastShot_Implementation(uint8 Kind, FVector_NetQuantize From, FVector_NetQuantize To)
{
	if (!TNTctItemComponentDetail::CanRender())
	{
		return;
	}
	const ETNTctItem Item = static_cast<ETNTctItem>(Kind);
	const FLinearColor Color = TNTctItemComponentDetail::TrailColor(Item);
	if (Item != ETNTctItem::AirBlunderbuss)
	{
		AddTrail(From, To, Color, Item == ETNTctItem::Grapple ? 0.035f : 0.06f, Item == ETNTctItem::Grapple ? 0.45f : 0.22f);
		return;
	}
	// El trabuco: un abanico de ráfagas del ancho del cono.
	const FVector Shot = FVector(To) - FVector(From);
	const FVector Dir = Shot.GetSafeNormal();
	const double Length = Shot.Size();
	for (int32 Ray = -2; Ray <= 2; ++Ray)
	{
		const FVector RayDir = Dir.RotateAngleAxis(Ray * TNTctItemTuning::BlunderbussHalfAngleDeg * 0.45f, FVector::UpVector);
		AddTrail(From, FVector(From) + RayDir * Length * (Ray == 0 ? 1.0 : 0.8), Color, 0.12f, 0.3f);
	}
}

void UTN_TctItemComponent::AddTrail(const FVector& From, const FVector& To, const FLinearColor& Color, float Width, float Life)
{
	AActor* Owner = GetOwner();
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	const double Length = FVector::Dist(From, To);
	if (!Owner || !Cylinder || Length < 1.0)
	{
		return;
	}
	UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transient);
	if (!Mesh)
	{
		return;
	}
	Mesh->SetStaticMesh(Cylinder);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->SetUsingAbsoluteLocation(true);
	Mesh->SetUsingAbsoluteRotation(true);
	Mesh->SetUsingAbsoluteScale(true);
	Mesh->RegisterComponent();
	// El cilindro del motor mide 100 uu y va a lo largo de Z: se tumba en la dirección del disparo.
	const FVector Dir = (To - From) / Length;
	Mesh->SetWorldLocationAndRotation((From + To) * 0.5, FRotationMatrix::MakeFromZ(Dir).Rotator());
	Mesh->SetWorldScale3D(FVector(Width, Width, Length / 100.0));
	if (UMaterialInstanceDynamic* Material = Mesh->CreateDynamicMaterialInstance(0))
	{
		Material->SetVectorParameterValue(TEXT("Color"), Color);
	}
	FTrail Trail;
	Trail.Mesh = Mesh;
	Trail.Life = Life;
	Trail.Width = Width;
	Trails.Add(Trail);
	RefreshTick();
}

void UTN_TctItemComponent::TickTrails(float DeltaTime)
{
	for (int32 Index = Trails.Num() - 1; Index >= 0; --Index)
	{
		FTrail& Trail = Trails[Index];
		Trail.Age += DeltaTime;
		UStaticMeshComponent* Mesh = Trail.Mesh.Get();
		if (!Mesh || Trail.Age >= Trail.Life)
		{
			if (Mesh)
			{
				Mesh->DestroyComponent();
			}
			Trails.RemoveAtSwap(Index);
			continue;
		}
		// Se adelgaza hasta desaparecer.
		const float Left = 1.f - Trail.Age / Trail.Life;
		const FVector Scale = Mesh->GetComponentScale();
		Mesh->SetWorldScale3D(FVector(Trail.Width * Left, Trail.Width * Left, Scale.Z));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick y fin
// ─────────────────────────────────────────────────────────────────────────────

void UTN_TctItemComponent::RefreshTick()
{
	SetComponentTickEnabled(bHeavyApplied || Trails.Num() > 0);
}

void UTN_TctItemComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TickTrails(DeltaTime);
	if (bHeavyApplied && !IsHeavy())
	{
		// Se acaba el lastre a la hora del servidor, en cada máquina (sin esperar otra réplica).
		ApplyHeavy();
	}
	RefreshTick();
}

void UTN_TctItemComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	HeavyEnd = 0.f;
	if (bHeavyApplied)
	{
		ApplyHeavy();
	}
	for (const FTrail& Trail : Trails)
	{
		if (UStaticMeshComponent* Mesh = Trail.Mesh.Get())
		{
			Mesh->DestroyComponent();
		}
	}
	Trails.Reset();
	Super::EndPlay(EndPlayReason);
}
