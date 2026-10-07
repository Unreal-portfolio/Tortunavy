#include "World/TN_SlowZoneVolume.h"
#include "Components/BoxComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
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
