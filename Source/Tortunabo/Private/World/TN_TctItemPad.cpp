#include "World/TN_TctItemPad.h"
#include "Core/TN_Log.h"
#include "Game/TN_TctGameState.h"
#include "Game/TN_TctItemComponent.h"
#include "Player/TortugaCharacter.h"
#include "Game/TN_TctItems.h"
#include "Core/TN_ProjectMaterials.h"
#include "ProcMap/TN_ProcMapRuntimeMesh.h"
#include "ProcMap/TN_TctPropMeshes.h"
#include "World/ProcMap/TN_ProcMapMath.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace TNTctItemPadDetail
{
	/** Cada cuánto mira el servidor el reloj del punto (s). */
	constexpr float UpdateInterval = 0.25f;
	/** Si no ha podido sacar un objeto, lo reintenta pasado esto (s). */
	constexpr float RetrySeconds = 2.f;
	/** Agua «sin agua» (sin GameState de TcT). */
	constexpr float NoWaterZ = -1.e6f;
	const TCHAR* FoliageMaterialPath = TEXT("/Game/ProcMap/Materials/M_ProcFoliage.M_ProcFoliage");
	/** Ancho del haz (fracción del cilindro del motor, de 100 uu) y su opacidad: una línea fina, no un pilar. */
	constexpr float BeamWidth = 0.06f;
	constexpr float BeamOpacity = 0.16f;

	bool CanRender()
	{
		return !IsRunningDedicatedServer() && FApp::CanEverRender();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Pickup
// ─────────────────────────────────────────────────────────────────────────────

ATN_TctItemPickup::ATN_TctItemPickup()
{
	RefreshPrompt();
}

void ATN_TctItemPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ATN_TctItemPickup, Kind, COND_InitialOnly);
}

void ATN_TctItemPickup::SetUp(ATN_TctItemPad* InPad, ETNTctItem InKind, const FTN_InventoryItem& Item)
{
	Pad = InPad;
	Kind = static_cast<uint8>(InKind);
	InitializeFromInventoryItem(Item);
	RefreshPrompt();
}

void ATN_TctItemPickup::OnRep_Kind()
{
	RefreshPrompt();
}

void ATN_TctItemPickup::RefreshPrompt()
{
	const ETNTctItem Item = GetKind();
	PromptText = Item == ETNTctItem::None
		? NSLOCTEXT("Tortunabo", "PickupPrompt", "Recoger")
		: FText::Format(NSLOCTEXT("TNTct", "PickupItemPrompt", "Coger {0}"), TNTctItems::DisplayName(Item));
}

bool ATN_TctItemPickup::CanInteract(APawn* Interactor) const
{
	if (GetKind() != ETNTctItem::Flotador)
	{
		return Super::CanInteract(Interactor);
	}
	// El flotador no ocupa la mano: basta con no llevar ya uno.
	const UTN_TctItemComponent* Effects = UTN_TctItemComponent::FindOn(Interactor);
	return ATN_InteractableBase::CanInteract(Interactor) && !bTaken && Cast<ATortugaCharacter>(Interactor)
		&& !(Effects && Effects->HasFloat());
}

void ATN_TctItemPickup::TakeFloat(APawn* Interactor)
{
	UTN_TctItemComponent* Effects = UTN_TctItemComponent::FindOrAddOn(Cast<ATortugaCharacter>(Interactor));
	if (!Effects || !Effects->ServerGrantFloat())
	{
		return;
	}
	SetNetDormancy(DORM_Awake);
	bTaken = true;
	SetInteractionEnabled(false);
	SetActorHiddenInGame(true);
	ForceNetUpdate();
	TNTctItems::PlayCue(Cast<ATortugaCharacter>(Interactor), ETNRaceSound::BoxOpen, 1.f);
	// Como el resto de pickups: la destrucción replicada en el siguiente fotograma.
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]() { Destroy(); }));
	UE_LOG(LogTortunabo, Log, TEXT("[TcT] %s se cuelga el flotador del caparazón."), *GetNameSafe(Interactor));
}

void ATN_TctItemPickup::Interact(APawn* Interactor)
{
	if (GetKind() == ETNTctItem::Flotador)
	{
		if (HasAuthority() && CanInteract(Interactor))
		{
			TakeFloat(Interactor);
		}
	}
	else
	{
		Super::Interact(Interactor);
	}
	if (bTaken)
	{
		if (ATN_TctItemPad* OwningPad = Pad.Get())
		{
			OwningPad->NotifyTaken(this);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Punto de objetos
// ─────────────────────────────────────────────────────────────────────────────

ATN_TctItemPad::ATN_TctItemPad()
{
	// El tick solo mueve el destello mientras hay un objeto puesto (RefreshLook lo enciende).
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickInterval = 0.05f;
	bReplicates = true;
	SetReplicateMovement(false);
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(1.f);

	Disc = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Disc"));
	RootComponent = Disc;
	Disc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Disc->SetCanEverAffectNavigation(false);
	Disc->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		Disc->SetStaticMesh(Cylinder.Object);
	}
	// Raíz plana y sin dibujar (#920): el punto se ve como lo que lo rodea (cofre, nido, piedras...), no como un disco.
	Disc->SetRelativeScale3D(FVector(1.6f, 1.6f, 0.03f));
	Disc->SetVisibility(false);

	// Haz de luz: un cilindro fino y translúcido que sube desde el disco mientras hay un objeto (se ve de lejos).
	Beam = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Beam"));
	Beam->SetupAttachment(Disc);
	Beam->SetUsingAbsoluteLocation(true);
	Beam->SetUsingAbsoluteRotation(true);
	Beam->SetUsingAbsoluteScale(true);
	Beam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Beam->SetCanEverAffectNavigation(false);
	Beam->SetCastShadow(false);
	Beam->SetVisibility(false);
	if (Cylinder.Succeeded())
	{
		Beam->SetStaticMesh(Cylinder.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BeamMaterial(TEXT("/Game/ProcMap/Materials/MI_ProcSea.MI_ProcSea"));
	if (BeamMaterial.Succeeded())
	{
		Beam->SetMaterial(0, BeamMaterial.Object);
	}
}

void ATN_TctItemPad::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_TctItemPad, Rarity);
	DOREPLIFETIME(ATN_TctItemPad, bBeamOn);
}

float ATN_TctItemPad::BeamHeight(ETNTctRarity InRarity)
{
	// Común: solo el destello; raro: un hilo de luz corto; épico: más alto, pero fino (#920).
	return InRarity == ETNTctRarity::Epic ? 900.f : (InRarity == ETNTctRarity::Rare ? 380.f : 0.f);
}

FLinearColor ATN_TctItemPad::RarityColor(ETNTctRarity InRarity)
{
	return InRarity == ETNTctRarity::Epic ? FLinearColor(0.85f, 0.3f, 1.f)
		: (InRarity == ETNTctRarity::Rare ? FLinearColor(0.2f, 0.75f, 1.f) : FLinearColor(1.f, 0.62f, 0.18f));
}

void ATN_TctItemPad::OnRep_Look()
{
	RefreshLook();
}

void ATN_TctItemPad::BuildLook()
{
	using namespace TNTctMesh;
	if (!TNTctItemPadDetail::CanRender() || BuiltRarity == Rarity)
	{
		return;
	}
	BuiltRarity = Rarity;
	if (IsValid(PropLook)) { PropLook->DestroyComponent(); }
	if (IsValid(Glint)) { Glint->DestroyComponent(); }
	PropLook = nullptr;
	Glint = nullptr;
	const FVector Where = GetActorLocation();
	// El sitio da el estilo y el giro: el mismo en el servidor y en cada cliente, sin replicar nada más.
	const uint32 Site = TNProcMap::HashCell(0x920A0Du, FMath::RoundToInt32(Where.X), FMath::RoundToInt32(Where.Y));
	const EPadStyle Style = PadStyleFor(Rarity, Site >> 5);
	const FRotator Facing(0.0, static_cast<double>(Site % 360u), 0.0);
	GlintPhase = static_cast<float>(Site % 628u) * 0.01f;

	TNProcMesh::FTNProcMeshBuffers Buffers;
	BuildPad(Buffers, Style, Site);
	// La paleta de los props es la de las mallas procedurales: se decodifica una vez más, como en el generador.
	for (FLinearColor& Col : Buffers.Colors)
	{
		Col = FLinearColor(TNProcRuntimeMesh::SRGBToLinear(Col.R), TNProcRuntimeMesh::SRGBToLinear(Col.G), TNProcRuntimeMesh::SRGBToLinear(Col.B), Col.A);
	}
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TNTctItemPadDetail::FoliageMaterialPath);
	if (!Material) { Material = TNMaterials::VertexColor(); }
	if (UStaticMesh* Mesh = TNProcRuntimeMesh::MakeStaticMesh(this, Buffers, Material, false))
	{
		PropLook = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		PropLook->SetStaticMesh(Mesh);
		PropLook->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PropLook->SetCanEverAffectNavigation(false);
		PropLook->SetGenerateOverlapEvents(false);
		PropLook->SetupAttachment(RootComponent);
		PropLook->SetUsingAbsoluteLocation(true);
		PropLook->SetUsingAbsoluteRotation(true);
		PropLook->SetUsingAbsoluteScale(true);
		PropLook->RegisterComponent();
		PropLook->SetWorldLocationAndRotation(Where, Facing);
		PropLook->SetWorldScale3D(FVector::OneVector);
	}
	if (UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		Glint = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		Glint->SetStaticMesh(Sphere);
		Glint->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Glint->SetCastShadow(false);
		Glint->SetCanEverAffectNavigation(false);
		Glint->SetupAttachment(RootComponent);
		Glint->SetUsingAbsoluteLocation(true);
		Glint->SetUsingAbsoluteRotation(true);
		Glint->SetUsingAbsoluteScale(true);
		Glint->SetVisibility(false);
		Glint->RegisterComponent();
		Glint->SetWorldLocation(Where + Facing.RotateVector(PadGlintOffset(Style)));
		Glint->SetWorldScale3D(FVector(0.14f));
		if (UMaterialInterface* Sea = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/MI_ProcSea.MI_ProcSea")))
		{
			if (UMaterialInstanceDynamic* Glow = UMaterialInstanceDynamic::Create(Sea, Glint))
			{
				Glow->SetVectorParameterValue(TEXT("Color"), RarityColor(GetRarity()));
				Glow->SetScalarParameterValue(TEXT("Opacity"), 0.3f);
				Glint->SetMaterial(0, Glow);
			}
		}
	}
}

void ATN_TctItemPad::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsValid(Glint) || !bBeamOn)
	{
		return;
	}
	// Un latido lento: sube y baja el brillo y el tamaño del destello.
	const float Pulse = 0.5f + 0.5f * FMath::Sin(GlintPhase + (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f) * 2.4f);
	Glint->SetWorldScale3D(FVector(0.10f + 0.08f * Pulse));
	if (UMaterialInstanceDynamic* Glow = Cast<UMaterialInstanceDynamic>(Glint->GetMaterial(0)))
	{
		Glow->SetScalarParameterValue(TEXT("Opacity"), 0.16f + 0.5f * Pulse);
	}
}

void ATN_TctItemPad::RefreshLook()
{
	const ETNTctRarity Level = GetRarity();
	BuildLook();
	if (IsValid(Glint))
	{
		Glint->SetVisibility(bBeamOn);
		if (UMaterialInstanceDynamic* Glow = Cast<UMaterialInstanceDynamic>(Glint->GetMaterial(0)))
		{
			Glow->SetVectorParameterValue(TEXT("Color"), RarityColor(Level));
		}
	}
	SetActorTickEnabled(IsValid(Glint) && bBeamOn);
	if (Beam)
	{
		const float Height = BeamHeight(Level);
		Beam->SetVisibility(bBeamOn && Height > 0.f);
		if (Height > 0.f)
		{
			// El cilindro del motor mide 100 uu de alto y 100 de ancho, en coordenadas de mundo (el disco es plano y escala lo que cuelga de él).
			Beam->SetWorldScale3D(FVector(TNTctItemPadDetail::BeamWidth, TNTctItemPadDetail::BeamWidth, Height / 100.f));
			Beam->SetWorldLocation(GetActorLocation() + FVector(0.0, 0.0, Height * 0.5f));
		}
		if (!Cast<UMaterialInstanceDynamic>(Beam->GetMaterial(0)) && Beam->GetMaterial(0))
		{
			if (UMaterialInstanceDynamic* Glow = UMaterialInstanceDynamic::Create(Beam->GetMaterial(0), this))
			{
				Beam->SetMaterial(0, Glow);
			}
		}
		if (UMaterialInstanceDynamic* Glow = Cast<UMaterialInstanceDynamic>(Beam->GetMaterial(0)))
		{
			Glow->SetVectorParameterValue(TEXT("Color"), RarityColor(Level));
			Glow->SetScalarParameterValue(TEXT("Opacity"), TNTctItemPadDetail::BeamOpacity);
		}
	}
}

void ATN_TctItemPad::ServerSetRarity(ETNTctRarity NewRarity)
{
	if (!HasAuthority())
	{
		return;
	}
	Rarity = static_cast<uint8>(NewRarity);
	ForceNetUpdate();
	RefreshLook();
}

float ATN_TctItemPad::RoundProgress() const
{
	const UWorld* World = GetWorld();
	const ATN_TctGameState* State = World ? World->GetGameState<ATN_TctGameState>() : nullptr;
	const float Elapsed = State ? State->GetFloodElapsed() : -1.f;
	return Elapsed < 0.f ? 0.f : TNTctRules::RoundProgress(State->Flood.ToPlan(), Elapsed);
}

void ATN_TctItemPad::BeginPlay()
{
	Super::BeginPlay();
	if (UMaterialInstanceDynamic* Material = Disc ? Disc->CreateDynamicMaterialInstance(0) : nullptr)
	{
		Material->SetVectorParameterValue(TEXT("Color"), DiscColor);
	}
	RefreshLook();
}

void ATN_TctItemPad::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(UpdateHandle);
	if (HasAuthority())
	{
		RemoveCurrent();
	}
	Super::EndPlay(EndPlayReason);
}

void ATN_TctItemPad::ServerStartRound(double Now, int32 PadIndex)
{
	if (!HasAuthority())
	{
		return;
	}
	RemoveCurrent();
	Available = TNTctItems::AvailableKinds();
	Clock.StartRound(Now, TNTctItemRules::PadFirstSpawnDelay(PadIndex));
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(UpdateHandle, this, &ATN_TctItemPad::UpdateFromTimer, TNTctItemPadDetail::UpdateInterval, true);
	}
}

void ATN_TctItemPad::ServerStopRound()
{
	if (!HasAuthority())
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(UpdateHandle);
	Clock.Stop();
	RemoveCurrent();
}

void ATN_TctItemPad::UpdateFromTimer()
{
	const UWorld* World = GetWorld();
	const ATN_TctGameState* State = World ? World->GetGameState<ATN_TctGameState>() : nullptr;
	ServerUpdate(World ? World->GetTimeSeconds() : 0.0, State ? State->GetWaterZ() : TNTctItemPadDetail::NoWaterZ);
}

void ATN_TctItemPad::ServerUpdate(double Now, float WaterZ)
{
	if (!HasAuthority())
	{
		return;
	}
	// El objeto ha desaparecido sin avisar (destruido por otra cosa): cuenta como cogido.
	ATN_TctItemPickup* Pickup = Current.Get();
	if (Clock.bHasItem && (!IsValid(Pickup) || Pickup->IsTaken()))
	{
		Current.Reset();
		Clock.MarkTaken(Now, RespawnSeconds);
		bBeamOn = false;
		ForceNetUpdate();
		RefreshLook();
	}
	if (TNTctItemRules::IsPadSubmerged(static_cast<float>(GetActorLocation().Z), WaterZ))
	{
		if (Clock.IsRunning())
		{
			// El mar se lo lleva: este punto ya no saca nada esta ronda.
			RemoveCurrent();
			Clock.Stop();
			GetWorldTimerManager().ClearTimer(UpdateHandle);
		}
		return;
	}
	if (Clock.ShouldSpawn(Now, false) && !SpawnItem())
	{
		Clock.StartRound(Now, TNTctItemPadDetail::RetrySeconds);
	}
}

void ATN_TctItemPad::NotifyTaken(ATN_TctItemPickup* Pickup)
{
	if (!HasAuthority() || !Pickup || Pickup != Current.Get())
	{
		return;
	}
	Current.Reset();
	Clock.MarkTaken(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0, RespawnSeconds);
	bBeamOn = false;
	ForceNetUpdate();
	RefreshLook();
}

bool ATN_TctItemPad::SpawnItem()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	if (Available.Num() == 0)
	{
		Available = TNTctItems::AvailableKinds();
	}
	const ETNTctItem Kind = TNTctItemRules::PickPadItem(Available, LastKind, FMath::FRand(), GetRarity(), RoundProgress());
	FTN_InventoryItem Item;
	if (Kind == ETNTctItem::None || !TNTctItems::MakeItem(Kind, Item))
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[TcT] El punto de objetos %s no ha podido sacar nada."), *GetName());
		return false;
	}
	const FTransform Where(FRotator(0.0, FMath::FRandRange(0.0, 360.0), 0.0), GetActorLocation() + FVector(0.0, 0.0, ItemLift));
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	ATN_TctItemPickup* Pickup = World->SpawnActor<ATN_TctItemPickup>(ATN_TctItemPickup::StaticClass(), Where, Params);
	if (!Pickup)
	{
		return false;
	}
	Pickup->SetUp(this, Kind, Item);
	Pickup->FinishSpawning(Where);
	Current = Pickup;
	LastKind = Kind;
	Clock.MarkSpawned();
	bBeamOn = true;
	ForceNetUpdate();
	RefreshLook();
	UE_LOG(LogTortunabo, Verbose, TEXT("[TcT] Punto %s: sale %s."), *GetName(), TNTctItemRules::Spec(Kind).Code);
	return true;
}

void ATN_TctItemPad::RemoveCurrent()
{
	if (ATN_TctItemPickup* Pickup = Current.Get())
	{
		if (!Pickup->IsTaken() && !Pickup->IsActorBeingDestroyed())
		{
			Pickup->Destroy();
		}
	}
	Current.Reset();
	if (bBeamOn)
	{
		bBeamOn = false;
		ForceNetUpdate();
		RefreshLook();
	}
}
