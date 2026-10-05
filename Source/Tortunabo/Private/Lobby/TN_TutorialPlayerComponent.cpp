#include "Lobby/TN_TutorialPlayerComponent.h"
#include "Lobby/TN_TutorialCourse.h"
#include "Lobby/TN_TutorialWidget.h"
#include "Lobby/TN_TutorialRules.h"
#include "TN_TutorialLayout.h"
#include "TN_TutorialTexts.h"
#include "Core/TN_Log.h"
#include "Player/TN_DebugRpcDecisions.h"
#include "Core/TN_InventoryTypes.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Multiplayer/TN_LocalPlaySubsystem.h"
#include "Settings/TN_GameSettingsSubsystem.h"
#include "Player/TortugaCharacter.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_InventoryComponent.h"
#include "Player/TN_ShellComponent.h"
#include "Player/TN_StaminaComponent.h"
#include "Voice/ProximityVoiceComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Net/UnrealNetwork.h"
#include "VR/TN_VRControls.h"
#include "VR/TN_VRMode.h"
#include "Settings/TN_InputDeviceSubsystem.h"

namespace TNTutorialPlayerDetail
{
	using TNTutorial::EKey;
	using TNTutorial::EStation;

	/** Capa del cartel en el viewport: encima del HUD de la tortuga (4) y del flujo del lobby (5), debajo de la voz (10). */
	constexpr int32 WidgetZOrder = 6;
	/** Cuánto se queda el mensaje final (s). */
	constexpr float FarewellSeconds = 6.5f;
	/** Si la caída de la cascada no acaba en este tiempo (algo raro), la tortuga vuelve a moverse. */
	constexpr float MaxFallSeconds = 25.f;

	/** Fila de controles (UTN_GameSettingsSubsystem::GetKeyBindings) de cada tecla de botón del tutorial. */
	const TCHAR* RowIdOf(EKey Key)
	{
		switch (Key)
		{
			case EKey::Sprint:          return TEXT("IA_Sprint");
			case EKey::Jump:            return TEXT("IA_Jump");
			case EKey::Interact:        return TEXT("IA_Interact");
			case EKey::RotateInventory: return TEXT("IA_RotateInventory");
			case EKey::DropItem:        return TEXT("IA_DropItem");
			case EKey::Shell:           return TEXT("IA_Shell");
			case EKey::EmoteWheel:      return TEXT("IA_OpenEmoteWheel");
			case EKey::ChatWheel:       return TEXT("IA_OpenChatWheel");
			case EKey::Talk:            return TEXT("Talk");
			case EKey::Pause:           return TEXT("Pause");
			default:                    return nullptr;
		}
	}

	/** Lo que se enseña sin ajustes (servidor dedicado o sin subsistema): las teclas de serie, con sus nombres traducidos. */
	FText FallbackLabel(EKey Key, bool bPad)
	{
		auto Name = [](const FKey& K) { return UTN_GameSettingsSubsystem::KeyDisplayName(K); };
		switch (Key)
		{
			case EKey::Move:            return bPad ? NSLOCTEXT("TNTutorial", "PadMove", "Stick izquierdo") : INVTEXT("W A S D");
			case EKey::Look:            return bPad ? NSLOCTEXT("TNTutorial", "PadLook", "Stick derecho") : NSLOCTEXT("TNTutorial", "MouseLook", "Ratón");
			case EKey::Sprint:          return Name(bPad ? EKeys::Gamepad_RightTrigger : EKeys::LeftShift);
			case EKey::Jump:            return bPad ? Name(EKeys::Gamepad_FaceButton_Bottom) : NSLOCTEXT("TNTutorial", "SpaceKey", "Espacio");
			case EKey::Interact:        return Name(bPad ? EKeys::Gamepad_FaceButton_Left : EKeys::E);
			case EKey::RotateInventory: return Name(bPad ? EKeys::Gamepad_RightShoulder : EKeys::G);
			case EKey::DropItem:        return Name(bPad ? EKeys::Gamepad_FaceButton_Top : EKeys::X);
			case EKey::Shell:           return Name(bPad ? EKeys::Gamepad_FaceButton_Right : EKeys::LeftControl);
			case EKey::EmoteWheel:      return Name(bPad ? EKeys::Gamepad_LeftTrigger : EKeys::Q);
			case EKey::ChatWheel:       return Name(bPad ? EKeys::Gamepad_LeftShoulder : EKeys::C);
			case EKey::Talk:            return bPad ? NSLOCTEXT("TNTutorial", "PadTalk", "Cruceta abajo") : Name(EKeys::V);
			case EKey::Pause:           return Name(bPad ? EKeys::Gamepad_Special_Right : (GIsEditor ? EKeys::Tab : EKeys::Escape));
			default:                    return FText::GetEmpty();
		}
	}

	bool IsSwimming(const ATortugaCharacter* Turtle)
	{
		const UCharacterMovementComponent* Move = Turtle ? Turtle->GetCharacterMovement() : nullptr;
		return Move && Move->IsSwimming();
	}

	FName ItemIdOf(const FTN_InventoryItem& Item) { return Item.IsValid() ? Item.ItemId : NAME_None; }
}

UTN_TutorialPlayerComponent::UTN_TutorialPlayerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(true);
	TaskDone.Init(false, TNTutorial::NumStations * TNTutorial::MaxTasks);
	StationCelebrated.Init(false, TNTutorial::NumStations);
}

void UTN_TutorialPlayerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UTN_TutorialPlayerComponent, bInTutorial, COND_OwnerOnly);
}

UTN_TutorialPlayerComponent* UTN_TutorialPlayerComponent::EnsureFor(APlayerController* PC)
{
	if (!PC || !PC->HasAuthority())
	{
		return nullptr;
	}
	if (UTN_TutorialPlayerComponent* Existing = PC->FindComponentByClass<UTN_TutorialPlayerComponent>())
	{
		return Existing;
	}
	UTN_TutorialPlayerComponent* Comp = NewObject<UTN_TutorialPlayerComponent>(PC, TEXT("TN_TutorialPlayer"));
	Comp->SetIsReplicated(true);
	Comp->RegisterComponent();
	return Comp;
}

UTN_TutorialPlayerComponent* UTN_TutorialPlayerComponent::FindFor(const APlayerController* PC)
{
	return PC ? PC->FindComponentByClass<UTN_TutorialPlayerComponent>() : nullptr;
}

UTN_TutorialPlayerComponent* UTN_TutorialPlayerComponent::FindLocal(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? FindFor(World->GetFirstPlayerController()) : nullptr;
}

APlayerController* UTN_TutorialPlayerComponent::GetPC() const
{
	return Cast<APlayerController>(GetOwner());
}

bool UTN_TutorialPlayerComponent::IsLocal() const
{
	const APlayerController* PC = GetPC();
	return PC && PC->IsLocalController();
}

ATN_TutorialCourse* UTN_TutorialPlayerComponent::GetCourse() const
{
	return ATN_TutorialCourse::Find(this);
}

void UTN_TutorialPlayerComponent::BeginPlay()
{
	Super::BeginPlay();
	ResetProgress();
}

void UTN_TutorialPlayerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bIgnoringMove)
	{
		if (APlayerController* PC = GetPC())
		{
			PC->SetIgnoreMoveInput(false);
		}
		bIgnoringMove = false;
	}
	RemoveWidget();
	Super::EndPlay(EndPlayReason);
}

// ─────────────────────────────────────────────────────────────────────────────
// Peticiones del jugador local
// ─────────────────────────────────────────────────────────────────────────────

void UTN_TutorialPlayerComponent::RequestStart(bool bForce)
{
	if (ATN_TutorialCourse* Course = GetCourse())
	{
		// El recorrido montado ya aquí: el servidor puede ponernos encima enseguida.
		Course->EnsureBuilt();
	}
	CheckedWorld = GetWorld();
	ServerRequestTutorial(bForce);
}

void UTN_TutorialPlayerComponent::RequestSkip()
{
	if (!bInTutorial)
	{
		// Fuera del tutorial (en una partida, o ya en el lobby): solo se apunta como hecho.
		SaveCompleted(true);
		return;
	}
	ServerSkipTutorial();
}

void UTN_TutorialPlayerComponent::RequestStation(int32 StationIndex)
{
	if (ATN_TutorialCourse* Course = GetCourse())
	{
		Course->EnsureBuilt();
	}
	CheckedWorld = GetWorld();
	ServerGoToStation(StationIndex);
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor
// ─────────────────────────────────────────────────────────────────────────────

void UTN_TutorialPlayerComponent::ServerRequestTutorial_Implementation(bool bForce)
{
	APlayerController* PC = GetPC();
	ATN_TutorialCourse* Course = GetCourse();
	if (!PC || !Course)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Tutorial] %s pide el tutorial, pero aquí no hay recorrido (solo en el lobby)."), *GetNameSafe(PC));
		return;
	}
	if (Course->IsParticipant(PC))
	{
		// Ya dentro (se unió con ?TNTut=1 y su máquina aún no lo sabía): solo se vuelve a la salida si se fuerza.
		if (bForce)
		{
			Course->GoToStation(PC, 0);
		}
		return;
	}
	Course->StartFor(PC);
}

void UTN_TutorialPlayerComponent::ServerSkipTutorial_Implementation()
{
	APlayerController* PC = GetPC();
	ATN_TutorialCourse* Course = GetCourse();
	if (Course && Course->IsParticipant(PC))
	{
		Course->FinishFor(PC, true);
		return;
	}
	SetInTutorialOnServer(false);
	ClientTutorialFinished(true);
}

void UTN_TutorialPlayerComponent::ServerGoToStation_Implementation(int32 StationIndex)
{
	ATN_TutorialCourse* Course = GetCourse();
	APlayerController* PC = GetPC();
	if (!Course || !PC)
	{
		return;
	}
	// Saltar a cualquier estación (y entrar si hace falta) es de pruebas: solo el anfitrión y fuera de Shipping. Un cliente
	// solo puede volver a una estación que ya ha pisado; antes se teletransportaba a donde quería con cualquier número (#17).
	const bool bDebugJump = TNDebugRpcLogic::CanRunHostOnlyDebugRpc(TNDebugRpcLogic::IsShippingBuild(), GetNetMode(), PC->IsLocalController());
	if (!TNTutorialRules::CanGoToStation(StationIndex, TNTutorial::NumStations, Course->GetReachedStation(PC), bDebugJump))
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Tutorial] %s pide ir a la estación %d: rechazada (no existe o aún no ha llegado)."),
			*GetNameSafe(PC), StationIndex + 1);
		return;
	}
	Course->GoToStation(PC, StationIndex);
}

void UTN_TutorialPlayerComponent::SetInTutorialOnServer(bool bIn)
{
	if (bInTutorial == bIn)
	{
		return;
	}
	bInTutorial = bIn;
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
	// El anfitrión no recibe OnRep.
	if (IsLocal())
	{
		HandleInTutorialChanged();
	}
}

void UTN_TutorialPlayerComponent::OnRep_InTutorial()
{
	if (IsLocal())
	{
		HandleInTutorialChanged();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Cliente dueño
// ─────────────────────────────────────────────────────────────────────────────

void UTN_TutorialPlayerComponent::ClientTaskDone_Implementation(uint8 StationIndex, uint8 TaskIndex)
{
	MarkTask(StationIndex, TaskIndex);
	RefreshWidget();
}

void UTN_TutorialPlayerComponent::ClientResetProgress_Implementation()
{
	// Las tareas tachadas, la estación y las medidas son locales: se vacían como al entrar de nuevas (HandleInTutorialChanged).
	ResetProgress();
	FinalMessageSeconds = 0.f;
	RefreshWidget();
}

void UTN_TutorialPlayerComponent::StartLocalFall(ATortugaCharacter* Turtle)
{
	APlayerController* PC = GetPC();
	if (bFalling || !Turtle || !PC)
	{
		return;
	}
	bFalling = true;
	FallSeconds = 0.f;
	// Sin velocidad horizontal ni control en el aire: se cae justo debajo del borde, en la plaza del castillo.
	Turtle->LaunchCharacter(FVector(0.0, 0.0, FMath::Min(0.0, Turtle->GetVelocity().Z)), true, true);
	if (!bIgnoringMove)
	{
		PC->SetIgnoreMoveInput(true);
		bIgnoringMove = true;
	}
}

void UTN_TutorialPlayerComponent::ClientTutorialFinished_Implementation(bool bSkipped)
{
	using namespace TNTutorialPlayerDetail;
	SaveCompleted(bSkipped);
	FinalMessageSeconds = FarewellSeconds;
	if (!bSkipped)
	{
		// Si el servidor lo ha visto antes que esta máquina (con retraso), la caída recta empieza ya.
		APlayerController* PC = GetPC();
		StartLocalFall(PC ? Cast<ATortugaCharacter>(PC->GetPawn()) : nullptr);
	}
	EnsureWidget();
	if (Widget)
	{
		if (bSkipped)
		{
			Widget->ShowFarewell(NSLOCTEXT("TNTutorial", "SkippedTitle", "Tutorial saltado"),
				NSLOCTEXT("TNTutorial", "SkippedText", "Lo que no hayas visto te lo cuenta el General Galápago, en la pestaña «Controles»."));
		}
		else
		{
			Widget->ShowFarewell(NSLOCTEXT("TNTutorial", "DoneTitle", "¡Tutorial completado!"),
				NSLOCTEXT("TNTutorial", "DoneText", "Ya sabes todo lo que hace una tortuga. ¡Bienvenida al castillo!"));
		}
	}
}

void UTN_TutorialPlayerComponent::SaveCompleted(bool bSkipped)
{
	UWorld* World = GetWorld();
	UMP_GameInstance* GI = World ? World->GetGameInstance<UMP_GameInstance>() : nullptr;
	if (!GI)
	{
		return;
	}
	// Partida local (#311): el guardado es del jugador 1; lo que haga un invitado dura la partida.
	if (!UTN_LocalPlaySubsystem::ShouldSaveFor(GetPC()))
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Tutorial] %s (invitado de la partida local): sin guardar."), *GetNameSafe(GetPC()));
		return;
	}
	GI->SetTutorialCompleted();
	UE_LOG(LogTortunabo, Log, TEXT("[Tutorial] %s: apuntado en el guardado local (%s)."), *GetNameSafe(GetPC()),
		bSkipped ? TEXT("saltado") : TEXT("terminado por la cascada"));
}

void UTN_TutorialPlayerComponent::HandleInTutorialChanged()
{
	bLocalInTutorial = bInTutorial;
	if (bInTutorial)
	{
		ResetProgress();
		bFalling = false;
		FinalMessageSeconds = 0.f;
		if (ATN_TutorialCourse* Course = GetCourse())
		{
			Course->EnsureBuilt();
		}
		KeyRefreshTimer = 0.f;
		RefreshKeys();
		EnsureWidget();
		RefreshWidget();
		UE_LOG(LogTortunabo, Log, TEXT("[Tutorial] %s entra en el tutorial."), *GetNameSafe(GetPC()));
		return;
	}
	// Fuera: si no es el final (que deja su mensaje unos segundos), el cartel se va ya.
	if (FinalMessageSeconds <= 0.f)
	{
		RemoveWidget();
	}
}

void UTN_TutorialPlayerComponent::ResetProgress()
{
	TaskDone.Init(false, TNTutorial::NumStations * TNTutorial::MaxTasks);
	StationCelebrated.Init(false, TNTutorial::NumStations);
	CurrentStation = INDEX_NONE;
	bHasLastLocation = false;
	bHasLastYaw = false;
	MovedDistance = 0.f;
	TurnedDegrees = 0.f;
	SprintSeconds = 0.f;
	StationSeconds = 0.f;
	ShellDistance = 0.f;
	StruggleSeconds = 0.f;
	ChatWheelHeld = 0.f;
	bSwamThisVisit = false;
	bCatapultBall = false;
	bWasInShell = false;
	bWasCarrying = false;
	bWasCarried = false;
	bWasSwimming = false;
	bDropKeyWasDown = false;
	LastEquipped = NAME_None;
	LastStored = NAME_None;
}

void UTN_TutorialPlayerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	APlayerController* PC = GetPC();
	if (!PC)
	{
		return;
	}
	// Servidor: el recorrido ya no está (otro mapa): fuera del tutorial.
	if (PC->HasAuthority() && bInTutorial && !GetCourse())
	{
		SetInTutorialOnServer(false);
	}
	if (PC->IsLocalController())
	{
		TickLocal(DeltaTime);
	}
}

void UTN_TutorialPlayerComponent::TickLocal(float DeltaTime)
{
	APlayerController* PC = GetPC();
	UWorld* World = GetWorld();
	if (!PC || !World)
	{
		return;
	}
	if (bLocalInTutorial != bInTutorial)
	{
		HandleInTutorialChanged();
	}

	ATN_TutorialCourse* Course = GetCourse();
	ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(PC->GetPawn());

	// Cliente que entra ya metido en el tutorial: bInTutorial puede llegar antes que el actor del recorrido (se
	// replica a 1 Hz) y HandleInTutorialChanged no lo encontró; se construye en cuanto aparece.
	if (bInTutorial && Course && !Course->IsBuilt())
	{
		Course->EnsureBuilt();
	}

	// Al llegar a un lobby (una vez por mapa): si esta máquina no ha hecho el tutorial, se pide.
	if (CheckedWorld.Get() != World)
	{
		PawnSeconds = (Course && Turtle) ? PawnSeconds + DeltaTime : 0.f;
		if (PawnSeconds > 0.4f)
		{
			CheckedWorld = World;
			const UMP_GameInstance* GI = World->GetGameInstance<UMP_GameInstance>();
			// En la partida local (#311) no sale solo: se hace desde el menú de pausa del lobby («Hacer el tutorial»).
			if (GI && !GI->HasCompletedTutorial() && !bInTutorial && !UTN_LocalPlaySubsystem::IsLocalGame(this))
			{
				UE_LOG(LogTortunabo, Log, TEXT("[Tutorial] Primera partida en esta máquina (%s): se pide el tutorial."), *GI->GetTutorialSlotName());
				Course->EnsureBuilt();
				ServerRequestTutorial(false);
			}
		}
	}

	// Las teclas se releen cada segundo (por si se reasignan en Ajustes) y al momento si se cambia de teclado a mando.
	KeyRefreshTimer -= DeltaTime;
	const UTN_InputDeviceSubsystem* Devices = UTN_InputDeviceSubsystem::Get(this);
	const bool bDeviceChanged = Devices && (Devices->IsUsingGamepad(GetPC()) != bGamepad || Devices->IsUsingVR(GetPC()) != bVRKeys
		|| static_cast<uint8>(Devices->GetPadFamily()) != KeyPadFamily);
	if ((KeyRefreshTimer <= 0.f || bDeviceChanged) && (bInTutorial || Widget))
	{
		KeyRefreshTimer = 1.f;
		RefreshKeys();
		RefreshWidget();
	}

	TickFinale(DeltaTime, Turtle);
	if (bInTutorial && Turtle && Course && Course->IsBuilt())
	{
		TickTasks(DeltaTime, Turtle, Course);
	}
}

void UTN_TutorialPlayerComponent::TickFinale(float DeltaTime, ATortugaCharacter* Turtle)
{
	using namespace TNTutorialPlayerDetail;
	APlayerController* PC = GetPC();
	if (bFalling)
	{
		FinalMessageSeconds = FMath::Max(FinalMessageSeconds, 0.5f);
		FallSeconds += DeltaTime;
		const UCharacterMovementComponent* Move = Turtle ? Turtle->GetCharacterMovement() : nullptr;
		const ATN_TutorialCourse* Course = GetCourse();
		const bool bBelow = Turtle && Course && !Course->IsInCourseSpace(Turtle->GetActorLocation());
		const bool bLanded = Move && (Move->IsMovingOnGround() || Move->IsSwimming()) && bBelow;
		if (bLanded || !Turtle || FallSeconds > MaxFallSeconds)
		{
			bFalling = false;
			FallSeconds = 0.f;
			if (bIgnoringMove && PC)
			{
				PC->SetIgnoreMoveInput(false);
			}
			bIgnoringMove = false;
		}
	}
	if (FinalMessageSeconds > 0.f && !bFalling)
	{
		FinalMessageSeconds -= DeltaTime;
		if (FinalMessageSeconds <= 0.f && !bInTutorial)
		{
			RemoveWidget();
		}
	}
}

bool UTN_TutorialPlayerComponent::IsTaskDone(int32 StationIndex, int32 TaskIndex) const
{
	const int32 Idx = StationIndex * TNTutorial::MaxTasks + TaskIndex;
	return TaskDone.IsValidIndex(Idx) && TaskDone[Idx];
}

void UTN_TutorialPlayerComponent::MarkTask(int32 StationIndex, int32 TaskIndex)
{
	const int32 Idx = StationIndex * TNTutorial::MaxTasks + TaskIndex;
	if (!TaskDone.IsValidIndex(Idx) || TaskDone[Idx])
	{
		return;
	}
	TaskDone[Idx] = true;
	const TNTutorial::FStationDef& Def = TNTutorial::Station(StationIndex);
	bool bAll = true;
	for (int32 t = 0; t < Def.NumTasks; ++t)
	{
		bAll &= IsTaskDone(StationIndex, t);
	}
	if (bAll && StationCelebrated.IsValidIndex(StationIndex) && !StationCelebrated[StationIndex])
	{
		StationCelebrated[StationIndex] = true;
		if (Widget)
		{
			Widget->Celebrate(TNTutorialTexts::Cheer(StationIndex * 7 + 3));
		}
		UE_LOG(LogTortunabo, Log, TEXT("[Tutorial] Estación %d aprendida."), StationIndex + 1);
	}
	RefreshWidget();
}

void UTN_TutorialPlayerComponent::EnterStation(int32 NewStation)
{
	CurrentStation = NewStation;
	MovedDistance = 0.f;
	TurnedDegrees = 0.f;
	SprintSeconds = 0.f;
	StationSeconds = 0.f;
	ShellDistance = 0.f;
	StruggleSeconds = 0.f;
	ChatWheelHeld = 0.f;
	bSwamThisVisit = false;
	bCatapultBall = false;
	RefreshWidget();
}

void UTN_TutorialPlayerComponent::TickTasks(float DeltaTime, ATortugaCharacter* Turtle, ATN_TutorialCourse* Course)
{
	using namespace TNTutorialPlayerDetail;
	using namespace TNTutorial;
	APlayerController* PC = GetPC();
	const FVector Location = Turtle->GetActorLocation();
	const FVector Local = Course->WorldToLocal(Location);
	const int32 Here = StationAtX(Local.X);
	if (Here != CurrentStation)
	{
		EnterStation(Here);
	}
	StationSeconds += DeltaTime;

	UCharacterMovementComponent* Move = Turtle->GetCharacterMovement();
	const UTN_ShellComponent* Shell = Turtle->GetShellComponent();
	const UTN_CarryComponent* Carry = Turtle->GetCarryComponent();
	const UTN_InventoryComponent* Inventory = Turtle->GetInventoryComponent();
	const UTN_StaminaComponent* Stamina = Turtle->GetStaminaComponent();
	const bool bInShell = Shell && Shell->IsInShell();
	const bool bSwimming = IsSwimming(Turtle);
	const bool bCarrying = Carry && Carry->IsCarrying();
	const bool bCarried = Carry && Carry->IsBeingCarried();

	// Lo que se ha movido y girado (en el suelo, sin caparazón) y lo que ha rodado en bola.
	const FVector Delta = bHasLastLocation ? Location - LastPawnLocation : FVector::ZeroVector;
	LastPawnLocation = Location;
	bHasLastLocation = true;
	const float Step = static_cast<float>(Delta.Size2D());
	if (Step < 400.f)
	{
		if (bInShell) { ShellDistance += Step; }
		else if (Move && Move->IsMovingOnGround()) { MovedDistance += Step; }
	}
	const float Yaw = PC->GetControlRotation().Yaw;
	if (bHasLastYaw)
	{
		TurnedDegrees += FMath::Abs(FMath::FindDeltaAngleDegrees(LastYaw, Yaw));
	}
	LastYaw = Yaw;
	bHasLastYaw = true;

	// Cascada: cayendo ya pasado el borde, se sigue recto hacia el castillo (sin mover la tortuga en el aire). El servidor
	// hace lo mismo con la misma regla.
	if (!bFalling && Local.X > Dims::LipX + 5.0 && Local.Z > Dims::VoidZ && Move && Move->IsFalling())
	{
		StartLocalFall(Turtle);
		MarkTask(static_cast<int32>(EStation::Waterfall), 0);
	}

	const FName Equipped = Inventory ? ItemIdOf(Inventory->GetEquippedItem()) : NAME_None;
	const FName Stored = Inventory ? ItemIdOf(Inventory->GetStoredItem()) : NAME_None;
	static const FName BallId(TEXT("ThrowableBall"));

	switch (static_cast<EStation>(Here))
	{
		case EStation::Welcome:
			if (MovedDistance >= 300.f) { MarkTask(Here, 0); }
			if (TurnedDegrees >= 90.f) { MarkTask(Here, 1); }
			break;
		case EStation::Sprint:
			if (Stamina && Stamina->IsSprinting())
			{
				SprintSeconds += DeltaTime;
			}
			if (SprintSeconds >= 1.2f) { MarkTask(Here, 0); }
			break;
		case EStation::Jump:
			if (Move && Move->IsFalling() && !bSwimming && Turtle->GetVelocity().Z > 150.0) { MarkTask(Here, 0); }
			break;
		case EStation::BellyDive:
			if (Turtle->IsDiving()) { MarkTask(Here, 0); }
			break;
		case EStation::Pickup:
			if (Inventory && (Inventory->HasEquippedItem() || Inventory->HasStoredItem())) { MarkTask(Here, 0); }
			break;
		case EStation::Search:
			if (Equipped == BallId || Stored == BallId) { MarkTask(Here, 0); }
			break;
		case EStation::Slots:
		{
			// Cambio: lo de la aleta y lo del caparazón se han cruzado.
			if (Equipped != NAME_None && Stored != NAME_None && Equipped != Stored && Equipped == LastStored && Stored == LastEquipped)
			{
				MarkTask(Here, 0);
			}
			// Soltar: la tecla con algo en la aleta.
			const bool bDropDown = IsKeyDown(static_cast<uint8>(EKey::DropItem));
			if (bDropDown && !bDropKeyWasDown && LastEquipped != NAME_None)
			{
				MarkTask(Here, 1);
			}
			bDropKeyWasDown = bDropDown;
			break;
		}
		case EStation::UseItem:
			if (Stamina && Stamina->HasUnlimitedStamina()) { MarkTask(Here, 0); }
			break;
		case EStation::Shell:
			if (bInShell && ShellDistance >= 250.f) { MarkTask(Here, 0); }
			if (IsTaskDone(Here, 0) && bWasInShell && !bInShell) { MarkTask(Here, 1); }
			break;
		case EStation::Trampoline:
		{
			const FVector Jelly = JellyFeet();
			const bool bNearJelly = FVector::Dist2D(Local, Jelly) < 450.0;
			if ((bNearJelly && Turtle->GetVelocity().Z > 800.0) || (Local.X > Dims::TerraceX && Local.Z > Dims::TerraceZ - 40.0))
			{
				MarkTask(Here, 0);
			}
			break;
		}
		case EStation::Catapult:
			// Cruza el cañón en bola (la catapulta lanza como bola de caparazón; por el tronco se va de pie).
			if (bInShell && Local.X > Dims::PartA1 && Local.X < Dims::PartB0)
			{
				bCatapultBall = true;
			}
			if (bCatapultBall && Local.X >= Dims::PartB0) { MarkTask(Here, 0); }
			break;
		case EStation::Swim:
			if (bSwimming)
			{
				bSwamThisVisit = true;
				MarkTask(Here, 0);
			}
			if (bSwamThisVisit && !bSwimming && Local.X > Dims::PoolX1 && Move && Move->IsMovingOnGround()) { MarkTask(Here, 1); }
			break;
		case EStation::Carry:
			if (bCarrying) { MarkTask(Here, 0); }
			if (bWasCarrying && !bCarrying) { MarkTask(Here, 1); }
			break;
		case EStation::Escape:
			if (bCarried)
			{
				MarkTask(Here, 0);
				if (IsKeyDown(static_cast<uint8>(EKey::Move)))
				{
					StruggleSeconds += DeltaTime;
				}
			}
			if (bWasCarried && !bCarried && StruggleSeconds >= 1.f) { MarkTask(Here, 1); }
			if (!bCarried && !bWasCarried) { StruggleSeconds = 0.f; }
			break;
		case EStation::Emotes:
		{
			const int32 Emote = Turtle->GetActiveEmoteIndex();
			if (Emote >= 0 && Emote < ATortugaCharacter::KNOCKDOWN_EMOTE_ID) { MarkTask(Here, 0); }
			if (IsKeyDown(static_cast<uint8>(EKey::ChatWheel)))
			{
				ChatWheelHeld += DeltaTime;
			}
			else
			{
				if (ChatWheelHeld >= 0.25f) { MarkTask(Here, 1); }
				ChatWheelHeld = 0.f;
			}
			break;
		}
		case EStation::Voice:
		{
			const UProximityVoiceComponent* Voice = Turtle->FindComponentByClass<UProximityVoiceComponent>();
			// Sin micrófono también se aprende: basta con leerlo un rato.
			if ((Voice && Voice->bIsSpeaking) || IsKeyDown(static_cast<uint8>(EKey::Talk)) || StationSeconds > 8.f)
			{
				MarkTask(Here, 0);
			}
			break;
		}
		case EStation::PauseMenu:
			if (const UTN_GameSettingsSubsystem* Settings = UTN_GameSettingsSubsystem::Get(this))
			{
				if (Settings->GetPauseMenuOwner() == PC) { MarkTask(Here, 0); }
			}
			break;
		default:
			break;
	}

	bWasInShell = bInShell;
	bWasCarrying = bCarrying;
	bWasCarried = bCarried;
	bWasSwimming = bSwimming;
	LastEquipped = Equipped;
	LastStored = Stored;
}

// ─────────────────────────────────────────────────────────────────────────────
// Teclas y cartel
// ─────────────────────────────────────────────────────────────────────────────

void UTN_TutorialPlayerComponent::RefreshKeys()
{
	using namespace TNTutorialPlayerDetail;
	APlayerController* PC = GetPC();
	bGamepad = PC && UTN_GameSettingsSubsystem::IsUsingGamepad(PC);
	const UTN_InputDeviceSubsystem* Devices = UTN_InputDeviceSubsystem::Get(this);
	bVRKeys = PC && Devices && Devices->IsUsingVR(PC);
	KeyPadFamily = static_cast<uint8>(Devices ? Devices->GetPadFamily() : ETNPadFamily::Xbox);
	KeyPadKeys.Reset();
	const int32 Device = bGamepad ? 1 : 0;
	KeyTexts.Reset();
	KeyKeys.Reset();
	const UTN_GameSettingsSubsystem* Settings = UTN_GameSettingsSubsystem::Get(this);
	TArray<FTNKeyBinding> Rows;
	if (Settings)
	{
		Rows = Settings->GetKeyBindingsFor(PC);
	}
	auto FindRow = [&Rows](const FString& Id) -> const FTNKeyBinding*
	{
		return Rows.FindByPredicate([&Id](const FTNKeyBinding& Row) { return Row.Id == Id; });
	};
	auto KeyOf = [](const FTNKeyBinding* Row, int32 Dev) -> FKey
	{
		if (!Row) { return FKey(); }
		return Row->Keys[Dev].IsValid() ? Row->Keys[Dev] : Row->FixedKeys[Dev];
	};

	for (uint8 K = static_cast<uint8>(EKey::Move); K <= static_cast<uint8>(EKey::Pause); ++K)
	{
		const EKey Key = static_cast<EKey>(K);
		FText Label = FallbackLabel(Key, bGamepad);
		FKey PadKey;
		TArray<FKey> Keys;
		if (bVRKeys)
		{
			// Con gafas (#644): el botón de los mandos Touch de cada acción, con su nombre («Gatillo derecho», «A»...). Sin dibujo
			// de mando (PadKey vacío): sale el nombre. Los sticks y el clic de hablar no se ven como "pulsado" (ejes).
			const TCHAR* ActionId = Key == EKey::Move ? TEXT("IA_Move") : (Key == EKey::Look ? TEXT("IA_Look") : RowIdOf(Key));
			const FKey VRKey = ActionId ? TNVRControls::KeyForAction(ActionId) : FKey();
			if (VRKey.IsValid())
			{
				Label = UTN_GameSettingsSubsystem::KeyDisplayName(VRKey);
				Keys.Add(VRKey);
			}
			KeyTexts.Add(K, Label);
			KeyKeys.Add(K, MoveTemp(Keys));
			KeyPadKeys.Add(K, FKey());
			continue;
		}
		if (Settings)
		{
			if (Key == EKey::Move)
			{
				// Avanzar, izquierda, retroceder, derecha (W A S D de serie); con el mando, el stick.
				static const TCHAR* Dirs[4] = { TEXT("IA_Move:Y+"), TEXT("IA_Move:X-"), TEXT("IA_Move:Y-"), TEXT("IA_Move:X+") };
				TArray<FString> Parts;
				FKey Stick;
				for (const TCHAR* Dir : Dirs)
				{
					const FTNKeyBinding* Row = FindRow(Dir);
					const FKey Keyboard = KeyOf(Row, 0);
					if (Keyboard.IsValid())
					{
						Keys.Add(Keyboard);
						Parts.Add(UTN_GameSettingsSubsystem::KeyDisplayName(Keyboard).ToString());
					}
					if (!Stick.IsValid()) { Stick = KeyOf(Row, 1); }
				}
				if (bGamepad && Stick.IsValid())
				{
					Label = UTN_GameSettingsSubsystem::KeyDisplayName(Stick);
					PadKey = Stick;
				}
				else if (!bGamepad && Parts.Num() == 4)
				{
					Label = FText::FromString(FString::Join(Parts, TEXT(" ")));
				}
			}
			else if (Key == EKey::Look)
			{
				for (const FTNKeyBinding& Fixed : Settings->GetFixedControls())
				{
					if (Fixed.Id == TEXT("IA_Look") && Fixed.FixedKeys[Device].IsValid())
					{
						Label = UTN_GameSettingsSubsystem::KeyDisplayName(Fixed.FixedKeys[Device]);
						PadKey = bGamepad ? Fixed.FixedKeys[Device] : FKey();
					}
				}
			}
			else if (const TCHAR* RowId = RowIdOf(Key))
			{
				const FTNKeyBinding* Row = FindRow(RowId);
				for (int32 Dev = 0; Dev < 2; ++Dev)
				{
					const FKey Bound = KeyOf(Row, Dev);
					if (Bound.IsValid()) { Keys.Add(Bound); }
				}
				const FKey Shown = KeyOf(Row, Device);
				if (Row)
				{
					PadKey = bGamepad ? Shown : FKey();
					if (Key == EKey::Pause && !bGamepad && GIsEditor && Shown == EKeys::Escape)
					{
						// En el editor Escape corta la partida: el menú va con el Tabulador.
						Label = UTN_GameSettingsSubsystem::KeyDisplayName(EKeys::Tab);
					}
					else
					{
						Label = UTN_GameSettingsSubsystem::KeyDisplayName(Shown);
					}
				}
			}
		}
		KeyTexts.Add(K, Label);
		KeyKeys.Add(K, MoveTemp(Keys));
		KeyPadKeys.Add(K, PadKey);
	}
}

bool UTN_TutorialPlayerComponent::IsKeyDown(uint8 Key) const
{
	const APlayerController* PC = GetPC();
	if (!PC)
	{
		return false;
	}
	if (Key == static_cast<uint8>(TNTutorial::EKey::Move))
	{
		if (FMath::Abs(PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX)) > 0.3f || FMath::Abs(PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftY)) > 0.3f)
		{
			return true;
		}
	}
	if (const TArray<FKey>* Keys = KeyKeys.Find(Key))
	{
		for (const FKey& K : *Keys)
		{
			if (K.IsValid() && !K.IsAxis1D() && !K.IsAxis2D() && PC->IsInputKeyDown(K))
			{
				return true;
			}
		}
	}
	return false;
}

void UTN_TutorialPlayerComponent::RefreshWidget()
{
	using namespace TNTutorial;
	if (!Widget || !bInTutorial)
	{
		return;
	}
	const int32 Here = CurrentStation == INDEX_NONE ? 0 : CurrentStation;
	const FStationDef& Def = TNTutorial::Station(Here);
	FTNTutorialView View;
	View.StationIndex = Here;
	View.NumStations = NumStations;
	View.Title = TNTutorialTexts::Title(Def.Id);
	View.Tip = TNTutorialTexts::Tip(Def.Id);
	for (int32 t = 0; t < Def.NumTasks; ++t)
	{
		FTNTutorialTaskView& Task = View.Tasks.AddDefaulted_GetRef();
		Task.Text = TNTutorialTexts::Task(Def.Id, t);
		const FText* KeyText = KeyTexts.Find(static_cast<uint8>(Def.Keys[t]));
		Task.Key = (Def.Keys[t] != EKey::None && KeyText) ? *KeyText : FText::GetEmpty();
		Task.PadKey = Def.Keys[t] != EKey::None ? KeyPadKeys.FindRef(static_cast<uint8>(Def.Keys[t])) : FKey();
		Task.PadFamily = static_cast<ETNPadFamily>(KeyPadFamily);
		Task.bDone = IsTaskDone(Here, t);
	}
	const FText* PauseText = KeyTexts.Find(static_cast<uint8>(EKey::Pause));
	View.SkipHint = FText::Format(NSLOCTEXT("TNTutorial", "SkipHint", "¿Ya sabes jugar? {0} > «Saltar el tutorial»."),
		PauseText ? *PauseText : UTN_GameSettingsSubsystem::KeyDisplayName(EKeys::Escape));
	Widget->SetView(View);
}

void UTN_TutorialPlayerComponent::EnsureWidget()
{
	using namespace TNTutorialPlayerDetail;
	APlayerController* PC = GetPC();
	if (Widget || !PC || !PC->IsLocalController())
	{
		return;
	}
	Widget = CreateWidget<UTN_TutorialWidget>(PC, UTN_TutorialWidget::StaticClass());
	if (Widget)
	{
		TNVR::AddToScreen(Widget, WidgetZOrder);
	}
}

void UTN_TutorialPlayerComponent::RemoveWidget()
{
	if (Widget)
	{
		Widget->RemoveFromParent();
		Widget = nullptr;
	}
}
