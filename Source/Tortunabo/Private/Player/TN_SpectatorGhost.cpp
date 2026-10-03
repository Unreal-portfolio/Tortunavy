#include "Player/TN_SpectatorGhost.h"
#include "Player/TN_GhostCameraModifier.h"
#include "Player/TN_GhostEgg.h"
#include "Player/MP_GamePlayerController.h"
#include "TN_GhostInternal.h"
#include "TN_GhostMeshes.h"
#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_Log.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InputComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/SpectatorPawn.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "Settings/TN_GameSettingsSubsystem.h"
#include "UI/HUD/TN_GhostHatchWidget.h"
#include "UI/HUD/TN_GhostHUDWidget.h"

namespace TNSpectatorGhostDetail
{
	/** Tamaño del fantasmita (la malla mide ~1 m de la nariz a la punta de la cola con escala 1). */
	constexpr float GhostScale = 1.15f;
	/** Envíos de la cámara del dueño al servidor: cada cuánto como mucho, cada cuánto aunque no se mueva y desde cuánto. */
	constexpr float SendInterval = 1.f / 12.f;
	constexpr float KeepAliveInterval = 0.5f;
	constexpr double MinSendMoveSq = 3.0 * 3.0;
	/** Suavizado de la posición recibida en las demás máquinas. */
	constexpr float SmoothSpeed = 9.f;
	/** Se desvanece cuando una cámara local está a menos de FadeFar cm, y no se ve a menos de FadeNear. */
	constexpr float FadeNear = 70.f;
	constexpr float FadeFar = 230.f;
	/** Controles: grados por unidad del ratón (la escala de giro de serie del motor), del stick por segundo y zoom del gatillo. */
	constexpr float MouseDegreesPerUnit = 2.5f;
	constexpr float PadDegreesPerSecond = 150.f;
	constexpr float TriggerZoomStepsPerSecond = 5.f;
	/** Vuelo en U: altura sobre la base del huevo por la que entra (por encima de la tapa), punto de dentro y tramo final recto. */
	constexpr double EggTopHeight = 270.0;
	constexpr double EggInsideHeight = 90.0;
	constexpr float ArchEnd = 0.8f;
	/** Hombros de las aletas (cm, espacio de la malla). */
	const FVector ShoulderLeft(12.0, -20.0, 3.0);
	const FVector ShoulderRight(12.0, 20.0, 3.0);
}

ATN_SpectatorGhost::ATN_SpectatorGhost()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	SetNetUpdateFrequency(15.f);
	SetCanBeDamaged(false);
	SetActorEnableCollision(false);

	GhostRoot = CreateDefaultSubobject<USceneComponent>(TEXT("GhostRoot"));
	SetRootComponent(GhostRoot);
	FloatRoot = CreateDefaultSubobject<USceneComponent>(TEXT("FloatRoot"));
	FloatRoot->SetupAttachment(GhostRoot);
	FloatRoot->SetRelativeScale3D(FVector(TNSpectatorGhostDetail::GhostScale));

	// Mallas generadas en ejecución (RF_Transient; sus punteros, Transient): sin colisión, sin sombra y ocultas hasta
	// que se construyen.
	auto MakeMesh = [this](const TCHAR* Name, int32 SortPriority)
	{
		UProceduralMeshComponent* Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(Name);
		Mesh->SetFlags(RF_Transient);
		Mesh->SetupAttachment(FloatRoot);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		Mesh->SetCastShadow(false);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetTranslucentSortPriority(SortPriority);
		Mesh->SetVisibility(false);
		return Mesh;
	};
	TailMesh = MakeMesh(TEXT("TailMesh"), 0);
	BodyMesh = MakeMesh(TEXT("BodyMesh"), 1);
	FlipperLeft = MakeMesh(TEXT("FlipperLeft"), 1);
	FlipperRight = MakeMesh(TEXT("FlipperRight"), 1);
	EyesMesh = MakeMesh(TEXT("EyesMesh"), 3);
	FlipperLeft->SetRelativeLocation(TNSpectatorGhostDetail::ShoulderLeft);
	FlipperRight->SetRelativeLocation(TNSpectatorGhostDetail::ShoulderRight);
}

void ATN_SpectatorGhost::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_SpectatorGhost, GhostPlayerState);
	DOREPLIFETIME(ATN_SpectatorGhost, Stage);
	DOREPLIFETIME_CONDITION(ATN_SpectatorGhost, View, COND_SkipOwner);
	DOREPLIFETIME(ATN_SpectatorGhost, Revive);
}

// ─────────────────────────────────────────────────────────────────────────────
// Búsqueda
// ─────────────────────────────────────────────────────────────────────────────

ATN_SpectatorGhost* ATN_SpectatorGhost::FindFor(const APlayerController* PC)
{
	UWorld* World = PC ? PC->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	const APlayerState* PS = PC->PlayerState;
	ATN_SpectatorGhost* LeavingGhost = nullptr;
	for (TActorIterator<ATN_SpectatorGhost> It(World); It; ++It)
	{
		ATN_SpectatorGhost* Candidate = *It;
		if (!IsValid(Candidate) || Candidate->IsActorBeingDestroyed())
		{
			continue;
		}
		const bool bMine = Candidate->GetOwner() == PC || (PS && Candidate->GhostPlayerState == PS);
		if (!bMine)
		{
			continue;
		}
		if (Candidate->IsActiveGhost())
		{
			return Candidate;
		}
		LeavingGhost = Candidate;
	}
	return LeavingGhost;
}

ATN_SpectatorGhost* ATN_SpectatorGhost::FindForPlayerState(const APlayerState* PlayerState)
{
	UWorld* World = PlayerState ? PlayerState->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ATN_SpectatorGhost> It(World); It; ++It)
	{
		ATN_SpectatorGhost* Candidate = *It;
		if (IsValid(Candidate) && !Candidate->IsActorBeingDestroyed() && Candidate->IsActiveGhost() && Candidate->GhostPlayerState == PlayerState)
		{
			return Candidate;
		}
	}
	return nullptr;
}

void ATN_SpectatorGhost::GetActiveGhosts(const UWorld* World, TArray<ATN_SpectatorGhost*>& OutGhosts)
{
	OutGhosts.Reset();
	if (!World)
	{
		return;
	}
	for (TActorIterator<ATN_SpectatorGhost> It(World); It; ++It)
	{
		ATN_SpectatorGhost* Candidate = *It;
		if (IsValid(Candidate) && !Candidate->IsActorBeingDestroyed() && Candidate->IsActiveGhost())
		{
			OutGhosts.Add(Candidate);
		}
	}
}

float ATN_SpectatorGhost::ServerNow(const UWorld* World)
{
	if (!World)
	{
		return 0.f;
	}
	if (const AGameStateBase* GS = World->GetGameState())
	{
		return static_cast<float>(GS->GetServerWorldTimeSeconds());
	}
	return World->GetTimeSeconds();
}

APlayerController* ATN_SpectatorGhost::GetOwnerController() const
{
	return Cast<APlayerController>(GetOwner());
}

bool ATN_SpectatorGhost::IsLocallyOwned() const
{
	const APlayerController* PC = GetOwnerController();
	return PC && PC->IsLocalController();
}

APlayerState* ATN_SpectatorGhost::GetFollowedPlayerState() const
{
	// El dueño no recibe su propia vista: la saca de su ViewTarget.
	if (IsLocallyOwned() && !HasAuthority())
	{
		const APlayerController* PC = GetOwnerController();
		const APawn* Watched = PC ? Cast<APawn>(PC->GetViewTarget()) : nullptr;
		return IsWatchable(Watched) ? Watched->GetPlayerState() : nullptr;
	}
	return View.Followed;
}

bool ATN_SpectatorGhost::IsFreeCamera() const
{
	return !CameraModifier || CameraModifier->IsFree();
}

bool ATN_SpectatorGhost::IsWatchable(const APawn* Pawn) const
{
	if (!IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || Pawn->IsHidden() || Pawn->IsA<ASpectatorPawn>())
	{
		return false;
	}
	const APlayerState* PS = Pawn->GetPlayerState();
	if (!PS || PS == GhostPlayerState || TNGhost::IsGhostPlayer(PS))
	{
		return false;
	}
	// Como AMP_GamePlayerController::BuildSpectateCandidates: vivas y aún en juego.
	if (const ATN_CoopPlayerState* CoopPS = Cast<ATN_CoopPlayerState>(PS))
	{
		return CoopPS->IsAliveAndPlaying() && !CoopPS->bHasFinishedRun;
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Ciclo de vida
// ─────────────────────────────────────────────────────────────────────────────

void ATN_SpectatorGhost::BeginPlay()
{
	Super::BeginPlay();
	DisplayLocation = GetActorLocation();
	DisplayRotation = FRotator(0.f, GetActorRotation().Yaw, 0.f);
	if (GetNetMode() != NM_DedicatedServer)
	{
		BuildVisuals();
	}
	// El cartel del espectador en cada jugador de esta máquina: el suyo si es fantasma, o quién le está mirando.
	if (UWorld* World = GetWorld())
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* LocalPC = It->Get();
			if (LocalPC && LocalPC->IsLocalController())
			{
				UTN_GhostHUDWidget::EnsureFor(LocalPC);
			}
		}
	}
	// Quien se une (o recibe el fantasma) con la vuelta a la vida ya empezada.
	if (Stage == ETNGhostStage::Reviving)
	{
		StartLocalRevive();
	}
}

void ATN_SpectatorGhost::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CleanUpOwner();
	Super::EndPlay(EndPlayReason);
}

void ATN_SpectatorGhost::InitGhost(APlayerController* InOwner, APawn* InLeftBody)
{
	if (!HasAuthority())
	{
		return;
	}
	SetOwner(InOwner);
	GhostPlayerState = InOwner ? InOwner->PlayerState.Get() : nullptr;
	LeftBody = InLeftBody;
	View.Location = GetActorLocation();
	ForceNetUpdate();
	UE_LOG(LogTortunabo, Log, TEXT("[Fantasma] %s pasa a fantasma espectador."), *GetNameSafe(GhostPlayerState));
}

void ATN_SpectatorGhost::BeginRevive(ATN_GhostEgg* Egg, float FlightSeconds, float StartTime, float ArriveTime, float HatchTime)
{
	if (!HasAuthority() || !Egg)
	{
		return;
	}
	ReviveEgg = Egg;
	Revive.EggLocation = Egg->GetActorLocation();
	Revive.EggYaw = static_cast<float>(Egg->GetActorRotation().Yaw);
	Revive.StartTime = StartTime;
	Revive.FlightSeconds = FlightSeconds;
	Revive.ArriveTime = ArriveTime;
	Revive.HatchTime = HatchTime;
	Stage = ETNGhostStage::Reviving;
	bLocalReviveStarted = false;
	bHatchScreenShown = false;
	OnRep_Stage();
	ForceNetUpdate();
}

void ATN_SpectatorGhost::CancelRevive()
{
	if (!HasAuthority() || Stage != ETNGhostStage::Reviving)
	{
		return;
	}
	Revive = FTNGhostRevive();
	ReviveEgg.Reset();
	Stage = ETNGhostStage::Spectating;
	OnRep_Stage();
	ForceNetUpdate();
}

void ATN_SpectatorGhost::EndGhost()
{
	if (!HasAuthority() || Stage == ETNGhostStage::Leaving)
	{
		return;
	}
	Stage = ETNGhostStage::Leaving;
	OnRep_Stage();
	ForceNetUpdate();
	SetLifeSpan(0.6f);
	UE_LOG(LogTortunabo, Log, TEXT("[Fantasma] %s deja de ser fantasma."), *GetNameSafe(GhostPlayerState));
}

void ATN_SpectatorGhost::OnRep_Stage()
{
	if (Stage == ETNGhostStage::Leaving)
	{
		CleanUpOwner();
	}
	else if (Stage == ETNGhostStage::Reviving)
	{
		StartLocalRevive();
	}
	else
	{
		bLocalReviveStarted = false;
		bHatchScreenShown = false;
		bFlightFromSet = false;
	}
}

void ATN_SpectatorGhost::OnRep_Revive()
{
	if (Stage == ETNGhostStage::Reviving)
	{
		StartLocalRevive();
	}
}

void ATN_SpectatorGhost::StartLocalRevive()
{
	if (Stage != ETNGhostStage::Reviving || Revive.HatchTime <= 0.f)
	{
		return;
	}
	if (!bLocalReviveStarted)
	{
		bLocalReviveStarted = true;
		bFlightFromSet = false;
	}
	// En la pantalla del que vuelve: negro, ¡pum!, la cáscara oscura que se resquebraja y se abre (solo en la suya). Se
	// reintenta en cada Tick por si el dueño llega después que la fase.
	if (!bHatchScreenShown && IsLocallyOwned())
	{
		bHatchScreenShown = true;
		const float Now = ServerNow(GetWorld());
		UTN_GhostHatchWidget::ShowFor(GetOwnerController(), Revive.ArriveTime - Now, Revive.HatchTime - Now);
	}
}

void ATN_SpectatorGhost::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	LocalTime += DeltaSeconds;
	if (HasAuthority())
	{
		TickServer(DeltaSeconds);
		if (IsActorBeingDestroyed())
		{
			return;
		}
	}
	if (Stage == ETNGhostStage::Reviving)
	{
		StartLocalRevive();
	}
	if (IsLocallyOwned() && IsActiveGhost())
	{
		// Ya con tortuga propia (acaba de salir del huevo), aunque aún no haya llegado que se va: nada de cámaras ni controles.
		const APlayerController* PC = GetOwnerController();
		if (PC && PC->GetPawn())
		{
			CleanUpOwner();
		}
		else
		{
			SetUpOwner();
			TickOwner(DeltaSeconds);
		}
	}
	if (bVisualsBuilt)
	{
		UpdateVisuals(DeltaSeconds);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor
// ─────────────────────────────────────────────────────────────────────────────

void ATN_SpectatorGhost::TickServer(float DeltaSeconds)
{
	const APlayerController* PC = GetOwnerController();
	if (!IsValid(PC) || PC->IsActorBeingDestroyed())
	{
		// Su jugador se ha ido: el fantasma también.
		if (Stage != ETNGhostStage::Leaving)
		{
			Destroy();
		}
		return;
	}
	if (Stage == ETNGhostStage::Spectating)
	{
		ApplyServerFollow(View.Followed);
	}
}

void ATN_SpectatorGhost::ApplyServerFollow(APlayerState* Followed)
{
	APlayerController* PC = GetOwnerController();
	if (!PC || PC->IsLocalController())
	{
		return;
	}
	APawn* Pawn = Followed ? Followed->GetPawn() : nullptr;
	if (!Pawn || Pawn == ServerViewPawn.Get())
	{
		return;
	}
	ServerViewPawn = Pawn;
	// El cliente elige solo a quién mira (bClientSimulatingViewTarget); esto es para que el servidor le mande la rotación
	// de la cámara de ese jugador (TargetViewRotation), que es lo que enseña la vista fija.
	PC->SetViewTarget(Pawn);
}

void ATN_SpectatorGhost::ServerUpdateView_Implementation(FVector_NetQuantize10 CameraLocation, APlayerState* Followed)
{
	if (Stage != ETNGhostStage::Spectating || CameraLocation.ContainsNaN())
	{
		return;
	}
	FVector Location = CameraLocation;
	APlayerState* Target = (Followed && Followed != GhostPlayerState) ? Followed : nullptr;
	// Nunca más lejos de la tortuga que sigue que el tope.
	if (const APawn* Pawn = Target ? Target->GetPawn() : nullptr)
	{
		const FVector Offset = Location - Pawn->GetActorLocation();
		if (Offset.SizeSquared() > FMath::Square(MaxDistanceToTurtle))
		{
			Location = Pawn->GetActorLocation() + Offset.GetSafeNormal() * MaxDistanceToTurtle;
		}
	}
	View.Location = Location;
	View.Followed = Target;
	ApplyServerFollow(Target);
}

// ─────────────────────────────────────────────────────────────────────────────
// Dueño: cámaras, controles y vista
// ─────────────────────────────────────────────────────────────────────────────

void ATN_SpectatorGhost::SetUpOwner()
{
	APlayerController* PC = GetOwnerController();
	if (bOwnerSetUp || !PC || !PC->IsLocalController())
	{
		return;
	}
	bOwnerSetUp = true;
	if (APlayerCameraManager* Camera = PC->PlayerCameraManager)
	{
		// El jugador elige a quién sigue: el servidor solo le sigue la pista (no le cambia la ViewTarget).
		if (PC->GetNetMode() == NM_Client)
		{
			Camera->bClientSimulatingViewTarget = true;
		}
		CameraModifier = Cast<UTN_GhostCameraModifier>(Camera->AddNewCameraModifier(UTN_GhostCameraModifier::StaticClass()));
		if (CameraModifier)
		{
			CameraModifier->SetGhost(this);
		}
	}

	// Controles propios encima de la pila del PlayerController (sin tocarlo): órbita, zoom, cambiar de tortuga y cámara.
	// La rueda, con la cámara fija, sigue cambiando de tortuga como antes.
	GhostInput = NewObject<UInputComponent>(PC, UInputComponent::StaticClass(), NAME_None, RF_Transient);
	GhostInput->Priority = 5;
	GhostInput->BindAxisKey(EKeys::MouseX, this, &ATN_SpectatorGhost::HandleMouseX);
	GhostInput->BindAxisKey(EKeys::MouseY, this, &ATN_SpectatorGhost::HandleMouseY);
	GhostInput->BindAxisKey(EKeys::Gamepad_RightX, this, &ATN_SpectatorGhost::HandlePadX);
	GhostInput->BindAxisKey(EKeys::Gamepad_RightY, this, &ATN_SpectatorGhost::HandlePadY);
	GhostInput->BindAxisKey(EKeys::Gamepad_RightTriggerAxis, this, &ATN_SpectatorGhost::HandleZoomInAxis);
	GhostInput->BindAxisKey(EKeys::Gamepad_LeftTriggerAxis, this, &ATN_SpectatorGhost::HandleZoomOutAxis);
	GhostInput->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &ATN_SpectatorGhost::HandleWheelUp);
	GhostInput->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &ATN_SpectatorGhost::HandleWheelDown);
	GhostInput->BindKey(EKeys::Right, IE_Pressed, this, &ATN_SpectatorGhost::HandleNext);
	GhostInput->BindKey(EKeys::Left, IE_Pressed, this, &ATN_SpectatorGhost::HandlePrevious);
	GhostInput->BindKey(EKeys::Gamepad_RightShoulder, IE_Pressed, this, &ATN_SpectatorGhost::HandleNext);
	GhostInput->BindKey(EKeys::Gamepad_LeftShoulder, IE_Pressed, this, &ATN_SpectatorGhost::HandlePrevious);
	GhostInput->BindKey(EKeys::Gamepad_DPad_Right, IE_Pressed, this, &ATN_SpectatorGhost::HandleNext);
	GhostInput->BindKey(EKeys::Gamepad_DPad_Left, IE_Pressed, this, &ATN_SpectatorGhost::HandlePrevious);
	GhostInput->BindKey(EKeys::C, IE_Pressed, this, &ATN_SpectatorGhost::HandleToggleCamera);
	GhostInput->BindKey(EKeys::Gamepad_RightThumbstick, IE_Pressed, this, &ATN_SpectatorGhost::HandleToggleCamera);
	PC->PushInputComponent(GhostInput);

	UTN_GhostHUDWidget::EnsureFor(PC);
	InvalidTargetTime = 0.f;
	SwitchRetryTimer = 0.f;
	SendTimer = 0.f;
	KeepAliveTimer = 0.f;
}

void ATN_SpectatorGhost::CleanUpOwner()
{
	if (!bOwnerSetUp)
	{
		return;
	}
	bOwnerSetUp = false;
	APlayerController* PC = GetOwnerController();
	if (IsValid(PC))
	{
		if (GhostInput)
		{
			PC->PopInputComponent(GhostInput);
		}
		if (APlayerCameraManager* Camera = PC->PlayerCameraManager)
		{
			if (CameraModifier)
			{
				Camera->RemoveCameraModifier(CameraModifier);
			}
			if (PC->GetNetMode() == NM_Client)
			{
				Camera->bClientSimulatingViewTarget = false;
			}
		}
	}
	GhostInput = nullptr;
	CameraModifier = nullptr;
}

void ATN_SpectatorGhost::TickOwner(float DeltaSeconds)
{
	using namespace TNSpectatorGhostDetail;
	APlayerController* PC = GetOwnerController();
	if (!PC || Stage != ETNGhostStage::Spectating)
	{
		return;
	}

	// A quién sigue: si la tortuga ya no vale (ha llegado, ha caído, se ha ido o es otro fantasma), a la siguiente.
	APawn* Watched = Cast<APawn>(PC->GetViewTarget());
	if (IsWatchable(Watched))
	{
		InvalidTargetTime = 0.f;
		SwitchRetryTimer = 0.f;
	}
	else
	{
		InvalidTargetTime += DeltaSeconds;
		SwitchRetryTimer -= DeltaSeconds;
		if (InvalidTargetTime > 0.35f && SwitchRetryTimer <= 0.f)
		{
			SwitchRetryTimer = 1.f;
			if (AMP_GamePlayerController* GamePC = Cast<AMP_GamePlayerController>(PC))
			{
				GamePC->SpectateNextPlayer();
				Watched = Cast<APawn>(PC->GetViewTarget());
			}
		}
	}
	APlayerState* Followed = IsWatchable(Watched) ? Watched->GetPlayerState() : nullptr;

	// El fantasma flota justo donde está la cámara (la del fotograma anterior).
	FVector CameraLocation = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : GetActorLocation();
	if (CameraModifier && CameraModifier->HasLastView())
	{
		CameraLocation = CameraModifier->GetLastView().Location;
	}
	DisplayLocation = CameraLocation;
	bHasDisplay = true;
	// El servidor mira qué le llega a un espectador desde su peón de espectador (LastSpectatorSyncLocation): que vaya
	// con la cámara, o dejaría de recibir lo que pasa lejos de donde se hizo fantasma.
	if (ASpectatorPawn* Spectator = PC->GetSpectatorPawn())
	{
		Spectator->SetActorLocation(CameraLocation, false, nullptr, ETeleportType::TeleportPhysics);
	}

	if (HasAuthority())
	{
		View.Location = CameraLocation;
		View.Followed = Followed;
		return;
	}
	SendTimer -= DeltaSeconds;
	KeepAliveTimer -= DeltaSeconds;
	const bool bFollowedChanged = Followed != LastSentFollowed.Get();
	const bool bMoved = FVector::DistSquared(CameraLocation, LastSentLocation) > MinSendMoveSq;
	if (bFollowedChanged || (SendTimer <= 0.f && (bMoved || KeepAliveTimer <= 0.f)))
	{
		SendTimer = SendInterval;
		KeepAliveTimer = KeepAliveInterval;
		LastSentLocation = CameraLocation;
		LastSentFollowed = Followed;
		ServerUpdateView(CameraLocation, Followed);
	}
}

void ATN_SpectatorGhost::AddLook(float X, float Y, bool bGamepad)
{
	using namespace TNSpectatorGhostDetail;
	if (!CameraModifier || !CameraModifier->IsFree() || Stage != ETNGhostStage::Spectating || (FMath::IsNearlyZero(X) && FMath::IsNearlyZero(Y)))
	{
		return;
	}
	const APlayerController* PC = GetOwnerController();
	const UWorld* World = GetWorld();
	if (!PC || !World || PC->ShouldShowMouseCursor())
	{
		return;
	}
	// Sensibilidad e inversión del menú de ajustes, del aparato que mueve la cámara (se lee el valor crudo).
	float Sensitivity = 1.f;
	bool bInvertY = false;
	if (const UTN_GameSettingsSubsystem* Settings = UTN_GameSettingsSubsystem::Get(PC))
	{
		Sensitivity = Settings->GetLookSensitivityFor(PC, bGamepad);
		bInvertY = Settings->IsLookYInvertedFor(PC, bGamepad);
	}
	const float Scale = (bGamepad ? PadDegreesPerSecond * World->GetDeltaSeconds() : MouseDegreesPerUnit) * Sensitivity;
	CameraModifier->AddOrbitInput(X * Scale, Y * Scale * (bInvertY ? -1.f : 1.f));
}

void ATN_SpectatorGhost::HandleMouseX(float Value) { AddLook(Value, 0.f, false); }
void ATN_SpectatorGhost::HandleMouseY(float Value) { AddLook(0.f, Value, false); }
void ATN_SpectatorGhost::HandlePadX(float Value) { AddLook(Value, 0.f, true); }
void ATN_SpectatorGhost::HandlePadY(float Value) { AddLook(0.f, Value, true); }

void ATN_SpectatorGhost::HandleZoomInAxis(float Value)
{
	if (CameraModifier && Value > 0.05f && GetWorld())
	{
		CameraModifier->AddZoom(Value * TNSpectatorGhostDetail::TriggerZoomStepsPerSecond * GetWorld()->GetDeltaSeconds());
	}
}

void ATN_SpectatorGhost::HandleZoomOutAxis(float Value)
{
	if (CameraModifier && Value > 0.05f && GetWorld())
	{
		CameraModifier->AddZoom(-Value * TNSpectatorGhostDetail::TriggerZoomStepsPerSecond * GetWorld()->GetDeltaSeconds());
	}
}

void ATN_SpectatorGhost::HandleWheelUp()
{
	if (CameraModifier && CameraModifier->IsFree())
	{
		CameraModifier->AddZoom(1.f);
		return;
	}
	HandleNext();
}

void ATN_SpectatorGhost::HandleWheelDown()
{
	if (CameraModifier && CameraModifier->IsFree())
	{
		CameraModifier->AddZoom(-1.f);
		return;
	}
	HandlePrevious();
}

void ATN_SpectatorGhost::HandleNext()
{
	if (Stage != ETNGhostStage::Spectating)
	{
		return;
	}
	if (AMP_GamePlayerController* GamePC = Cast<AMP_GamePlayerController>(GetOwnerController()))
	{
		GamePC->SpectateNextPlayer();
	}
}

void ATN_SpectatorGhost::HandlePrevious()
{
	if (Stage != ETNGhostStage::Spectating)
	{
		return;
	}
	if (AMP_GamePlayerController* GamePC = Cast<AMP_GamePlayerController>(GetOwnerController()))
	{
		GamePC->SpectatePreviousPlayer();
	}
}

void ATN_SpectatorGhost::HandleToggleCamera()
{
	if (CameraModifier && Stage == ETNGhostStage::Spectating)
	{
		CameraModifier->ToggleFree();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Malla y animación (cada máquina con pantalla)
// ─────────────────────────────────────────────────────────────────────────────

void ATN_SpectatorGhost::BuildVisuals()
{
	UMaterialInterface* BodyBase = TNGhostMesh::LoadMaterial(true);
	UMaterialInterface* EyesBase = TNGhostMesh::LoadMaterial(false);
	BodyMaterial = BodyBase ? UMaterialInstanceDynamic::Create(BodyBase, this) : nullptr;
	EyesMaterial = EyesBase ? UMaterialInstanceDynamic::Create(EyesBase, this) : nullptr;

	TNGhostMesh::FSmoothMesh Mesh;
	TNGhostMesh::BuildBody(Mesh);
	Mesh.Create(BodyMesh, BodyMaterial);
	Mesh.Reset();
	TNGhostMesh::BuildEyes(Mesh);
	Mesh.Create(EyesMesh, EyesMaterial);
	Mesh.Reset();
	TNGhostMesh::BuildFlipper(Mesh, -1.f);
	Mesh.Create(FlipperLeft, BodyMaterial);
	Mesh.Reset();
	TNGhostMesh::BuildFlipper(Mesh, 1.f);
	Mesh.Create(FlipperRight, BodyMaterial);
	TNGhostMesh::BuildTail(Mesh, 0.f, 0.f);
	Mesh.Create(TailMesh, BodyMaterial);

	bVisualsBuilt = true;
	bVisible = true;
	SetGhostVisible(false);
}

void ATN_SpectatorGhost::SetGhostVisible(bool bInVisible)
{
	if (bVisible == bInVisible)
	{
		return;
	}
	bVisible = bInVisible;
	for (UProceduralMeshComponent* Mesh : { BodyMesh.Get(), TailMesh.Get(), FlipperLeft.Get(), FlipperRight.Get(), EyesMesh.Get() })
	{
		if (Mesh)
		{
			Mesh->SetVisibility(bInVisible);
		}
	}
}

void ATN_SpectatorGhost::ApplyOpacity(float Opacity)
{
	if (FMath::Abs(Opacity - ShownOpacity) < 0.01f)
	{
		return;
	}
	ShownOpacity = Opacity;
	// Parámetro «Opacity» de M_ProcFXCloud / M_ProcFXSoft (multiplica el alfa del color de vértice).
	if (BodyMaterial)
	{
		BodyMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.8f * Opacity);
	}
	if (EyesMaterial)
	{
		EyesMaterial->SetScalarParameterValue(TEXT("Opacity"), Opacity);
	}
}

FVector ATN_SpectatorGhost::FlightPoint(float U, FVector* OutTangent) const
{
	using namespace TNSpectatorGhostDetail;
	const FVector Top = FVector(Revive.EggLocation) + FVector(0.0, 0.0, EggTopHeight);
	const FVector Inside = FVector(Revive.EggLocation) + FVector(0.0, 0.0, EggInsideHeight);
	if (U >= ArchEnd)
	{
		// Tramo final: de cabeza, recto hacia abajo, dentro del huevo.
		if (OutTangent)
		{
			*OutTangent = FVector(0.0, 0.0, -1.0);
		}
		return FMath::Lerp(Top, Inside, static_cast<double>((U - ArchEnd) / (1.f - ArchEnd)));
	}
	// U invertida: sube, arquea por encima y baja en vertical hasta encima del huevo (Bézier cúbica).
	const float Linear = U / ArchEnd;
	const double T = Linear * Linear * (3.f - 2.f * Linear);
	const double Height = FMath::Clamp(0.35 * FVector::Dist(FlightFrom, Top) + 150.0, 150.0, 450.0);
	const FVector P0 = FlightFrom;
	const FVector P1 = FlightFrom + FVector(0.0, 0.0, Height);
	const FVector P2 = Top + FVector(0.0, 0.0, Height);
	const FVector P3 = Top;
	const double Inv = 1.0 - T;
	if (OutTangent)
	{
		const FVector Derivative = (P1 - P0) * (3.0 * Inv * Inv) + (P2 - P1) * (6.0 * Inv * T) + (P3 - P2) * (3.0 * T * T);
		*OutTangent = Derivative.GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0, 0.0, -1.0));
	}
	return P0 * (Inv * Inv * Inv) + P1 * (3.0 * Inv * Inv * T) + P2 * (3.0 * Inv * T * T) + P3 * (T * T * T);
}

void ATN_SpectatorGhost::UpdateTail()
{
	// Una sola copia para todos los fantasmas (se usa y se sube en el acto, en el hilo de juego).
	static TNGhostMesh::FSmoothMesh TailScratch;
	const float Swish = Stage == ETNGhostStage::Reviving ? 1.f : 0.f;
	TNGhostMesh::BuildTail(TailScratch, LocalTime, Swish);
	if (TailMesh && TailMesh->GetNumSections() > 0)
	{
		TailScratch.Update(TailMesh);
	}
	else
	{
		TailScratch.Create(TailMesh, BodyMaterial);
	}
}

void ATN_SpectatorGhost::UpdateVisuals(float DeltaSeconds)
{
	using namespace TNSpectatorGhostDetail;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const bool bOwner = IsLocallyOwned();
	bool bShow = true;
	float DiveScale = 1.f;
	float DiveFade = 1.f;
	bool bFlying = false;

	// Volviendo a la vida, y también al irse después (sale del huevo): nunca vuelve a su sitio de antes.
	if (Stage != ETNGhostStage::Spectating && Revive.HatchTime > 0.f)
	{
		// ── Vuelo en U hasta el huevo (en todas las máquinas, con el mismo reloj) ──
		if (!bFlightFromSet)
		{
			FlightFrom = bHasDisplay ? DisplayLocation : GetActorLocation();
			bFlightFromSet = true;
		}
		const FVector Inside = FVector(Revive.EggLocation) + FVector(0.0, 0.0, EggInsideHeight);
		const float Elapsed = ServerNow(World) - Revive.StartTime;
		if (Revive.FlightSeconds <= 0.f || Elapsed >= Revive.FlightSeconds)
		{
			// Sin vuelo o ya dentro del huevo.
			DisplayLocation = Inside;
			bShow = false;
		}
		else
		{
			const float U = FMath::Clamp(Elapsed / Revive.FlightSeconds, 0.f, 1.f);
			FVector Tangent = FVector::UpVector;
			DisplayLocation = FlightPoint(U, &Tangent);
			FVector Across = (FVector(Revive.EggLocation) - FlightFrom).GetSafeNormal2D();
			if (Across.IsNearlyZero())
			{
				Across = FVector(1.0, 0.0, 0.0);
			}
			const FVector RightAxis = FVector::CrossProduct(FVector::UpVector, Across);
			DisplayRotation = FRotationMatrix::MakeFromXY(Tangent, RightAxis).Rotator();
			// Al meterse encoge un poco y se apaga, como si entrara por la cáscara.
			DiveScale = U > 0.8f ? FMath::Lerp(1.f, 0.35f, (U - 0.8f) / 0.2f) : 1.f;
			DiveFade = U > 0.86f ? 1.f - (U - 0.86f) / 0.14f : 1.f;
			bFlying = true;
		}
		bHasDisplay = true;
	}
	else
	{
		// ── Flotando donde está la cámara del espectador ──
		const APlayerState* FollowedPS = GetFollowedPlayerState();
		const APawn* FollowedPawn = FollowedPS ? FollowedPS->GetPawn() : nullptr;
		if (!bOwner)
		{
			FVector Target = View.Location;
			if (FollowedPawn)
			{
				const FVector Offset = Target - FollowedPawn->GetActorLocation();
				if (Offset.SizeSquared() > FMath::Square(MaxDistanceToTurtle))
				{
					Target = FollowedPawn->GetActorLocation() + Offset.GetSafeNormal() * MaxDistanceToTurtle;
				}
			}
			DisplayLocation = bHasDisplay ? FMath::VInterpTo(DisplayLocation, Target, DeltaSeconds, SmoothSpeed) : Target;
			bHasDisplay = true;
		}
		// Mirando a la tortuga que sigue.
		if (FollowedPawn)
		{
			FRotator Look = (FollowedPawn->GetActorLocation() + FVector(0.0, 0.0, 30.0) - DisplayLocation).Rotation();
			Look.Pitch = FMath::Clamp(Look.Pitch, -35.0, 25.0);
			Look.Roll = 0.0;
			DisplayRotation = FMath::RInterpTo(DisplayRotation, Look, DeltaSeconds, 5.f);
		}
		// El dueño no se ve a sí mismo (flota justo en su cámara).
		bShow = !bOwner && bHasDisplay;
	}
	SetActorLocationAndRotation(DisplayLocation, DisplayRotation);

	// ── Opacidad: aparece, se va, se mete en el huevo y se aparta de las cámaras que lo atraviesan ──
	SpawnFade = FMath::Min(1.f, SpawnFade + DeltaSeconds / 0.5f);
	if (Stage == ETNGhostStage::Leaving)
	{
		LeaveFade = FMath::Max(0.f, LeaveFade - DeltaSeconds / 0.4f);
	}
	// En el vuelo, los demás lo ven siempre; el dueño lo ve salir de su cámara poco a poco.
	float Proximity = 1.f;
	if (!bFlying || bOwner)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* LocalPC = It->Get();
			if (LocalPC && LocalPC->IsLocalController() && LocalPC->PlayerCameraManager)
			{
				const float CameraDistance = static_cast<float>(FVector::Dist(LocalPC->PlayerCameraManager->GetCameraLocation(), DisplayLocation));
				Proximity = FMath::Min(Proximity, FMath::SmoothStep(FadeNear, FadeFar, CameraDistance));
			}
		}
	}
	const float Opacity = SpawnFade * LeaveFade * Proximity * DiveFade;
	bShow = bShow && Opacity > 0.02f;
	SetGhostVisible(bShow);
	if (!bShow)
	{
		return;
	}
	ApplyOpacity(Opacity);

	// ── Se mece: sube y baja, se balancea, bate las aletas y la colita ondea ──
	const float Bob = 5.f * FMath::Sin(LocalTime * 2.1f);
	FloatRoot->SetRelativeLocationAndRotation(FVector(0.0, 0.0, Bob),
		FRotator(3.f * FMath::Sin(LocalTime * 1.7f), 0.f, 5.f * FMath::Sin(LocalTime * 1.3f)));
	FloatRoot->SetRelativeScale3D(FVector(GhostScale * DiveScale));
	const float Flap = 22.f * FMath::Sin(LocalTime * 3.4f) + (bFlying ? 16.f * FMath::Sin(LocalTime * 9.f) : 0.f);
	FlipperLeft->SetRelativeRotation(FRotator(0.f, -22.f, -Flap));
	FlipperRight->SetRelativeRotation(FRotator(0.f, 22.f, Flap));
	UpdateTail();
}
