#include "Testing/TN_MonkeyComponent.h"

#include "Components/CapsuleComponent.h"
#include "Core/TN_Log.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/App.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TortugaCharacter.h"
#include "Settings/TN_GameSettingsSubsystem.h"
#include "World/Beach/TN_BeachStun.h"

namespace TNMonkeyDetail
{
	/** Cuánto por debajo de la arena del generador (cm) cuenta como «bajo el terreno» y como «caída sin rescatar». */
	constexpr double UnderTerrainDepth = 150.0;
	constexpr double LostFallDepth = 3000.0;
	/** Segundos seguidos en ese estado antes de darlo por bueno (la red de seguridad de la carrera actúa antes). */
	constexpr float UnderTerrainSeconds = 2.f;
	constexpr float NoPawnSeconds = 10.f;
	constexpr float WatchInterval = 0.25f;
	/** Un salto de posición mayor que esto entre dos muestras es un teletransporte (rescate, patada): no suma distancia. */
	constexpr double TeleportDistance = 2000.0;
}

UTN_MonkeyComponent::UTN_MonkeyComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UTN_MonkeyComponent::Configure(int32 InSeed, int32 InPlayerIndex)
{
	Seed = InSeed;
	Stats = FTNMonkeyPlayerStats();
	Stats.PlayerIndex = InPlayerIndex;
	Planner = TNMonkey::FPlanner(InSeed, InPlayerIndex);
	bStepActive = false;
}

bool UTN_MonkeyComponent::CanUsePauseMenu() const
{
	// El menú de pausa es de la instancia de juego y de la interfaz real: solo con pantalla y para el primer jugador.
	return Stats.PlayerIndex == 0 && FApp::CanEverRender() && FSlateApplication::IsInitialized();
}

void UTN_MonkeyComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	Elapsed += DeltaTime;
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	ATortugaCharacter* Turtle = PC ? Cast<ATortugaCharacter>(PC->GetPawn()) : nullptr;

	if (Turtle != LastPawn.Get())
	{
		// Pawn nuevo (reaparición, rescate con respawn): lo que estuviera pulsado ya no vale.
		if (LastPawn.IsValid())
		{
			++Stats.PawnChanges;
		}
		bStepActive = false;
		bSprinting = false;
		bHaveLastLocation = false;
		Stuck = TNMonkey::FStuckDetector();
		LastPawn = Turtle;
	}

	Watch(Turtle, DeltaTime);
	if (!Turtle)
	{
		return;
	}

	if (!bStepActive)
	{
		Step = Planner.Next();
		StepElapsed = 0.f;
		bSecondJumpDone = false;
		bStepActive = true;
		BeginStep(Turtle);
	}
	StepElapsed += DeltaTime;
	TickStep(Turtle, DeltaTime);
	if (StepElapsed >= Step.Seconds)
	{
		EndStep(Turtle);
		bStepActive = false;
	}
}

void UTN_MonkeyComponent::BeginStep(ATortugaCharacter* Turtle)
{
	using TNMonkey::EAction;
	++Stats.ActionCounts[static_cast<int32>(Step.Action)];
	switch (Step.Action)
	{
		case EAction::Sprint:
			Turtle->StartSprint();
			bSprinting = true;
			break;
		case EAction::Jump:
		case EAction::DoubleJump:
			Turtle->Jump();
			break;
		case EAction::Shell:
			Turtle->ToggleShell();
			break;
		case EAction::Interact:
			Turtle->TryInteract();
			break;
		case EAction::UseItem:
			// De vez en vez cambia de mano el objeto antes de usarlo.
			if (Stats.ActionCounts[static_cast<int32>(EAction::UseItem)] % 4 == 1)
			{
				Turtle->RotateInventory();
			}
			Turtle->TryUseEquippedItem();
			break;
		case EAction::Emote:
			Turtle->TriggerEmote(Step.Arg);
			break;
		case EAction::Drop:
			Turtle->DropEquippedItem();
			break;
		case EAction::Pause:
			if (CanUsePauseMenu())
			{
				if (UTN_GameSettingsSubsystem* Settings = UTN_GameSettingsSubsystem::Get(this))
				{
					Settings->OpenPauseMenu(Cast<APlayerController>(GetOwner()));
					bPauseOpened = true;
				}
			}
			else
			{
				++Stats.PauseSkipped;
			}
			break;
		default:
			break;
	}
}

void UTN_MonkeyComponent::TickStep(ATortugaCharacter* Turtle, float DeltaTime)
{
	using TNMonkey::EAction;
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	switch (Step.Action)
	{
		case EAction::Walk:
		case EAction::Sprint:
		case EAction::Shell:
			if (!bPauseOpened)
			{
				Turtle->Move(FInputActionValue(Step.Move));
			}
			[[fallthrough]];
		case EAction::Look:
			if (PC && Step.YawRate != 0.f)
			{
				FRotator Rotation = PC->GetControlRotation();
				Rotation.Yaw += Step.YawRate * DeltaTime;
				PC->SetControlRotation(Rotation);
			}
			break;
		case EAction::DoubleJump:
			// Segundo pulso en el aire (panzazo) pasado un rato del primero.
			if (!bSecondJumpDone && StepElapsed >= 0.35f)
			{
				bSecondJumpDone = true;
				Turtle->Jump();
			}
			break;
		default:
			break;
	}
}

void UTN_MonkeyComponent::EndStep(ATortugaCharacter* Turtle)
{
	using TNMonkey::EAction;
	switch (Step.Action)
	{
		case EAction::Sprint:
			Turtle->StopSprint();
			bSprinting = false;
			break;
		case EAction::Jump:
		case EAction::DoubleJump:
			Turtle->StopJumping();
			break;
		case EAction::Shell:
			if (Step.Arg == 1 && Turtle->IsInShell())
			{
				Turtle->ToggleShell();
			}
			break;
		case EAction::Interact:
			Turtle->ReleaseInteract();
			break;
		case EAction::Pause:
			if (bPauseOpened)
			{
				bPauseOpened = false;
				if (UTN_GameSettingsSubsystem* Settings = UTN_GameSettingsSubsystem::Get(this))
				{
					Settings->ClosePauseMenu();
				}
			}
			break;
		default:
			break;
	}
	if (Step.Action == EAction::Walk || Step.Action == EAction::Sprint || Step.Action == EAction::Shell)
	{
		Turtle->OnMoveReleased();
	}
}

void UTN_MonkeyComponent::ReleaseAll()
{
	ATortugaCharacter* Turtle = LastPawn.IsValid() ? const_cast<ATortugaCharacter*>(LastPawn.Get()) : nullptr;
	if (Turtle)
	{
		if (bSprinting)
		{
			Turtle->StopSprint();
		}
		Turtle->ReleaseInteract();
		Turtle->OnMoveReleased();
	}
	bSprinting = false;
	if (bPauseOpened)
	{
		bPauseOpened = false;
		if (UTN_GameSettingsSubsystem* Settings = UTN_GameSettingsSubsystem::Get(this))
		{
			Settings->ClosePauseMenu();
		}
	}
	bStepActive = false;
}

void UTN_MonkeyComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseAll();
	Super::EndPlay(EndPlayReason);
}

void UTN_MonkeyComponent::AddEvent(const TCHAR* Kind, const FVector& Where, const FString& Detail)
{
	FTNMonkeyEvent& Event = Stats.Events.AddDefaulted_GetRef();
	Event.Kind = Kind;
	Event.Time = Elapsed;
	Event.Location = Where;
	Event.Detail = Detail;
	UE_LOG(LogTortunabo, Warning, TEXT("[Monkey] jugador %d, %s en %.1f s, en (%.1f, %.1f, %.1f) m: %s"), Stats.PlayerIndex, Kind, Elapsed, Where.X / 100.0,
		Where.Y / 100.0, Where.Z / 100.0, *Detail);
}

void UTN_MonkeyComponent::Watch(const ATortugaCharacter* Turtle, float DeltaTime)
{
	using namespace TNMonkeyDetail;
	if (!Turtle)
	{
		Stats.NoPawnSeconds += DeltaTime;
		if (Stats.NoPawnSeconds > NoPawnSeconds && !bNoPawnReported)
		{
			bNoPawnReported = true;
			AddEvent(TEXT("no_pawn"), FVector::ZeroVector, FString::Printf(TEXT("sin tortuga desde hace %.0f s"), Stats.NoPawnSeconds));
		}
		return;
	}
	Stats.NoPawnSeconds = 0.f;
	bNoPawnReported = false;

	WatchClock += DeltaTime;
	if (WatchClock < WatchInterval)
	{
		return;
	}
	const float Dt = WatchClock;
	WatchClock = 0.f;

	const FVector Location = Turtle->GetActorLocation();
	if (!bHaveStartZ)
	{
		bHaveStartZ = true;
		StartZ = Location.Z;
	}
	if (bHaveLastLocation)
	{
		const double Moved = FVector::Dist2D(Location, LastLocation);
		if (Moved < TeleportDistance)
		{
			Stats.DistanceCm += Moved;
		}
	}
	LastLocation = Location;
	bHaveLastLocation = true;
	const double FeetZ = Location.Z - (Turtle->GetCapsuleComponent() ? Turtle->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.0);
	Stats.MinZ = FMath::Min(Stats.MinZ, FeetZ);

	// Profundidad: 70 m por debajo de donde empezó cuenta como caída perdida.
	const double Depth = (StartZ - FeetZ) - 7000.0;
	Stats.MaxDepthUnderGround = FMath::Max(Stats.MaxDepthUnderGround, Depth);

	DepthClock = Depth > UnderTerrainDepth ? DepthClock + Dt : 0.f;
	DeepClock = Depth > LostFallDepth ? DeepClock + Dt : 0.f;
	if (DepthClock >= UnderTerrainSeconds && !bUnderReported)
	{
		bUnderReported = true;
		AddEvent(TEXT("under_terrain"), Location, FString::Printf(TEXT("%.0f cm bajo la arena del generador (%s)"), Depth, TNMonkey::ActionName(Step.Action)));
	}
	if (DeepClock >= UnderTerrainSeconds && !bFallReported)
	{
		bFallReported = true;
		AddEvent(TEXT("unrescued_fall"), Location, FString::Printf(TEXT("%.0f cm bajo la arena y sin rescatar"), Depth));
	}
	if (Depth < 50.0)
	{
		bUnderReported = false;
		bFallReported = false;
	}

	const UCharacterMovementComponent* Movement = Turtle->GetCharacterMovement();
	const bool bTrying = bStepActive && (Step.Action == TNMonkey::EAction::Walk || Step.Action == TNMonkey::EAction::Sprint)
		&& !TNBeach::IsTurtleStunned(Turtle) && !Turtle->IsInShell() && !Turtle->IsKnockedDown() && !Turtle->IsDead()
		&& !(Turtle->GetCarryComponent() && Turtle->GetCarryComponent()->IsBeingCarried()) && !bPauseOpened;
	if (Stuck.Update(Dt, Location, bTrying))
	{
		AddEvent(TEXT("stuck"), Location,
			FString::Printf(TEXT("%.0f s empujando sin avanzar 1 m (%s, modo %d, velocidad %.0f cm/s)"), Stuck.Window, TNMonkey::ActionName(Step.Action),
				Movement ? static_cast<int32>(Movement->MovementMode.GetValue()) : -1, Movement ? Movement->Velocity.Size() : 0.0));
	}
}
