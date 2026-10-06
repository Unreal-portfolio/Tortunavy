#include "World/TN_TctThiefGull.h"
#include "Beach/TN_BeachEnemyKit.h"
#include "Beach/TN_BeachEnemyMeshes.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TN_Log.h"
#include "Engine/World.h"
#include "Game/TN_TctItemRules.h"
#include "Game/TN_TctItems.h"
#include "Misc/App.h"
#include "Net/UnrealNetwork.h"
#include "Player/TN_InventoryComponent.h"
#include "Player/TortugaCharacter.h"
#include "World/Beach/TN_RaceItems.h"
#include "World/TN_PickupInteractableBase.h"

namespace TNTctThiefGullDetail
{
	/** Tamaño de la gaviota de la fauna (unos 2,5 m de punta a punta de las alas). */
	constexpr float GullScale = 2.8f;
	/** Altura sobre la tortuga a la que pasa al robar o entregar (uu). */
	constexpr double GrabHeight = 70.0;
	/** Lo que tarda en irse cuando no tiene nada que hacer (s). */
	constexpr float LeaveSeconds = 2.f;
	/** Sin llegar a la víctima en este tiempo, se va (s). */
	constexpr float OutboundSeconds = 6.f;
	/** Aleteos por segundo. */
	constexpr float FlapRate = 3.2f;

	bool CanRender()
	{
		return !IsRunningDedicatedServer() && FApp::CanEverRender();
	}
}

ATN_TctThiefGull::ATN_TctThiefGull()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(30.f);
	SetNetCullDistanceSquared(FMath::Square(25000.f));

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;
}

void ATN_TctThiefGull::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_TctThiefGull, Carried);
}

bool ATN_TctThiefGull::ServerLaunch(ATortugaCharacter* Thief)
{
	UWorld* World = Thief ? Thief->GetWorld() : nullptr;
	if (!World || !Thief->HasAuthority())
	{
		return false;
	}
	TArray<ATortugaCharacter*> Others;
	TNTctItems::GatherTurtles(Thief, Thief, Others);
	TArray<ATortugaCharacter*> Targets;
	TArray<FVector> Positions;
	for (ATortugaCharacter* Other : Others)
	{
		if (TNTctItems::CanAffect(Other, false))
		{
			Targets.Add(Other);
			Positions.Add(Other->GetActorLocation());
		}
	}
	const int32 Pick = TNTctItemRules::PickThiefVictim(Thief->GetActorLocation(), Positions);
	if (Pick == INDEX_NONE)
	{
		return false;
	}
	const FVector Start = Thief->GetActorLocation() + FVector(0.0, 0.0, 120.0);
	FActorSpawnParameters Params;
	Params.Owner = Thief;
	Params.Instigator = Thief;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATN_TctThiefGull* Gull = World->SpawnActor<ATN_TctThiefGull>(ATN_TctThiefGull::StaticClass(), FTransform(Thief->GetActorRotation(), Start), Params);
	if (!Gull)
	{
		return false;
	}
	Gull->Thief = Thief;
	Gull->Victim = Targets[Pick];
	Gull->SetLifeSpan(TNTctItemTuning::ThiefMaxSeconds);
	TNTctItems::PlayCue(Thief, ETNRaceSound::Squawk, 1.25f);
	UE_LOG(LogTortunabo, Log, TEXT("[TcT] %s suelta una gaviota ladrona hacia %s."), *GetNameSafe(Thief), *GetNameSafe(Targets[Pick]));
	return true;
}

void ATN_TctThiefGull::BeginPlay()
{
	Super::BeginPlay();
	BuildVisuals();
	ShowCarried();
}

void ATN_TctThiefGull::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	if (HasAuthority())
	{
		ServerTick(DeltaSeconds);
	}
	PoseVisuals();
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor
// ─────────────────────────────────────────────────────────────────────────────

bool ATN_TctThiefGull::FlyTowards(const FVector& Target, float DeltaSeconds)
{
	const FVector Here = GetActorLocation();
	// Llega en picado: cuanto más lejos, más alta.
	const double Flat = FVector::Dist2D(Here, Target);
	const FVector Aim = Target + FVector(0.0, 0.0, FMath::Clamp((Flat - 150.0) * 0.3, 0.0, static_cast<double>(TNTctItemTuning::ThiefFlyHeight)));
	const FVector ToAim = Aim - Here;
	const double Step = TNTctItemTuning::ThiefSpeed * DeltaSeconds;
	const FVector Next = ToAim.Size() <= Step ? Aim : Here + ToAim.GetSafeNormal() * Step;
	const FVector Facing(ToAim.X, ToAim.Y, 0.0);
	SetActorLocationAndRotation(Next, Facing.IsNearlyZero() ? GetActorRotation() : Facing.Rotation());
	return FVector::Dist(Next, Target) <= TNTctItemTuning::ThiefReach;
}

void ATN_TctThiefGull::ServerTick(float DeltaSeconds)
{
	using namespace TNTctThiefGullDetail;
	switch (Phase)
	{
	case ETNTctGullPhase::Outbound:
	{
		ATortugaCharacter* Target = Victim.Get();
		if (!IsValid(Target) || Target->IsDead() || Age > OutboundSeconds)
		{
			StartLeaving();
			return;
		}
		if (FlyTowards(Target->GetActorLocation() + FVector(0.0, 0.0, GrabHeight), DeltaSeconds))
		{
			ServerSteal(Target);
		}
		return;
	}
	case ETNTctGullPhase::Return:
	{
		ATortugaCharacter* Launcher = Thief.Get();
		if (!IsValid(Launcher) || Launcher->IsDead())
		{
			// Sin nadie a quien dárselo: se lo lleva.
			StartLeaving();
			return;
		}
		if (FlyTowards(Launcher->GetActorLocation() + FVector(0.0, 0.0, GrabHeight), DeltaSeconds))
		{
			ServerDeliver(Launcher);
		}
		return;
	}
	case ETNTctGullPhase::Leave:
		LeaveAge += DeltaSeconds;
		SetActorLocation(GetActorLocation() + (LeaveDirection * 0.8 + FVector::UpVector * 0.6).GetSafeNormal() * TNTctItemTuning::ThiefSpeed * DeltaSeconds);
		if (LeaveAge >= LeaveSeconds)
		{
			Destroy();
		}
		return;
	}
}

void ATN_TctThiefGull::ServerSteal(ATortugaCharacter* Target)
{
	UTN_InventoryComponent* Inventory = Target->GetInventoryComponent();
	FTN_InventoryItem Stolen;
	if (Inventory && Inventory->HasEquippedItem() && Inventory->TryExtractEquippedItem(Stolen) && Stolen.IsValid())
	{
		Carried = Stolen;
		ShowCarried();
		ForceNetUpdate();
		Phase = ETNTctGullPhase::Return;
		TNTctItems::PlayCue(Target, ETNRaceSound::Catch, 1.2f);
		UE_LOG(LogTortunabo, Log, TEXT("[TcT] La gaviota ladrona le quita %s a %s."), *Stolen.ItemId.ToString(), *GetNameSafe(Target));
		return;
	}
	// No lleva nada en la mano: la marea.
	if (TNTctItems::CanAffect(Target, false))
	{
		Target->ApplyMareoEffect(TNTctItemTuning::ThiefDizzySeconds);
	}
	TNTctItems::PlayCue(Target, ETNRaceSound::Squawk, 1.4f);
	UE_LOG(LogTortunabo, Log, TEXT("[TcT] La gaviota ladrona no encuentra nada y marea a %s."), *GetNameSafe(Target));
	StartLeaving();
}

void ATN_TctThiefGull::ServerDeliver(ATortugaCharacter* Launcher)
{
	FTN_InventoryItem Item = Carried;
	Carried = FTN_InventoryItem();
	ShowCarried();
	ForceNetUpdate();
	UTN_InventoryComponent* Inventory = Launcher->GetInventoryComponent();
	if (Inventory && Inventory->CanReceiveItem(Item, false) && Inventory->TryAddOrReplaceEquipped(Item, false))
	{
		TNTctItems::PlayCue(Launcher, ETNRaceSound::Land, 1.1f);
	}
	else if (UWorld* World = GetWorld(); World && Item.PickupActorClass)
	{
		// Con las manos llenas, lo deja a sus pies.
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.bDeferConstruction = true;
		const FTransform Where(Launcher->GetActorLocation() + Launcher->GetActorForwardVector() * 80.0);
		if (ATN_PickupInteractableBase* Pickup = World->SpawnActor<ATN_PickupInteractableBase>(Item.PickupActorClass, Where, Params))
		{
			Pickup->InitializeFromInventoryItem(Item);
			Pickup->FinishSpawning(Where);
		}
		TNTctItems::PlayCue(Launcher, ETNRaceSound::Land, 0.9f);
	}
	UE_LOG(LogTortunabo, Log, TEXT("[TcT] La gaviota ladrona le entrega %s a %s."), *Item.ItemId.ToString(), *GetNameSafe(Launcher));
	StartLeaving();
}

void ATN_TctThiefGull::StartLeaving()
{
	if (Phase == ETNTctGullPhase::Leave)
	{
		return;
	}
	Phase = ETNTctGullPhase::Leave;
	LeaveAge = 0.f;
	const FVector Forward = GetActorForwardVector();
	LeaveDirection = FVector(Forward.X, Forward.Y, 0.0).GetSafeNormal();
	if (LeaveDirection.IsNearlyZero())
	{
		LeaveDirection = FVector::ForwardVector;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Visual
// ─────────────────────────────────────────────────────────────────────────────

void ATN_TctThiefGull::BuildVisuals()
{
	using TNProcMesh::FTNProcMeshBuffers;
	if (!TNTctThiefGullDetail::CanRender())
	{
		return;
	}
	// La gaviota de la fauna (la de las zonas de gaviotas), en pequeño.
	TArray<TNFauna::FTNFaunaPart> Parts;
	TNFauna::FTNFaunaRig Rig;
	TNFauna::FTNFaunaBirdJaw Jaw;
	TNBeachMeshes::BuildBirdParts(false, Parts, Rig, Jaw);

	GullRoot = NewObject<USceneComponent>(this, NAME_None, RF_Transient);
	GullRoot->SetupAttachment(RootComponent);
	GullRoot->RegisterComponent();
	GullRoot->SetRelativeScale3D(FVector(TNTctThiefGullDetail::GullScale));

	UStaticMeshComponent* BodyComp = nullptr;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (int32 PartIndex = 0; PartIndex < Parts.Num(); ++PartIndex)
		{
			const TNFauna::FTNFaunaPart& Part = Parts[PartIndex];
			const bool bIsBody = Part.Bone == TNFauna::ETNFaunaBone::Body;
			if ((Pass == 0) != bIsBody)
			{
				continue;
			}
			const FTNProcMeshBuffers& Buffers = Part.Mesh;
			UStaticMesh* PartMesh = TNBeachKit::CachedMesh(TNBeachMeshes::BirdPartKey(false, PartIndex), [&Buffers](FTNProcMeshBuffers& M) { M = Buffers; });
			USceneComponent* Parent = bIsBody ? GullRoot.Get() : static_cast<USceneComponent*>(BodyComp);
			UStaticMeshComponent* Comp = TNBeachKit::AddPart(this, Parent ? Parent : GullRoot.Get(), PartMesh, Part.Pivot, false);
			if (bIsBody && !BodyComp)
			{
				BodyComp = Comp;
			}
			const int32 Index = GullParts.Add(Comp);
			PartPivots.Add(Part.Pivot);
			if (Part.Bone == TNFauna::ETNFaunaBone::WingL) { WingLeftPart = Index; }
			if (Part.Bone == TNFauna::ETNFaunaBone::WingR) { WingRightPart = Index; }
		}
	}

	CarriedMesh = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
	CarriedMesh->SetupAttachment(RootComponent);
	CarriedMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CarriedMesh->SetCastShadow(false);
	CarriedMesh->SetRelativeLocation(FVector(10.0, 0.0, -45.0));
	CarriedMesh->RegisterComponent();
	CarriedMesh->SetVisibility(false);
}

void ATN_TctThiefGull::PoseVisuals()
{
	if (!GullRoot)
	{
		return;
	}
	const float Flap = 40.f * FMath::Sin(Age * 2.f * PI * TNTctThiefGullDetail::FlapRate);
	if (GullParts.IsValidIndex(WingLeftPart))
	{
		TNBeachKit::Pose(GullParts[WingLeftPart], PartPivots[WingLeftPart], FRotator(0.f, -8.f, Flap));
	}
	if (GullParts.IsValidIndex(WingRightPart))
	{
		TNBeachKit::Pose(GullParts[WingRightPart], PartPivots[WingRightPart], FRotator(0.f, 8.f, -Flap));
	}
	if (CarriedMesh && CarriedMesh->IsVisible())
	{
		CarriedMesh->SetRelativeRotation(FRotator(0.f, Age * 120.f, 0.f));
	}
}

void ATN_TctThiefGull::OnRep_Carried()
{
	ShowCarried();
}

void ATN_TctThiefGull::ShowCarried()
{
	if (!CarriedMesh)
	{
		return;
	}
	if (!Carried.IsValid())
	{
		CarriedMesh->SetVisibility(false);
		return;
	}
	// Las mallas de los objetos de código y de la carrera se resuelven en cada máquina.
	FTN_InventoryItem Look = Carried;
	TNRaceItems::ResolveVisuals(Look);
	CarriedMesh->SetStaticMesh(Look.EquippedMesh);
	CarriedMesh->SetRelativeScale3D(Look.EquippedMeshScale.IsNearlyZero() ? FVector::OneVector : Look.EquippedMeshScale);
	CarriedMesh->SetVisibility(Look.EquippedMesh != nullptr);
}
