#include "World/TN_SlowZoneVolume.h"
#include "Components/BoxComponent.h"
#include "Components/DecalComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/TortugaCharacter.h"
#include "Player/TN_StaminaComponent.h"

ATN_SlowZoneVolume::ATN_SlowZoneVolume()
{
	PrimaryActorTick.bCanEverTick = true;

	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	SetRootComponent(TriggerBox);

	TriggerBox->SetBoxExtent(FVector(200.f, 200.f, 100.f));
	TriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	TriggerBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

void ATN_SlowZoneVolume::BeginPlay()
{
	Super::BeginPlay();

	// AddUniqueDynamic: idempotente. Evita el ensure "InvocationList[CurFunctionIndex] != InDelegate"
	// cuando el BP child ya bindea el evento O BeginPlay se re-ejecuta (edge case).
	TriggerBox->OnComponentBeginOverlap.AddUniqueDynamic(this, &ATN_SlowZoneVolume::OnBoxBeginOverlap);
	TriggerBox->OnComponentEndOverlap.AddUniqueDynamic(this, &ATN_SlowZoneVolume::OnBoxEndOverlap);
}

void ATN_SlowZoneVolume::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	for (const TWeakObjectPtr<ATortugaCharacter>& WeakChar : CharactersInZone)
	{
		ATortugaCharacter* Char = WeakChar.Get();
		if (!Char) { continue; }

		// Solo modificar CMC en servidor o pawn local (evita corromper clientes remotos).
		if (!HasAuthority() && !Char->IsLocallyControlled()) { continue; }

		UCharacterMovementComponent* CMC = Char->GetCharacterMovement();
		if (!CMC) { continue; }

		// ── Clamp horizontal (en vuelo) ──────────────────────────────────────────
		if (CMC->IsFalling())
		{
			FVector HorizVel = FVector(CMC->Velocity.X, CMC->Velocity.Y, 0.f);
			if (HorizVel.Size() > MaxSlowSpeed)
			{
				HorizVel = HorizVel.GetSafeNormal() * MaxSlowSpeed;
				CMC->Velocity.X = HorizVel.X;
				CMC->Velocity.Y = HorizVel.Y;
			}
		}

		// ── Clamp vertical (efecto sirope): apenas sube, baja lento ─────────────
		if (CMC->Velocity.Z > MaxUpwardVelocity)
		{
			CMC->Velocity.Z = MaxUpwardVelocity;
		}
		else if (CMC->Velocity.Z < -MaxFallVelocity)
		{
			CMC->Velocity.Z = -MaxFallVelocity;
		}
	}
}

void ATN_SlowZoneVolume::OnBoxBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	ATortugaCharacter* Char = Cast<ATortugaCharacter>(OtherActor);
	if (!Char || CharactersInZone.Contains(Char)) { return; }

	UTN_StaminaComponent* StaminaComp = Char->FindComponentByClass<UTN_StaminaComponent>();
	if (!StaminaComp) { return; }

	CharactersInZone.Add(Char);
	// Con el nombre de esta zona: cada zona solapada pone y quita el suyo, y manda el menor (UTN_StaminaComponent).
	StaminaComp->SetSpeedCap(LimitSource(), MaxSlowSpeed);
	Char->OnDestroyed.AddUniqueDynamic(this, &ATN_SlowZoneVolume::OnCharacterDestroyed);

	// Sirope: gravedad y salto reducidos. El componente guarda los de base con el primer límite y los devuelve al quitar
	// el último, así que da igual en qué orden se entre y se salga de zonas solapadas (o del agua).
	if (HasAuthority() || Char->IsLocallyControlled())
	{
		StaminaComp->SetGravityScaleOverride(LimitSource(), GravityScaleInZone);
		StaminaComp->SetJumpLimit(LimitSource(), JumpVelocityInZone);
	}
}

void ATN_SlowZoneVolume::OnBoxEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	ATortugaCharacter* Char = Cast<ATortugaCharacter>(OtherActor);
	if (!Char || !CharactersInZone.Contains(Char)) { return; }

	CharactersInZone.Remove(Char);
	Char->OnDestroyed.RemoveDynamic(this, &ATN_SlowZoneVolume::OnCharacterDestroyed);
	RemoveLimits(Char);
}

void ATN_SlowZoneVolume::RemoveLimits(ATortugaCharacter* Char) const
{
	UTN_StaminaComponent* Stamina = Char ? Char->FindComponentByClass<UTN_StaminaComponent>() : nullptr;
	if (!Stamina) { return; }

	// Solo los de esta zona: si sigue en otra solapada, los de esa siguen mandando.
	Stamina->ClearSpeedCap(LimitSource());
	if (HasAuthority() || Char->IsLocallyControlled())
	{
		Stamina->ClearGravityScaleOverride(LimitSource());
		Stamina->ClearJumpLimit(LimitSource());
	}
}

void ATN_SlowZoneVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// La zona desaparece con tortugas dentro (fin de ronda, streaming): sin EndOverlap, sus límites se quedarían puestos.
	for (const TWeakObjectPtr<ATortugaCharacter>& WeakChar : CharactersInZone)
	{
		if (ATortugaCharacter* Char = WeakChar.Get())
		{
			Char->OnDestroyed.RemoveDynamic(this, &ATN_SlowZoneVolume::OnCharacterDestroyed);
			RemoveLimits(Char);
		}
	}
	CharactersInZone.Reset();
	Super::EndPlay(EndPlayReason);
}

void ATN_SlowZoneVolume::OnCharacterDestroyed(AActor* DestroyedActor)
{
	ATortugaCharacter* Char = Cast<ATortugaCharacter>(DestroyedActor);
	if (!Char) { return; }

	CharactersInZone.Remove(Char);
}

void ATN_SlowZoneVolume::SetZoneExtent(const FVector& Extent)
{
	if (!TriggerBox)
	{
		return;
	}
	TriggerBox->SetBoxExtent(Extent, true);

	// El charco: un decal que proyecta hacia abajo desde el centro de la caja y llega al suelo aunque el camino suba o baje
	// dentro de la zona. Sin el material (no se ha ejecutado Scripts/create_survival_decals.py) la zona frena sin verse.
	static TWeakObjectPtr<UMaterialInterface> CachedMaterial;
	UMaterialInterface* Material = CachedMaterial.Get();
	if (!Material)
	{
		Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_SlowZoneSyrupDecal.M_SlowZoneSyrupDecal"), nullptr, LOAD_NoWarn);
		CachedMaterial = Material;
	}
	if (!Material)
	{
		return;
	}
	if (!SyrupDecal)
	{
		SyrupDecal = NewObject<UDecalComponent>(this, TEXT("SyrupDecal"), RF_Transient);
		SyrupDecal->SetupAttachment(TriggerBox);
		// El eje X del decal es la dirección en que proyecta: hacia abajo.
		SyrupDecal->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
		SyrupDecal->SetDecalMaterial(Material);
		SyrupDecal->RegisterComponent();
		if (UMaterialInstanceDynamic* Instance = SyrupDecal->CreateDynamicMaterialInstance())
		{
			// Cada zona con su borde.
			Instance->SetScalarParameterValue(TEXT("Seed"), static_cast<float>(GetTypeHash(GetActorLocation()) % 997));
		}
	}
	// Tras girarlo, su X es la vertical (profundidad) y su Z, el largo de la zona. La profundidad cubre la caja entera y
	// 3 m por debajo, por si el suelo baja dentro de la zona.
	SyrupDecal->DecalSize = FVector(Extent.Z + 300.f, Extent.Y, Extent.X);
	SyrupDecal->MarkRenderStateDirty();
}
