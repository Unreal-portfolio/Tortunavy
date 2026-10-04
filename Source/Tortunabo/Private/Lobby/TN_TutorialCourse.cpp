// ─────────────────────────────────────────────────────────────────────────────
// ATN_TutorialCourse — el tutorial de la primera partida: quién está dentro, los puntos de control, la caída de las islas,
// la cascada del final, las piezas de práctica (servidor) y cuándo se ve (cada máquina). La malla, la vegetación, el agua
// y la fauna, en TN_TutorialCourse_Build.cpp; el recorrido, en TN_TutorialLayout.h. Docs/Tutorial.md.
// ─────────────────────────────────────────────────────────────────────────────

#include "Lobby/TN_TutorialCourse.h"
#include "Lobby/TN_TutorialFauna.h"
#include "Lobby/TN_TutorialPlayerComponent.h"
#include "Lobby/TN_TutorialPractice.h"
#include "Lobby/Playground/TN_JellyfishTrampoline.h"
#include "Lobby/TN_TutorialRules.h"
#include "TN_TutorialLayout.h"
#include "Core/TN_InventoryTypes.h"
#include "Core/TN_Log.h"
#include "Player/TortugaCharacter.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_ShellComponent.h"
#include "World/ProcMap/TN_ProcWaterActors.h"
#include "World/TN_PickupInteractableBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"

namespace TNTutorialCourseDetail
{
	/** Distancia (cm) hasta la que un cliente recibe las piezas de práctica: los del lobby (180 m más abajo) no las tienen. */
	constexpr float PracticeNetCull = 9000.f;
	/** Sin nadie en el tutorial durante esto (s), se quitan las piezas de práctica. */
	constexpr float EmptyDespawnSeconds = 20.f;
	/** La barrita vuelve a salir a los tantos segundos de cogerla. */
	constexpr double BarRespawnSeconds = 3.0;
	/** El recorrido se ve con la cámara por encima de esta altura sobre el suelo del lobby (y cerca de las islas). */
	constexpr double VisibleAboveLobby = 2500.0;
	/** Catálogo de objetos de siempre. */
	const TCHAR* const ItemsTable = TEXT("/Game/Blueprints/Gameplay/Items/DT_Items.DT_Items");
}

ATN_TutorialCourse::ATN_TutorialCourse()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	// Replicado sin propiedades que cambien: los clientes lo tienen (y su transformada) para montar el recorrido igual.
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	SetNetUpdateFrequency(1.f);
	SetCanBeDamaged(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
}

ATN_TutorialCourse* ATN_TutorialCourse::Find(const UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ATN_TutorialCourse> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			return *It;
		}
	}
	return nullptr;
}

ATN_TutorialCourse* ATN_TutorialCourse::SpawnAbove(UWorld* World, const FVector& Landing, float Yaw)
{
	if (!World)
	{
		return nullptr;
	}
	using namespace TNTutorial;
	const FRotator Rotation(0.f, Yaw, 0.f);
	// La cascada (el borde del pasillo, Z local 0) justo encima del sitio de aterrizaje.
	const FVector Lip(Dims::LipX, CorridorCenter(Dims::LipX), 0.0);
	const FVector Location = Landing + FVector(0.0, 0.0, Dims::Height) - Rotation.RotateVector(Lip);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATN_TutorialCourse* Course = World->SpawnActor<ATN_TutorialCourse>(ATN_TutorialCourse::StaticClass(), Location, Rotation, Params);
	if (Course)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Tutorial] Recorrido colocado a %.0f m sobre %s (la cascada cae ahí)."), Dims::Height / 100.0, *Landing.ToString());
	}
	return Course;
}

void ATN_TutorialCourse::BeginPlay()
{
	Super::BeginPlay();
}

void ATN_TutorialCourse::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		DespawnPractice();
	}
	if (PoolVolume)
	{
		PoolVolume->Destroy();
		PoolVolume = nullptr;
	}
	if (Fauna)
	{
		Fauna->Destroy();
		Fauna = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

// ─────────────────────────────────────────────────────────────────────────────
// Espacio del recorrido
// ─────────────────────────────────────────────────────────────────────────────

FVector ATN_TutorialCourse::LocalToWorld(const FVector& Local) const
{
	return GetActorTransform().TransformPosition(Local);
}

FVector ATN_TutorialCourse::WorldToLocal(const FVector& WorldPoint) const
{
	return GetActorTransform().InverseTransformPosition(WorldPoint);
}

float ATN_TutorialCourse::LocalYawToWorld(float LocalYaw) const
{
	return FRotator::NormalizeAxis(GetActorRotation().Yaw + LocalYaw);
}

int32 ATN_TutorialCourse::StationAt(const FVector& WorldPoint) const
{
	return TNTutorial::StationAtX(WorldToLocal(WorldPoint).X);
}

bool ATN_TutorialCourse::IsInCourseSpace(const FVector& WorldPoint) const
{
	using namespace TNTutorial;
	const FVector Local = WorldToLocal(WorldPoint);
	return Local.Z > -(Dims::Height - TNTutorialCourseDetail::VisibleAboveLobby) && Local.X > Dims::PartA0 - 6000.0
		&& Local.X < Dims::PartB1 + 6000.0 && FMath::Abs(Local.Y) < 8000.0;
}

FVector ATN_TutorialCourse::GetLandingSpot() const
{
	using namespace TNTutorial;
	return LocalToWorld(FVector(Dims::LipX, CorridorCenter(Dims::LipX), -Dims::Height));
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick
// ─────────────────────────────────────────────────────────────────────────────

void ATN_TutorialCourse::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		ServerTick(DeltaSeconds);
	}
	if (bBuilt)
	{
		VisibilityTimer -= DeltaSeconds;
		if (VisibilityTimer <= 0.f)
		{
			VisibilityTimer = 0.25f;
			UpdateLocalVisibility();
		}
	}
}

void ATN_TutorialCourse::UpdateLocalVisibility()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// Se ve con la cámara arriba (quien hace el tutorial o cae por la cascada); desde el lobby, no.
	bool bShow = false;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalController())
		{
			continue;
		}
		if (PC->PlayerCameraManager && IsInCourseSpace(PC->PlayerCameraManager->GetCameraLocation()))
		{
			bShow = true;
		}
		if (const UTN_TutorialPlayerComponent* Comp = UTN_TutorialPlayerComponent::FindFor(PC))
		{
			bShow |= Comp->IsInTutorial();
		}
	}
	SetLocalVisible(bShow);
}

void ATN_TutorialCourse::SetLocalVisible(bool bVisible)
{
	if (bLocalVisible == bVisible)
	{
		return;
	}
	bLocalVisible = bVisible;
	if (SceneRoot)
	{
		SceneRoot->SetVisibility(bVisible, true);
	}
	if (Fauna)
	{
		Fauna->SetFaunaVisible(bVisible);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor: participantes
// ─────────────────────────────────────────────────────────────────────────────

ATN_TutorialCourse::FParticipant* ATN_TutorialCourse::FindParticipant(const APlayerController* PC)
{
	return Participants.FindByPredicate([PC](const FParticipant& P) { return P.PC.Get() == PC; });
}

bool ATN_TutorialCourse::IsParticipant(const APlayerController* PC) const
{
	return PC && Participants.ContainsByPredicate([PC](const FParticipant& P) { return P.PC.Get() == PC; });
}

int32 ATN_TutorialCourse::GetReachedStation(const APlayerController* PC) const
{
	const FParticipant* P = PC ? Participants.FindByPredicate([PC](const FParticipant& Each) { return Each.PC.Get() == PC; }) : nullptr;
	return P ? TNTutorialRules::StationOfCheckpoint(P->Checkpoint, static_cast<int32>(TNTutorial::EStation::Catapult)) : INDEX_NONE;
}

int32 ATN_TutorialCourse::FreeSlot() const
{
	for (int32 Slot = 0; Slot < 8; ++Slot)
	{
		if (!Participants.ContainsByPredicate([Slot](const FParticipant& P) { return P.Slot == Slot; }))
		{
			return Slot;
		}
	}
	return Participants.Num() % 8;
}

void ATN_TutorialCourse::StartFor(APlayerController* PC)
{
	using namespace TNTutorial;
	if (!HasAuthority() || !PC)
	{
		return;
	}
	EnsureBuilt();
	FParticipant* P = FindParticipant(PC);
	// Repetir estando dentro («Repetir el tutorial», TN.Tutorial.Start): bInTutorial ya es true y el cliente no se entera solo.
	const bool bWasInside = P != nullptr;
	if (!P)
	{
		FParticipant New;
		New.PC = PC;
		New.Slot = FreeSlot();
		Participants.Add(New);
		P = &Participants.Last();
		UE_LOG(LogTortunabo, Log, TEXT("[Tutorial] %s empieza el tutorial (sitio %d; %d dentro)."), *GetNameSafe(PC), P->Slot, Participants.Num());
	}
	P->Checkpoint = 0;
	if (APawn* Pawn = PC->GetPawn())
	{
		PlacePawn(Pawn, LocalToWorld(SpawnFeet(P->Slot)), LocalYawToWorld(0.f), PC);
	}
	if (UTN_TutorialPlayerComponent* Comp = UTN_TutorialPlayerComponent::EnsureFor(PC))
	{
		Comp->SetInTutorialOnServer(true);
		if (TNTutorialRules::ShouldResetProgress(bWasInside, 0))
		{
			Comp->ClientResetProgress();
		}
	}
	EnsurePractice();
}

void ATN_TutorialCourse::FinishFor(APlayerController* PC, bool bSkipped)
{
	if (!HasAuthority() || !PC)
	{
		return;
	}
	const int32 Index = Participants.IndexOfByPredicate([PC](const FParticipant& P) { return P.PC.Get() == PC; });
	const int32 Slot = Index != INDEX_NONE ? Participants[Index].Slot : 0;
	if (Index != INDEX_NONE)
	{
		Participants.RemoveAt(Index);
	}
	if (bSkipped)
	{
		// De vuelta al lobby, donde aterriza la cascada (en el suelo de verdad, algo separados si son varios).
		if (APawn* Pawn = PC->GetPawn())
		{
			FVector Landing = GetLandingSpot() + FRotator(0.f, GetActorRotation().Yaw, 0.f).RotateVector(FVector(0.0, ((Slot % 4) - 1.5) * 170.0, 0.0));
			FHitResult Hit;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(TN_TutorialLanding), false, Pawn);
			if (GetWorld()->LineTraceSingleByChannel(Hit, Landing + FVector(0.0, 0.0, 1500.0), Landing - FVector(0.0, 0.0, 1500.0), ECC_WorldStatic, Query))
			{
				Landing = Hit.ImpactPoint;
			}
			PlacePawn(Pawn, Landing, LocalYawToWorld(0.f), PC);
		}
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Tutorial] %s %s el tutorial (%d dentro)."), *GetNameSafe(PC), bSkipped ? TEXT("salta") : TEXT("termina"),
		Participants.Num());
	if (UTN_TutorialPlayerComponent* Comp = UTN_TutorialPlayerComponent::EnsureFor(PC))
	{
		// Primero el aviso (deja el mensaje final en pantalla) y luego fuera del tutorial.
		Comp->ClientTutorialFinished(bSkipped);
		Comp->SetInTutorialOnServer(false);
	}
}

void ATN_TutorialCourse::GoToStation(APlayerController* PC, int32 StationIndex)
{
	using namespace TNTutorial;
	if (!HasAuthority() || !PC)
	{
		return;
	}
	const bool bWasInside = IsParticipant(PC);
	if (!bWasInside)
	{
		StartFor(PC);
	}
	FParticipant* P = FindParticipant(PC);
	if (!P)
	{
		return;
	}
	const int32 Target = FMath::Clamp(StationIndex, 0, NumStations - 1);
	// Los puntos de control llevan uno más pasado el cañón (TNTutorial::CheckpointX).
	P->Checkpoint = Target <= static_cast<int32>(EStation::Catapult) ? Target : Target + 1;
	if (APawn* Pawn = PC->GetPawn())
	{
		const FVector Feet = CheckpointFeet(P->Checkpoint) + FVector(0.0, ((P->Slot % 4) - 1.5) * 120.0, 0.0);
		PlacePawn(Pawn, LocalToWorld(FVector(Feet.X, Feet.Y, CorridorFloorAt(Feet.X, Feet.Y - CorridorCenter(Feet.X)))), LocalYawToWorld(0.f), PC);
	}
	// Volver a la salida estando dentro es repetir el tutorial: el HUD del jugador empieza de cero. Quien acaba de entrar ya lo hace solo.
	if (TNTutorialRules::ShouldResetProgress(bWasInside, Target))
	{
		if (UTN_TutorialPlayerComponent* Comp = UTN_TutorialPlayerComponent::FindFor(PC))
		{
			Comp->ClientResetProgress();
		}
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Tutorial] %s va a la estación %d."), *GetNameSafe(PC), Target + 1);
}

void ATN_TutorialCourse::NotifyDummyHit(APawn* Thrower)
{
	if (!HasAuthority() || !Thrower)
	{
		return;
	}
	for (const FParticipant& P : Participants)
	{
		APlayerController* PC = P.PC.Get();
		if (PC && PC->GetPawn() == Thrower)
		{
			if (UTN_TutorialPlayerComponent* Comp = UTN_TutorialPlayerComponent::FindFor(PC))
			{
				Comp->ClientTaskDone(static_cast<uint8>(TNTutorial::EStation::Throw), 0);
			}
			UE_LOG(LogTortunabo, Log, TEXT("[Tutorial] %s le ha dado al cangrejo de prácticas."), *GetNameSafe(PC));
			return;
		}
	}
}

void ATN_TutorialCourse::PlacePawn(APawn* Pawn, const FVector& WorldFeet, float WorldYaw, APlayerController* PC)
{
	if (!Pawn)
	{
		return;
	}
	if (ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Pawn))
	{
		if (UTN_CarryComponent* Carry = Turtle->GetCarryComponent())
		{
			if (Carry->IsCarrying())
			{
				Carry->ForceRelease(false);
			}
			if (ATortugaCharacter* Carrier = Carry->GetCarrier())
			{
				if (UTN_CarryComponent* CarrierCarry = Carrier->GetCarryComponent())
				{
					CarrierCarry->ForceRelease(false);
				}
			}
		}
		if (Turtle->IsKnockedDown())
		{
			Turtle->RecoverFromKnockdown();
		}
		if (UTN_ShellComponent* Shell = Turtle->GetShellComponent())
		{
			if (Shell->IsInShell())
			{
				Shell->SetExitLocked(false);
				Shell->ForceExitShell();
			}
		}
		// Aparecer un palmo por encima no la hace bola ni la rompe.
		Turtle->SetFallImmuneUntilLanded();
	}
	const float HalfHeight = Pawn->GetSimpleCollisionHalfHeight();
	const FVector Target = WorldFeet + FVector(0.0, 0.0, HalfHeight + 10.0);
	Pawn->SetActorLocationAndRotation(Target, FRotator(0.f, WorldYaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	if (ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		if (UCharacterMovementComponent* Move = Character->GetCharacterMovement())
		{
			// Por MOVE_None: si ya estaba cayendo, MOVE_Falling solo no reinicia la caída (contaría desde el sitio viejo).
			Move->StopMovementImmediately();
			Move->SetMovementMode(MOVE_None);
			Move->SetMovementMode(MOVE_Falling);
		}
	}
	if (PC)
	{
		PC->ClientSetRotation(FRotator(-8.f, WorldYaw, 0.f), true);
	}
	Pawn->ForceNetUpdate();
}

void ATN_TutorialCourse::ServerTick(float DeltaSeconds)
{
	ServerTimer += DeltaSeconds;
	if (ServerTimer < 0.1f)
	{
		return;
	}
	const float Step = ServerTimer;
	ServerTimer = 0.f;
	TickParticipants();
	if (Participants.Num() > 0)
	{
		EmptySeconds = 0.f;
		EnsurePractice();
		TickPractice(Step);
		return;
	}
	EmptySeconds += Step;
	if (EmptySeconds > TNTutorialCourseDetail::EmptyDespawnSeconds)
	{
		DespawnPractice();
	}
}

void ATN_TutorialCourse::TickParticipants()
{
	using namespace TNTutorial;
	for (int32 i = Participants.Num() - 1; i >= 0; --i)
	{
		if (!Participants.IsValidIndex(i))
		{
			continue;
		}
		APlayerController* PC = Participants[i].PC.Get();
		if (!PC)
		{
			Participants.RemoveAt(i);
			continue;
		}
		ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(PC->GetPawn());
		if (!Turtle)
		{
			continue;
		}
		const FVector Local = WorldToLocal(Turtle->GetActorLocation());
		UCharacterMovementComponent* Move = Turtle->GetCharacterMovement();

		// La cascada: pasado el borde y cayendo, termina. Cae recta y sin daño hasta la plaza (el cliente hace lo mismo).
		if (Local.X > Dims::LipX + 5.0 && Local.Z > Dims::VoidZ && Move && Move->IsFalling())
		{
			Turtle->SetFallImmuneUntilLanded();
			Turtle->LaunchCharacter(FVector(0.0, 0.0, FMath::Min(0.0, Turtle->GetVelocity().Z)), true, true);
			FinishFor(PC, false);
			continue;
		}

		FParticipant& P = Participants[i];
		// Todavía en el lobby (la tortuga ha salido más tarde o ha vuelto a aparecer): a su punto de control.
		if (!IsInCourseSpace(Turtle->GetActorLocation()))
		{
			PlacePawn(Turtle, LocalToWorld(P.Checkpoint == 0 ? SpawnFeet(P.Slot) : CheckpointFeet(P.Checkpoint)), LocalYawToWorld(0.f), PC);
			continue;
		}
		// Punto de control: el más avanzado pisado.
		if (Move && Move->IsMovingOnGround() && PartOf(Local.X) != INDEX_NONE)
		{
			P.Checkpoint = FMath::Max(P.Checkpoint, CheckpointAtOrBefore(Local.X));
		}
		// Se ha caído de las islas: vuelve al último punto de control.
		if (Local.Z < Dims::VoidZ)
		{
			UE_LOG(LogTortunabo, Log, TEXT("[Tutorial] %s se ha caído de las islas: vuelve al punto de control %d."), *GetNameSafe(PC), P.Checkpoint);
			PlacePawn(Turtle, LocalToWorld(CheckpointFeet(P.Checkpoint)), LocalYawToWorld(0.f), PC);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor: piezas de práctica
// ─────────────────────────────────────────────────────────────────────────────

AActor* ATN_TutorialCourse::SpawnItemPickup(FName RowName, const FVector& WorldFeet)
{
	UWorld* World = GetWorld();
	const UDataTable* Table = LoadObject<UDataTable>(nullptr, TNTutorialCourseDetail::ItemsTable);
	if (!World || !Table)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Tutorial] Sin el catálogo de objetos (%s): no sale la barrita."), TNTutorialCourseDetail::ItemsTable);
		return nullptr;
	}
	const FTN_InventoryItem* Row = Table->FindRow<FTN_InventoryItem>(RowName, TEXT("Tutorial"), false);
	if (!Row || !Row->PickupActorClass)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Tutorial] El catálogo no tiene la fila %s con su objeto del suelo."), *RowName.ToString());
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = this;
	ATN_PickupInteractableBase* Pickup = World->SpawnActor<ATN_PickupInteractableBase>(Row->PickupActorClass, WorldFeet + FVector(0.0, 0.0, 5.0),
		FRotator(0.f, LocalYawToWorld(90.f), 0.f), Params);
	if (Pickup)
	{
		Pickup->InitializeFromInventoryItem(*Row);
		Pickup->SetNetCullDistanceSquared(FMath::Square(TNTutorialCourseDetail::PracticeNetCull));
	}
	return Pickup;
}

ATortugaCharacter* ATN_TutorialCourse::SpawnPracticeTurtle(const FVector& LocalFeet, float LocalYaw, const TCHAR* Label)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	// La misma tortuga que la de los jugadores (la clase del lobby, con su malla), sin mando: la gobierna el recorrido.
	UClass* PawnClass = ATortugaCharacter::StaticClass();
	if (const AGameModeBase* GameMode = World->GetAuthGameMode())
	{
		if (GameMode->DefaultPawnClass && GameMode->DefaultPawnClass->IsChildOf(ATortugaCharacter::StaticClass()))
		{
			PawnClass = GameMode->DefaultPawnClass;
		}
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	Params.Owner = this;
	const FVector Location = LocalToWorld(LocalFeet) + FVector(0.0, 0.0, 100.0);
	ATortugaCharacter* Turtle = World->SpawnActor<ATortugaCharacter>(PawnClass, Location, FRotator(0.f, LocalYawToWorld(LocalYaw), 0.f), Params);
	if (!Turtle)
	{
		return nullptr;
	}
	// Solo la ven los que están cerca (las de los jugadores son relevantes para todos).
	Turtle->bAlwaysRelevant = false;
	Turtle->SetNetCullDistanceSquared(FMath::Square(TNTutorialCourseDetail::PracticeNetCull));
	Turtle->Tags.Add(TEXT("TN_TutorialPractice"));
	if (UCharacterMovementComponent* Move = Turtle->GetCharacterMovement())
	{
		// Sin mando el movimiento no corre en el servidor: sin esto no caería ni se asentaría.
		Move->bRunPhysicsWithNoController = true;
		Move->SetMovementMode(MOVE_Falling);
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Tutorial] Tortuga de práctica «%s» en su sitio."), Label);
	return Turtle;
}

void ATN_TutorialCourse::EnsurePractice()
{
	using namespace TNTutorial;
	using namespace TNTutorialCourseDetail;
	UWorld* World = GetWorld();
	if (!HasAuthority() || !World || !bBuilt)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = this;

	if (!Mound.IsValid())
	{
		ATN_TutorialSearchSpot* Spot = World->SpawnActor<ATN_TutorialSearchSpot>(ATN_TutorialSearchSpot::StaticClass(), LocalToWorld(MoundFeet()),
			FRotator(0.f, LocalYawToWorld(0.f), 0.f), Params);
		if (Spot)
		{
			Spot->SetupSpot(static_cast<float>(Dims::MoundRadius) + 10.f, 0.f, 110.f, FLinearColor(0.78f, 0.66f, 0.45f));
			Spot->SetNetCullDistanceSquared(FMath::Square(PracticeNetCull));
			Mound = Spot;
		}
	}
	if (!Dummy.IsValid())
	{
		// Mirando hacia quien llega (-X local); va de lado a lado por su Y.
		ATN_TutorialDummy* Crab = World->SpawnActor<ATN_TutorialDummy>(ATN_TutorialDummy::StaticClass(), LocalToWorld(DummyFeet()),
			FRotator(0.f, LocalYawToWorld(180.f), 0.f), Params);
		if (Crab)
		{
			Crab->SetCourse(this);
			Crab->SetNetCullDistanceSquared(FMath::Square(PracticeNetCull));
			Dummy = Crab;
		}
	}
	if (!Jelly.IsValid())
	{
		const FTransform Xf(FRotator(0.f, LocalYawToWorld(180.f), 0.f), LocalToWorld(JellyFeet()));
		ATN_JellyfishTrampoline* Jellyfish = World->SpawnActorDeferred<ATN_JellyfishTrampoline>(ATN_JellyfishTrampoline::StaticClass(), Xf, this,
			nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Jellyfish)
		{
			// Grande y con fuerza: sube 9 m y deja en la terraza a quien llega andando.
			Jellyfish->ColorPreset = ETNJellyfishColor::Lilac;
			Jellyfish->Size = 1.35f;
			Jellyfish->LaunchZ = 1350.f;
			Jellyfish->SpotSeed = 7;
			Jellyfish->SetNetCullDistanceSquared(FMath::Square(PracticeNetCull));
			Jellyfish->FinishSpawning(Xf);
			Jelly = Jellyfish;
		}
	}
	if (!Catapult.IsValid())
	{
		const FTransform Xf(FRotator(0.f, LocalYawToWorld(0.f), 0.f), LocalToWorld(CatapultFeet()));
		ATN_TutorialCatapult* Launcher = World->SpawnActorDeferred<ATN_TutorialCatapult>(ATN_TutorialCatapult::StaticClass(), Xf, this,
			nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Launcher)
		{
			Launcher->SetupTutorialSpec(5, 0.75f);
			Launcher->FinishSpawning(Xf);
			Catapult = Launcher;
		}
	}
	if (!Rodolfo.IsValid())
	{
		Rodolfo = SpawnPracticeTurtle(RodolfoFeet(), 200.f, TEXT("Rodolfo"));
		RodolfoStandSeconds = 0.f;
	}
	if (!Berta.IsValid())
	{
		Berta = SpawnPracticeTurtle(BertaFeet(), 180.f, TEXT("Berta"));
		BertaCarrySeconds = 0.f;
		BertaRestUntil = 0.0;
	}
	if (!BarPickup.IsValid() && BarRespawnAt < 0.0)
	{
		// La primera vez, en el acto; luego, un rato después de cogerla (TickPractice).
		BarPickup = SpawnItemPickup(TEXT("StaminaBoost"), LocalToWorld(BarFeet()));
	}
}

void ATN_TutorialCourse::DespawnPractice()
{
	TArray<AActor*> ToDestroy = { BarPickup.Get(), Mound.Get(), Dummy.Get(), Jelly.Get(), Catapult.Get(), Rodolfo.Get(), Berta.Get() };
	for (AActor* Actor : ToDestroy)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
	BarPickup.Reset();
	Mound.Reset();
	Dummy.Reset();
	Jelly.Reset();
	Catapult.Reset();
	Rodolfo.Reset();
	Berta.Reset();
	BarRespawnAt = -1.0;
}

void ATN_TutorialCourse::TickPractice(float DeltaSeconds)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	// La barrita: cuando alguien la coge, sale otra al rato para el siguiente.
	if (!BarPickup.IsValid())
	{
		if (BarRespawnAt < 0.0)
		{
			BarRespawnAt = Now + TNTutorialCourseDetail::BarRespawnSeconds;
		}
		else if (Now >= BarRespawnAt)
		{
			BarPickup = SpawnItemPickup(TEXT("StaminaBoost"), LocalToWorld(TNTutorial::BarFeet()));
			BarRespawnAt = -1.0;
		}
	}
	TickRodolfo(DeltaSeconds);
	TickBerta(DeltaSeconds);
}

void ATN_TutorialCourse::TickRodolfo(float DeltaSeconds)
{
	using namespace TNTutorial;
	ATortugaCharacter* Turtle = Rodolfo.Get();
	if (!Turtle)
	{
		return;
	}
	const FVector Home = LocalToWorld(RodolfoFeet());
	UTN_ShellComponent* Shell = Turtle->GetShellComponent();
	const UTN_CarryComponent* Carry = Turtle->GetCarryComponent();
	const bool bCarried = Carry && Carry->IsBeingCarried();
	const FVector Local = WorldToLocal(Turtle->GetActorLocation());
	// Tirado por el borde o muy lejos: a su sitio.
	if (Local.Z < Dims::VoidZ || (!bCarried && FVector::Dist2D(Turtle->GetActorLocation(), Home) > 1800.0))
	{
		PlacePawn(Turtle, Home, LocalYawToWorld(200.f), nullptr);
		RodolfoStandSeconds = 0.f;
		return;
	}
	if (bCarried || (Shell && Shell->IsInShell()))
	{
		RodolfoStandSeconds = 0.f;
		return;
	}
	// De pie (acaba de aparecer o de salir de la bola tras un lanzamiento): al rato vuelve a su caparazón para que lo cojan.
	RodolfoStandSeconds += DeltaSeconds;
	if (RodolfoStandSeconds > 1.2f && Shell)
	{
		if (FVector::Dist2D(Turtle->GetActorLocation(), Home) > 350.0)
		{
			PlacePawn(Turtle, Home, LocalYawToWorld(200.f), nullptr);
		}
		Shell->ForceEnterShell(true, false);
		RodolfoStandSeconds = 0.f;
	}
}

void ATN_TutorialCourse::TickBerta(float DeltaSeconds)
{
	using namespace TNTutorial;
	ATortugaCharacter* Turtle = Berta.Get();
	UWorld* World = GetWorld();
	if (!Turtle || !World)
	{
		return;
	}
	UTN_CarryComponent* Carry = Turtle->GetCarryComponent();
	if (!Carry)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	const FVector Home = LocalToWorld(BertaFeet());
	if (Carry->IsCarrying())
	{
		// Si no se suelta forcejeando, la deja en el suelo a los 9 s (y vuelve a cogerla si se mete otra vez en la bola).
		BertaCarrySeconds += DeltaSeconds;
		if (BertaCarrySeconds > 9.f)
		{
			Carry->RequestDrop();
			BertaCarrySeconds = 0.f;
			BertaRestUntil = Now + 3.0;
		}
		return;
	}
	if (BertaCarrySeconds > 0.f)
	{
		// Se ha soltado: un respiro antes de volver a coger.
		BertaCarrySeconds = 0.f;
		BertaRestUntil = Now + 3.0;
	}
	if (WorldToLocal(Turtle->GetActorLocation()).Z < Dims::VoidZ || FVector::Dist2D(Turtle->GetActorLocation(), Home) > 250.0)
	{
		PlacePawn(Turtle, Home, LocalYawToWorld(180.f), nullptr);
		return;
	}
	if (Now < BertaRestUntil || Turtle->IsInShell() || Turtle->IsKnockedDown())
	{
		return;
	}
	// Coge a quien esté metido en su caparazón a menos de 2,4 m (solo los que hacen el tutorial).
	ATortugaCharacter* Target = nullptr;
	double Best = 240.0;
	for (const FParticipant& P : Participants)
	{
		const APlayerController* PC = P.PC.Get();
		ATortugaCharacter* Candidate = PC ? Cast<ATortugaCharacter>(PC->GetPawn()) : nullptr;
		if (!Candidate || !Candidate->IsInShell() || Candidate->IsDead())
		{
			continue;
		}
		const UTN_CarryComponent* Other = Candidate->GetCarryComponent();
		if (Other && (Other->IsBeingCarried() || Other->IsCarrying()))
		{
			continue;
		}
		const double Dist = FVector::Dist(Candidate->GetActorLocation(), Turtle->GetActorLocation());
		if (Dist < Best)
		{
			Best = Dist;
			Target = Candidate;
		}
	}
	if (Target)
	{
		const FVector To = (Target->GetActorLocation() - Turtle->GetActorLocation()).GetSafeNormal2D();
		if (!To.IsNearlyZero())
		{
			Turtle->SetActorRotation(FRotator(0.f, To.Rotation().Yaw, 0.f));
		}
		// En el servidor, la petición de coger se hace aquí mismo (Berta no tiene mando).
		Carry->TryGrabNearest();
	}
}
