#include "World/TN_UmbrellaInteractable.h"
#include "Core/TN_Log.h"
#include "Player/TortugaCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

ATN_UmbrellaInteractable::ATN_UmbrellaInteractable()
{
	bReplicates = true;
	PromptText = NSLOCTEXT("Tortunabo", "UmbrellaPrompt", "Abrir / Cerrar sombrilla");

	// ── BasicShapes usados para el mesh compound por defecto ─────────────────
	// Cylinder: 50 cm radio, 100 cm alto a escala (1,1,1). Centro en el origen.
	// Cone:     50 cm radio de base, 100 cm alto a escala (1,1,1). Ápice arriba.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderAsset(
		TEXT("/Engine/BasicShapes/Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeAsset(
		TEXT("/Engine/BasicShapes/Cone"));

	// ── Palo (Mesh heredado de TN_InteractableBase) ───────────────────────────
	// Cilindro fino, 5 cm de radio, 250 cm de alto.
	// MeshFloorOffset=125 → BeginPlay colocará el centro a Z=125 sobre SceneRoot,
	// por lo que el palo va de Z=0 (suelo) a Z=250 cm.
	MeshFloorOffset = 125.f;
	if (Mesh)
	{
		Mesh->SetRelativeScale3D(FVector(0.1f, 0.1f, 2.5f));
		if (CylinderAsset.Succeeded()) { Mesh->SetStaticMesh(CylinderAsset.Object); }
	}

	// ── Mango (HandleMeshComp) ────────────────────────────────────────────────
	// Cilindro más grueso desde Z=125 hasta Z=300 cm (centro Z=215, semialtura 87.5).
	// Escala (0.45, 0.45, 1.75) → radio 22.5 cm, alto 175 cm.
	HandleMeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HandleMesh"));
	HandleMeshComp->SetupAttachment(SceneRoot);
	HandleMeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HandleMeshComp->SetIsReplicated(false);
	HandleMeshComp->SetRelativeLocation(FVector(0.f, 0.f, 215.f));
	HandleMeshComp->SetRelativeScale3D(FVector(0.45f, 0.45f, 1.75f));
	if (CylinderAsset.Succeeded()) { HandleMeshComp->SetStaticMesh(CylinderAsset.Object); }

	// ── Cúpula (CanopyMeshComp) ───────────────────────────────────────────────
	// Cono invertido (Pitch=180°) con ápice en Z=250 (toca la punta del palo)
	// y base (aro, r=150 cm) en Z=280. Solo visible cuando la sombrilla está abierta.
	// Escala (3,3,0.3): radio base = 150 cm ≥ 1.5 m, alto cono = 30 cm.
	// RelativeLocation Z=265 → ápice a 265-15=250, base a 265+15=280.
	CanopyMeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CanopyMesh"));
	CanopyMeshComp->SetupAttachment(SceneRoot);
	CanopyMeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CanopyMeshComp->SetIsReplicated(false);
	CanopyMeshComp->SetRelativeLocation(FVector(0.f, 0.f, 265.f));
	CanopyMeshComp->SetRelativeScale3D(FVector(3.0f, 3.0f, 0.3f));
	CanopyMeshComp->SetRelativeRotation(FRotator(180.f, 0.f, 0.f)); // Pitch=180 → invierte cono
	CanopyMeshComp->SetVisibility(false); // oculta hasta que se abra
	if (ConeAsset.Succeeded()) { CanopyMeshComp->SetStaticMesh(ConeAsset.Object); }
}

void ATN_UmbrellaInteractable::BeginPlay()
{
	Super::BeginPlay();

	// Estado de partida: cerrada, o el replicado si ya llegó abierta (cliente que entra tarde, #893).
	ApplyUmbrellaState(bIsOpen);
}

void ATN_UmbrellaInteractable::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_UmbrellaInteractable, bIsOpen);
}

void ATN_UmbrellaInteractable::OnRep_IsOpen()
{
	ApplyUmbrellaState(bIsOpen);
}

void ATN_UmbrellaInteractable::SetOpenState(bool bOpen)
{
	// El actor es DORM_DormantAll: se despierta antes de cambiar la propiedad para que se replique.
	FlushNetDormancy();
	bIsOpen = bOpen;
	ApplyUmbrellaState(bOpen);
}

void ATN_UmbrellaInteractable::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(UmbrellaActiveTimerHandle);

	if (ATortugaCharacter* Char = ProtectedCharacter.Get())
	{
		Char->SetUmbrellaProtection(false);
	}

	Super::EndPlay(EndPlayReason);
}

// ── CanInteract: bypass de cooldown — la sombrilla siempre puede togglearse ──

bool ATN_UmbrellaInteractable::CanInteract(APawn* Interactor) const
{
	// Saltamos el cooldown de TN_DirectInteractableBase y vamos directo al
	// abuelo, que solo comprueba bInteractionEnabled y Interactor != null.
	return ATN_InteractableBase::CanInteract(Interactor);
}

// ── Interact: toggle abierto / cerrado ───────────────────────────────────────

void ATN_UmbrellaInteractable::Interact(APawn* Interactor)
{
	if (!HasAuthority() || !Interactor) { return; }

	ATortugaCharacter* Char = Cast<ATortugaCharacter>(Interactor);
	if (!Char) { return; }

	if (bIsOpen)
	{
		// ── Cerrar manualmente ────────────────────────────────────────────────
		GetWorldTimerManager().ClearTimer(UmbrellaActiveTimerHandle);

		if (ATortugaCharacter* ProtChar = ProtectedCharacter.Get())
		{
			ProtChar->SetUmbrellaProtection(false);
		}
		ProtectedCharacter.Reset();
		SetOpenState(false);

		UE_LOG(LogTortunabo, Log, TEXT("[Umbrella] %s cerró sombrilla manualmente"), *GetNameSafe(Char));
		MulticastOnUmbrellaClosed();
	}
	else
	{
		// ── Abrir ─────────────────────────────────────────────────────────────
		Char->SetUmbrellaProtection(true);
		ProtectedCharacter = Char;
		SetOpenState(true);

		UE_LOG(LogTortunabo, Log, TEXT("[Umbrella] %s abrió sombrilla (%.1fs)"),
			*GetNameSafe(Char), UmbrellaDurationSeconds);

		MulticastOnUmbrellaOpened(Interactor);

		FTimerDelegate Del = FTimerDelegate::CreateUObject(
			this, &ATN_UmbrellaInteractable::HandleUmbrellaExpired);
		GetWorldTimerManager().SetTimer(
			UmbrellaActiveTimerHandle, Del, UmbrellaDurationSeconds, false);
	}
}

void ATN_UmbrellaInteractable::HandleUmbrellaExpired()
{
	if (ATortugaCharacter* Char = ProtectedCharacter.Get())
	{
		Char->SetUmbrellaProtection(false);
	}
	ProtectedCharacter.Reset();
	SetOpenState(false);
	MulticastOnUmbrellaClosed();
}

// ── Visual state ──────────────────────────────────────────────────────────────

void ATN_UmbrellaInteractable::ApplyUmbrellaState(bool bOpen)
{
	if (MeshClosed && MeshOpen)
	{
		// ── Modo dual-asset: swap entre los dos meshes del BP ─────────────────
		// Ningún componente compound visible.
		if (Mesh)
		{
			Mesh->SetStaticMesh(bOpen ? MeshOpen : MeshClosed);
			Mesh->SetRelativeScale3D(FVector::OneVector);
			Mesh->SetVisibility(true);
		}
		if (HandleMeshComp) { HandleMeshComp->SetVisibility(false); }
		if (CanopyMeshComp) { CanopyMeshComp->SetVisibility(false); }
	}
	else if (MeshClosed)
	{
		// ── Modo solo-cerrado: el mesh plegado solo aparece cuando está cerrada ─
		// Al abrir: mesh cerrado desaparece, el mango y la cúpula se muestran.
		// Al cerrar: mesh cerrado reaparece, mango y cúpula se ocultan.
		if (Mesh)
		{
			Mesh->SetStaticMesh(MeshClosed);
			Mesh->SetRelativeScale3D(FVector::OneVector);
			Mesh->SetVisibility(!bOpen);
		}
		if (HandleMeshComp) { HandleMeshComp->SetVisibility(bOpen); }
		if (CanopyMeshComp) { CanopyMeshComp->SetVisibility(bOpen); }
	}
	else
	{
		// ── Modo compound: palo + mango siempre; cúpula solo cuando abierta ────
		if (Mesh)           { Mesh->SetVisibility(true); }
		if (HandleMeshComp) { HandleMeshComp->SetVisibility(true); }
		if (CanopyMeshComp) { CanopyMeshComp->SetVisibility(bOpen); }
	}
}

// ── Multicast ─────────────────────────────────────────────────────────────────

void ATN_UmbrellaInteractable::MulticastOnUmbrellaOpened_Implementation(APawn* User)
{
	if (SoundOpen)
	{
		UGameplayStatics::PlaySoundAtLocation(this, SoundOpen, GetActorLocation());
	}
	if (VFXOpen)
	{
		UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), VFXOpen, GetActorLocation());
	}
	OnUmbrellaOpened(User);
}

void ATN_UmbrellaInteractable::MulticastOnUmbrellaClosed_Implementation()
{
	if (SoundClose)
	{
		UGameplayStatics::PlaySoundAtLocation(this, SoundClose, GetActorLocation());
	}
	if (VFXClose)
	{
		UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), VFXClose, GetActorLocation());
	}
	OnUmbrellaClosed();
}
