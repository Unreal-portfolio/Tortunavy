#include "World/TN_RescuePickup.h"
#include "Core/TN_Log.h"
#include "Game/TN_RunGameMode.h"
#include "Core/TN_CoopPlayerState.h"
#include "Player/TortugaCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "World/TN_PlaceholderArt.h"
#include "World/TN_PlaceholderArtMeshes.h"
#include "Lobby/TN_CastleKit.h"
#include "Game/TN_ChapaRules.h"
#include "Game/TN_ItemRuntime.h"
#include "Player/TN_InventoryComponent.h"
#include "Settings/TN_EconomySettings.h"

ATN_RescuePickup::ATN_RescuePickup()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(true);
	NetDormancy = DORM_Awake;
	SetNetUpdateFrequency(15.f);
	SetMinNetUpdateFrequency(5.f);
	PromptText = NSLOCTEXT("Tortunabo", "RescuePrompt", "Rescatar");
	// Como el alcance de la tortuga (250; antes 300, pero el escaneo ya solo encontraba lo que estuviera a 350): la del
	// interactuable solo cuenta en el servidor, y más que el escaneo no sirve de nada.
	InteractionDistance = ATortugaCharacter::DefaultInteractionDistance;
}

void ATN_RescuePickup::BeginPlay()
{
	// Revivir pagando (#862): el aviso dice cuánto cuesta (antes de que la base lo pase al aviso 3D).
	if (const int32 Cost = ReviveCost(); Cost > 0)
	{
		PromptText = FText::Format(NSLOCTEXT("TNEconomy", "RevivePrompt", "Revivir: {0} {0}|plural(one=chapa,other=chapas)"), Cost);
	}
	Super::BeginPlay();
	BuildCodeArt();
}

// ── Revivir con chapas (#862) ──────────────────────────────────────────────────────────────────────────────────────

int32 ATN_RescuePickup::ReviveCost()
{
	return UTN_EconomySettings::Get().ReviveChapaCost;
}

bool ATN_RescuePickup::ChargeAndRevive(APawn* Payer, int32 Cost, TFunctionRef<bool()> Revive)
{
	if (Cost <= 0)
	{
		return Revive();
	}
	const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Payer);
	UTN_InventoryComponent* Inventory = Turtle ? Turtle->GetInventoryComponent() : nullptr;
	if (!Inventory || !TNChapaRules::CanPayRevive(Inventory->GetChapaCount(), Cost))
	{
		return false;
	}
	// Primero revive y luego cobra: si no se ha podido revivir, no se cobra nada.
	if (!Revive())
	{
		return false;
	}
	Inventory->TrySpendChapas(Cost);
	return true;
}

float ATN_RescuePickup::GetHoldDuration() const
{
	return ReviveCost() > 0 ? UTN_EconomySettings::Get().ReviveHoldSeconds : 0.f;
}

void ATN_RescuePickup::BeginHoldInteract(APawn* Interactor)
{
	if (!HasAuthority() || !Interactor || (HoldPawn && HoldPawn != Interactor) || !CanInteract(Interactor))
	{
		return;
	}
	const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Interactor);
	const UTN_InventoryComponent* Inventory = Turtle ? Turtle->GetInventoryComponent() : nullptr;
	if (!Inventory || !TNChapaRules::CanPayRevive(Inventory->GetChapaCount(), ReviveCost()))
	{
		// Sin chapas suficientes no empieza: dos notas graves para quien lo intenta.
		if (ACharacter* Character = Cast<ACharacter>(Interactor))
		{
			TNItemRuntime::PlayCue(Character, ETNRaceSound::Nope);
		}
		return;
	}
	HoldPawn = Interactor;
	HoldStart = static_cast<float>(TNItemRuntime::ServerNow(GetWorld()));
}

void ATN_RescuePickup::EndHoldInteract(APawn* Interactor)
{
	if (HasAuthority() && Interactor && HoldPawn == Interactor)
	{
		ClearHold();
	}
}

float ATN_RescuePickup::GetHoldProgress(const APawn* Interactor) const
{
	if (!Interactor || HoldPawn != Interactor || GetHoldDuration() <= 0.f)
	{
		return -1.f;
	}
	const double Held = TNItemRuntime::ServerNow(GetWorld()) - static_cast<double>(HoldStart);
	return FMath::Clamp(static_cast<float>(Held / GetHoldDuration()), 0.f, 1.f);
}

void ATN_RescuePickup::ClearHold()
{
	HoldPawn = nullptr;
	HoldStart = 0.f;
}

void ATN_RescuePickup::BuildCodeArt()
{
	if (GetNetMode() == NM_DedicatedServer || !Mesh || !TNPlaceholderArt::NeedsCodeArt(Mesh))
	{
		return;
	}
	// El huevo entero (base y tapa en una pieza) con las bandas de color de los huevos de la salida.
	UStaticMesh* Egg = TNPlaceholderArt::CachedArtMesh(TEXT("Rescue.Egg"), [](TNProcMesh::FTNProcMeshBuffers& B)
	{
		const TArray<double> Zs(TNCastleKit::EggZ(), TNCastleKit::EggProfileNum);
		const TArray<double> Rs(TNCastleKit::EggR(), TNCastleKit::EggProfileNum);
		constexpr int32 Segments = 16;
		constexpr int32 AccentEvery = 3;
		TNCastleKit::AddRevolution(B, FVector::ZeroVector, Zs, Rs, Segments, TNCastleKit::Pal(0xFFF3DC), true,
			TNCastleKit::Pal(TNCastleKit::EggAccent(0)), AccentEvery);
	}, TNPlaceholderArt::MatteAlpha);
	if (!Egg)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Rescue] %s: no se ha podido construir el huevo; se queda el marcador."), *GetName());
		return;
	}
	USceneComponent* Anchor = GetRootComponent();
	const FTransform Relative(FRotator::ZeroRotator, FVector(0.0, 0.0, CodeArtLift), FVector(CodeArtEggScale));
	CodeArtEgg = TNPlaceholderArt::AddArtPart(this, Anchor, Egg, Relative);
	// El cubo conserva su colisión: es lo que encuentra el escaneo de interacción de la tortuga.
	TNPlaceholderArt::HidePlaceholders(this, false);
}

void ATN_RescuePickup::AnimateCodeArt(float DeltaSeconds)
{
	if (!CodeArtEgg)
	{
		return;
	}
	CodeArtClock += DeltaSeconds;
	const float Bob = CodeArtBobAmplitude * FMath::Sin(CodeArtClock * UE_TWO_PI * CodeArtBobHz);
	const float Yaw = FMath::Fmod(CodeArtClock * CodeArtSpinDegreesPerSecond, 360.f);
	CodeArtEgg->SetRelativeLocationAndRotation(FVector(0.f, 0.f, CodeArtLift + Bob), FRotator(0.f, Yaw, 0.f));
}

void ATN_RescuePickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AnimateCodeArt(DeltaSeconds);

	if (!HasAuthority())
	{
		return;
	}

	// Revivir pagando: se completa al mantener la tecla el tiempo entero, cerca del cuerpo y aún muerto.
	if (HoldPawn)
	{
		const double Held = TNItemRuntime::ServerNow(GetWorld()) - static_cast<double>(HoldStart);
		const double MaxReach = ATortugaCharacter::DefaultInteractionDistance + 100.0;
		APawn* Reviver = HoldPawn;
		if (!IsValid(Reviver) || !CanInteract(Reviver) || FVector::DistSquared(Reviver->GetActorLocation(), GetActorLocation()) > MaxReach * MaxReach)
		{
			ClearHold();
		}
		else if (Held >= GetHoldDuration())
		{
			ClearHold();
			ServerRevive(Reviver);
			return;
		}
	}

	if (!FollowedDeadPawn.IsValid())
	{
		return;
	}

	const FVector FollowLocation = ResolveFollowLocation();
	SetActorLocation(FollowLocation + FVector(0.f, 0.f, 35.f), false, nullptr, ETeleportType::TeleportPhysics);
}

void ATN_RescuePickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_RescuePickup, DeadPlayerId);
	DOREPLIFETIME(ATN_RescuePickup, HoldPawn);
	DOREPLIFETIME(ATN_RescuePickup, HoldStart);
}

void ATN_RescuePickup::SetDeadPlayerId(int32 InPlayerId)
{
	DeadPlayerId = InPlayerId;
}

void ATN_RescuePickup::FollowDeadPawn(APawn* InDeadPawn, FName InTrackingBone)
{
	FollowedDeadPawn = InDeadPawn;
	TrackingBone = InTrackingBone.IsNone() ? TEXT("pelvis") : InTrackingBone;

	if (HasAuthority())
	{
		FlushNetDormancy();
		SetActorLocation(ResolveFollowLocation() + FVector(0.f, 0.f, 35.f), false, nullptr, ETeleportType::TeleportPhysics);
		ForceNetUpdate();
	}
}

FVector ATN_RescuePickup::ResolveFollowLocation() const
{
	APawn* DeadPawn = FollowedDeadPawn.Get();
	if (!DeadPawn)
	{
		return GetActorLocation();
	}

	if (const ATortugaCharacter* Character = Cast<ATortugaCharacter>(DeadPawn))
	{
		if (USkeletalMeshComponent* RagdollMesh = Character->GetMesh())
		{
			const int32 BoneIdx = RagdollMesh->GetBoneIndex(TrackingBone);
			if (BoneIdx != INDEX_NONE)
			{
				return RagdollMesh->GetBoneLocation(TrackingBone, EBoneSpaces::WorldSpace);
			}
		}
	}

	return DeadPawn->GetActorLocation();
}

bool ATN_RescuePickup::CanInteract(APawn* Interactor) const
{
	if (!Super::CanInteract(Interactor))
	{
		return false;
	}

	// No puede rescatarse a sí mismo ni interactuar si el jugador muerto ya no existe
	if (DeadPlayerId < 0)
	{
		return false;
	}

	// El muerto debe seguir conectado y seguir eliminado. Sin este check, un pickup
	// rezagado (dueño ya revivido por tótem, o desconectado) mostraría el prompt y
	// se consumiría sin efecto. PlayerArray + flags replicados → válido también en
	// clientes para el prompt.
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!GS)
	{
		return false;
	}
	const ATN_CoopPlayerState* DeadPS = nullptr;
	for (APlayerState* BasePS : GS->PlayerArray)
	{
		if (BasePS && BasePS->GetPlayerId() == DeadPlayerId)
		{
			DeadPS = Cast<ATN_CoopPlayerState>(BasePS);
			break;
		}
	}
	if (!DeadPS || DeadPS->bIsAlive || !DeadPS->bIsEliminated)
	{
		return false;
	}

	// No dejar que el propio jugador muerto (si de alguna forma tiene pawn) se auto-rescue
	if (Interactor)
	{
		if (APlayerController* InteractorPC = Cast<APlayerController>(Interactor->GetController()))
		{
			if (APlayerState* PS = InteractorPC->GetPlayerState<APlayerState>())
			{
				if (PS->GetPlayerId() == DeadPlayerId)
				{
					return false;
				}
			}
		}
	}

	return true;
}

void ATN_RescuePickup::Interact(APawn* Interactor)
{
	if (HasAuthority())
	{
		ServerRevive(Interactor);
	}
}

bool ATN_RescuePickup::ServerRevive(APawn* Interactor)
{
	if (!HasAuthority())
	{
		return false;
	}

	// Buscar el PlayerController del jugador muerto por PlayerId
	APlayerController* DeadPC = nullptr;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC) { continue; }
		if (APlayerState* PS = PC->GetPlayerState<APlayerState>())
		{
			if (PS->GetPlayerId() == DeadPlayerId)
			{
				DeadPC = PC;
				break;
			}
		}
	}

	if (!DeadPC)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[RescuePickup] Could not find PlayerController for DeadPlayerId=%d"), DeadPlayerId);
		Destroy();
		return false;
	}

	// RevivePlayer del GameMode; si cuesta chapas, se cobran a quien revive solo si de verdad vuelve a la vida.
	const int32 Cost = ReviveCost();
	ATN_RunGameMode* RunGM = Cast<ATN_RunGameMode>(GetWorld()->GetAuthGameMode());
	const ATN_CoopPlayerState* DeadPS = DeadPC->GetPlayerState<ATN_CoopPlayerState>();
	const bool bDone = ChargeAndRevive(Interactor, Cost, [&]() -> bool
	{
		if (!RunGM)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[RescuePickup] No TN_RunGameMode found — cannot revive"));
			return Cost <= 0;
		}
		RunGM->RevivePlayer(DeadPC);
		return Cost <= 0 || (DeadPS && DeadPS->bIsAlive);
	});
	if (!bDone)
	{
		if (ACharacter* Character = Cast<ACharacter>(Interactor))
		{
			TNItemRuntime::PlayCue(Character, ETNRaceSound::Nope);
		}
		UE_LOG(LogTortunabo, Log, TEXT("[RescuePickup] %s no puede revivir a Id=%d (faltan chapas: cuesta %d)."), *GetNameSafe(Interactor), DeadPlayerId, Cost);
		return false;
	}
	UE_LOG(LogTortunabo, Log, TEXT("[RescuePickup] Revived player (Id=%d) at (%.0f,%.0f,%.0f), %d chapas"),
		DeadPlayerId, GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z, Cost);

	// Llamar OnInteracted para hooks BP
	OnInteracted(Interactor);

	Destroy();
	return true;
}

