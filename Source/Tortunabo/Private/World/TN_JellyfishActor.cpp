#include "World/TN_JellyfishActor.h"
#include "World/TN_HazardEffects.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Player/TortugaCharacter.h"
#include "Core/TN_DebugCVars.h"
#include "DrawDebugHelpers.h"
#include "World/TN_WorldTuning.h"
#include "Core/TN_Log.h"
#include "World/TN_PlaceholderArt.h"
#include "World/TN_PlaceholderArtMeshes.h"
#include "World/Beach/TN_BeachPropMeshes.h"
#include "Beach/TN_BeachTrapKit.h"
#include "World/Beach/TN_BeachTrampolineRules.h"
#include "World/Beach/TN_BeachTrapStatusComponent.h"

namespace TNJellyfishArt
{
	/** Semilla fija por variante: todas las medusas de una variante comparten malla. */
	constexpr uint32 SeedBase = 0x4A31u;

	/** Campana (parte animada de la receta) y cuerpo (brazos y filamentos), en este orden. */
	TPair<UStaticMesh*, UStaticMesh*> Meshes(int32 Variant)
	{
		const int32 Kind = FMath::Clamp(Variant, 0, 3);
		TNBeachProp::FParts Parts;
		bool bBuilt = false;
		auto Build = [&Parts, &bBuilt, Kind]()
		{
			if (!bBuilt)
			{
				TNBeachProp::BuildJellyfish(Parts, Kind, SeedBase + static_cast<uint32>(Kind));
				bBuilt = true;
			}
		};
		UStaticMesh* Bell = TNPlaceholderArt::CachedArtMesh(FString::Printf(TEXT("Jellyfish.%d.Bell"), Kind),
			[&](TNProcMesh::FTNProcMeshBuffers& B) { Build(); B = Parts.Moving; });
		UStaticMesh* Body = TNPlaceholderArt::CachedArtMesh(FString::Printf(TEXT("Jellyfish.%d.Body"), Kind),
			[&](TNProcMesh::FTNProcMeshBuffers& B) { Build(); B = Parts.Body; });
		return TPair<UStaticMesh*, UStaticMesh*>(Bell, Body);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

ATN_JellyfishActor::ATN_JellyfishActor()
{
	PrimaryActorTick.bCanEverTick = true;
	// Tick apagado por defecto: solo se enciende durante el squish (multicast) o
	// con TN.Enemy.Debug activo. Cooldowns y overlaps no necesitan tick.
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
	bAlwaysRelevant = true;      // Visible para espectadores aunque estén lejos
	SetReplicateMovement(false); // La posición se sincroniza una sola vez
	// Estática tras replicar InitialLocation: dormir el canal de red y despertarlo
	// puntualmente (FlushNetDormancy) al capturar posición o rebotar. Patrón de la
	// bola / physics object.
	NetDormancy = DORM_DormantAll;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	HeadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeadMesh"));
	HeadMesh->SetupAttachment(Root);
	HeadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Trigger de rebote: caja fina sobre la parte superior de la esfera.
	// Por defecto centrado en Z=0 del actor; en el BP se mueve para quedar encima del HeadMesh.
	BounceZone = CreateDefaultSubobject<UBoxComponent>(TEXT("BounceZone"));
	BounceZone->SetupAttachment(Root);
	BounceZone->SetBoxExtent(FVector(80.f, 80.f, 15.f));
	BounceZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BounceZone->SetCollisionResponseToAllChannels(ECR_Ignore);
	BounceZone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

// ─────────────────────────────────────────────────────────────────────────────
// BeginPlay
// ─────────────────────────────────────────────────────────────────────────────

void ATN_JellyfishActor::BeginPlay()
{
	Super::BeginPlay();

	// Guardar escala inicial del HeadMesh para la animación squish
	HeadMeshDefaultScale = HeadMesh->GetRelativeScale3D();
	BuildCodeArt();
	BuildTentacles();

	// Con el CVar de debug activo el draw vive en Tick → mantenerlo encendido.
	if (TNDebug::EnemyDebug != 0)
	{
		SetActorTickEnabled(true);
	}

	if (HasAuthority())
	{
		BounceZone->OnComponentBeginOverlap.AddDynamic(
			this, &ATN_JellyfishActor::OnBounceZoneBeginOverlap);

		// Capturar posición inicial un tick después (patrón chunk: ChildActorComponent
		// puede no haber terminado de posicionar en este mismo tick).
		FTimerDelegate Delegate;
		Delegate.BindUObject(this, &ATN_JellyfishActor::DeferredCaptureInitialLocation);
		GetWorldTimerManager().SetTimer(DeferredInitHandle, Delegate, TNWorldTuning::ChunkChildActorSettleDelay, false);
		GetWorldTimerManager().SetTimer(TentacleTimer, FTimerDelegate::CreateUObject(this, &ATN_JellyfishActor::CheckTentacles), 0.1f, true);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// EndPlay
// ─────────────────────────────────────────────────────────────────────────────

void ATN_JellyfishActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
	}
	Super::EndPlay(EndPlayReason);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick — animación squish (local en todas las máquinas)
// ─────────────────────────────────────────────────────────────────────────────

void ATN_JellyfishActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// TN.Enemy.Debug: BounceZone en verde (servidor) / naranja (cliente) para
	// detectar desalineaciones de la zona replicada. No-op en Shipping.
	if (TNDebug::EnemyDebug != 0 && BounceZone)
	{
		const FColor Color = HasAuthority() ? FColor::Green : FColor::Orange;
		DrawDebugBox(GetWorld(), BounceZone->GetComponentLocation(),
			BounceZone->GetScaledBoxExtent(), BounceZone->GetComponentQuat(),
			Color, false, -1.f, 0, 2.f);
		if (HasAuthority() && PlayerCooldownExpiry.Num() > 0)
		{
			const double Now = GetWorld()->GetTimeSeconds();
			int32 ActiveCooldowns = 0;
			for (const auto& Pair : PlayerCooldownExpiry)
			{
				if (Now < Pair.Value) { ++ActiveCooldowns; }
			}
			DrawDebugString(GetWorld(), GetActorLocation() + FVector(0, 0, 100.f),
				FString::Printf(TEXT("SRV cooldowns=%d"), ActiveCooldowns),
				nullptr, Color, 0.f, true);
		}
	}

	// Cooldowns de rebote: lazy por timestamp en OnBounceZoneBeginOverlap — ya no
	// se decrementan aquí.

	// ── Animación squish (local) ──────────────────────────────────────────────
	if (bSquishingIn)
	{
		SquishAlpha += DeltaTime / FMath::Max(SquishInDuration, 0.001f);
		if (SquishAlpha >= 1.f)
		{
			SquishAlpha = 1.f;
			bSquishingIn = false;
			bSquishingOut = true;
		}
		ApplySquishScale();
	}
	else if (bSquishingOut)
	{
		SquishAlpha -= DeltaTime / FMath::Max(SquishOutDuration, 0.001f);
		if (SquishAlpha <= 0.f)
		{
			SquishAlpha = 0.f;
			bSquishingOut = false;
			// Restaurar escala exacta para no acumular error de float
			HeadMesh->SetRelativeScale3D(HeadMeshDefaultScale);
		}
		ApplySquishScale();
	}

	// Auto-apagado: sin squish activo y sin debug, no hay nada que hacer por tick.
	// Se re-enciende en MulticastPlayBounceEffects (squish) o al activar el CVar.
	if (!bSquishingIn && !bSquishingOut && TNDebug::EnemyDebug == 0)
	{
		SetActorTickEnabled(false);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// ApplySquishScale — aplana la esfera en Z, expande en XY
// ─────────────────────────────────────────────────────────────────────────────

void ATN_JellyfishActor::BuildCodeArt()
{
	if (GetNetMode() == NM_DedicatedServer || !HeadMesh || !BounceZone || !TNPlaceholderArt::NeedsCodeArt(HeadMesh))
	{
		return;
	}
	const TPair<UStaticMesh*, UStaticMesh*> Art = TNJellyfishArt::Meshes(CodeArtVariant);
	if (!Art.Key || !Art.Value)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Jellyfish] %s: no se ha podido construir la medusa de código; se queda el marcador."), *GetName());
		return;
	}
	// La campana mide lo que la zona de rebote (la parte que se pisa) y su cima queda a ras de la cara de arriba.
	const FVector Zone = BounceZone->GetUnscaledBoxExtent() * BounceZone->GetRelativeScale3D() * 2.0;
	const FBox BellBox = Art.Key->GetBoundingBox();
	const float Fit = TNPlaceholderArt::FitScale(BellBox.GetSize(), FVector(Zone.X, Zone.Y, 0.0) * CodeArtSizeFactor);
	const double ZoneTop = BounceZone->GetRelativeLocation().Z + Zone.Z * 0.5;
	const FVector Origin(0.0, 0.0, ZoneTop - BellBox.Max.Z * Fit);
	// Bajo HeadMesh para que el squish la aplaste igual: se deshace su escala de reposo.
	const FVector Head = HeadMeshDefaultScale;
	const FVector Undo(1.0 / FMath::Max(Head.X, UE_KINDA_SMALL_NUMBER), 1.0 / FMath::Max(Head.Y, UE_KINDA_SMALL_NUMBER),
		1.0 / FMath::Max(Head.Z, UE_KINDA_SMALL_NUMBER));
	const FTransform Relative(FRotator::ZeroRotator, (Origin - HeadMesh->GetRelativeLocation()) * Undo, Undo * Fit);
	CodeArtBell = TNPlaceholderArt::AddArtPart(this, HeadMesh, Art.Key, Relative);
	CodeArtBody = TNPlaceholderArt::AddArtPart(this, HeadMesh, Art.Value, Relative);
	// Los tentáculos de cilindro bloqueaban: escondidos serían paredes invisibles. La cabeza conserva la suya (es la
	// medusa que se pisa, ahora con otra forma).
	const ECollisionEnabled::Type HeadCollision = HeadMesh->GetCollisionEnabled();
	TNPlaceholderArt::HidePlaceholders(this, true);
	HeadMesh->SetCollisionEnabled(HeadCollision);
}

void ATN_JellyfishActor::ApplySquishScale() const
{
	// SquishAlpha 0→1: Z se reduce a SquishZScale; XY crecen para conservar volumen
	const float ZScale = FMath::Lerp(1.f, SquishZScale, SquishAlpha);

	// Expansión XY: sqrt(1/ZScale) conserva volumen de forma esférica aproximada
	const float XYScale = FMath::Lerp(1.f, FMath::Sqrt(1.f / FMath::Max(SquishZScale, 0.01f)), SquishAlpha);

	HeadMesh->SetRelativeScale3D(HeadMeshDefaultScale * FVector(XYScale, XYScale, ZScale));
}

// ─────────────────────────────────────────────────────────────────────────────
// Replicación de posición (mismo patrón que SeagullActor)
// ─────────────────────────────────────────────────────────────────────────────

void ATN_JellyfishActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_JellyfishActor, InitialLocation);
}

void ATN_JellyfishActor::DeferredCaptureInitialLocation()
{
	InitialLocation = GetActorLocation();
	FlushNetDormancy();     // Despertar el canal para que la posición viaje (DORM_DormantAll)
}

void ATN_JellyfishActor::OnRep_InitialLocation()
{
	SetActorLocation(InitialLocation);
}


// ─────────────────────────────────────────────────────────────────────────────
// Overlap — detección del rebote (solo servidor)
// ─────────────────────────────────────────────────────────────────────────────

void ATN_JellyfishActor::OnBounceZoneBeginOverlap(
	UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (!HasAuthority())
	{
		return;
	}

	ATortugaCharacter* TurtleChar = Cast<ATortugaCharacter>(OtherActor);
	if (!TurtleChar)
	{
		return;
	}

	// Solo si el jugador llega desde arriba (Z negativa = cayendo/andando)
	if (TurtleChar->GetVelocity().Z > 50.f)
	{
		return;
	}

	// Cooldown por jugador para no disparar en cada tick mientras está encima.
	// Lazy por timestamp: se comprueba/renueva aquí, sin decremento por tick.
	const TWeakObjectPtr<ATortugaCharacter> WeakChar(TurtleChar);
	const double Now = GetWorld()->GetTimeSeconds();
	if (const double* Expiry = PlayerCooldownExpiry.Find(WeakChar))
	{
		if (Now < *Expiry)
		{
			return;
		}
	}
	PlayerCooldownExpiry.Add(WeakChar, Now + BounceCooldown);

	ApplyBounce(TurtleChar);
}

// ─────────────────────────────────────────────────────────────────────────────
// ApplyBounce — servidor aplica el impulso
// ─────────────────────────────────────────────────────────────────────────────

void ATN_JellyfishActor::ApplyBounce(ATortugaCharacter* TurtleChar)
{
	if (!TurtleChar)
	{
		return;
	}

	// bXYOverride=false mantiene velocidad horizontal del jugador.
	// bZOverride=true SUSTITUYE la Z → siempre MaxBounceVelocity, sin acumulación.
	TurtleChar->LaunchCharacter(FVector(0.f, 0.f, MaxBounceVelocity), false, true);

	// Posición de los pies del jugador para sonido y VFX
	const FVector EffectLocation = TurtleChar->GetActorLocation();

	// DORM_DormantAll: despertar el canal para que el multicast salga.
	FlushNetDormancy();
	MulticastPlayBounceEffects(EffectLocation);
}

// ─────────────────────────────────────────────────────────────────────────────
// MulticastPlayBounceEffects — sonido + VFX + squish (todos los clientes)
// ─────────────────────────────────────────────────────────────────────────────

void ATN_JellyfishActor::MulticastPlayBounceEffects_Implementation(FVector EffectLocation)
{
	// ── Sonido ──────────────────────────────────────────────────────────────────
	if (BounceSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, BounceSound, EffectLocation);
	}

	// ── VFX (Niagara) ────────────────────────────────────────────────────────────
	if (BounceVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), BounceVFX, EffectLocation,
			FRotator::ZeroRotator,
			FVector::OneVector,
			true,   // auto destroy
			true);  // auto activate
	}

	// ── Squish: reiniciar desde el principio si ya estaba animando ───────────────
	SquishAlpha = 0.f;
	bSquishingIn = true;
	bSquishingOut = false;
	SetActorTickEnabled(true); // El squish vive en Tick; se auto-apaga al terminar
}

// ─────────────────────────────────────────────────────────────────────────────
// Tentáculos (#683): pican desde la arena (aturdimiento corto y ralentización, sin daño)
// ─────────────────────────────────────────────────────────────────────────────

float ATN_JellyfishActor::BellRadius() const
{
	const FVector Zone = BounceZone ? BounceZone->GetScaledBoxExtent() : FVector(80.f);
	return static_cast<float>(FMath::Max(Zone.X, Zone.Y)) * CodeArtSizeFactor;
}

void ATN_JellyfishActor::BuildTentacles()
{
	if (GetNetMode() == NM_DedicatedServer || TentacleReach <= 0.f || TentacleMesh)
	{
		return;
	}
	const double Bell = BellRadius();
	// La medusa se coloca con el origen en el suelo (Supervivencia y chunks): los tentáculos, tendidos en la arena.
	constexpr double GroundZ = 0.0;
	TNBeachTrapKit::FBuffers B;
	const FLinearColor Pink = TNPlaygroundKit::Rgb(0xE59AC8, 0.45f);
	const uint32 Seed = static_cast<uint32>(GetUniqueID()) * 2654435761u;
	constexpr int32 Count = 10;
	for (int32 k = 0; k < Count; ++k)
	{
		const double A = TNPlaygroundKit::KitTwoPi * (k + 0.4 * TNPlaygroundKit::Hash01(k, 1, Seed)) / Count;
		const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0.0);
		const FVector Side(-Dir.Y, Dir.X, 0.0);
		FVector Prev = Dir * Bell * 0.85 + FVector(0.0, 0.0, GroundZ + 4.0);
		for (int32 s = 1; s <= 4; ++s)
		{
			const double U = s / 4.0;
			const FVector Next = Dir * FMath::Lerp(Bell * 0.85, Bell + TentacleReach, U) + Side * (18.0 * FMath::Sin(U * 6.0 + k))
				+ FVector(0.0, 0.0, GroundZ + 4.0);
			TNPlaygroundKit::AddRod(B, Prev, Next, FMath::Lerp(5.0, 2.0, U), 5, Pink, FVector::UpVector);
			Prev = Next;
		}
	}
	TentacleMesh = NewObject<UStaticMeshComponent>(this, TEXT("TentacleMesh"));
	TentacleMesh->SetupAttachment(Root);
	TNBeachTrapKit::ConfigureVisual(TentacleMesh);
	TentacleMesh->RegisterComponent();
	TNBeachTrapKit::SetMesh(TentacleMesh, this, B);
}

void ATN_JellyfishActor::CheckTentacles()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority() || TentacleReach <= 0.f)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	const double Bell = BellRadius();
	// Por encima de la parte de abajo de la zona de rebote se está sobre la campana.
	const double TentacleTop = FMath::Max(60.0, BounceZone ? BounceZone->GetRelativeLocation().Z - BounceZone->GetScaledBoxExtent().Z - 30.0 : 0.0);
	TArray<ATortugaCharacter*> Turtles;
	ATN_BeachEnemy::GatherTurtles(this, Turtles);
	for (ATortugaCharacter* Turtle : Turtles)
	{
		const FVector Local = GetActorTransform().InverseTransformPositionNoScale(Turtle->GetActorLocation());
		const double FeetZ = Local.Z - Turtle->GetSimpleCollisionHalfHeight();
		const double Rho = FVector2D(Local.X, Local.Y).Size();
		if (TNTrampolineRules::TentacleContact(Rho, FeetZ, Bell, Bell + TentacleReach, TentacleTop) != TNTrampolineRules::ETentacleContact::Sting)
		{
			continue;
		}
		const double* Last = LastSting.Find(Turtle);
		if ((Last && Now - *Last < TNTrampolineRules::StingCooldown) || !TNBeachTrapKit::IsFreeTurtle(Turtle))
		{
			continue;
		}
		LastSting.Add(Turtle, Now);
		TNBeach::StunTurtle(Turtle, TNTrampolineRules::StingStunSeconds);
		// Tentáculos de la medusa: veneno de la hoja (#871).
		TNHazard::Apply(UTN_HazardTuning::Get().JellyfishTentacles, Turtle, this);
		if (UTN_BeachTrapStatusComponent* Status = UTN_BeachTrapStatusComponent::FindOrAddTo(Turtle))
		{
			Status->ServerSlow(TNTrampolineRules::StingSpeedFactor, TNTrampolineRules::StingSlowSeconds);
		}
		FlushNetDormancy();
		MulticastSting(Turtle->GetActorLocation());
	}
}

void ATN_JellyfishActor::MulticastSting_Implementation(FVector_NetQuantize At)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (StingSound) { UGameplayStatics::SpawnSoundAtLocation(this, StingSound, At); }
	if (StingVFX) { UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, StingVFX, At); }
}
