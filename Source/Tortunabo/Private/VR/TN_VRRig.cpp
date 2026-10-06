#include "VR/TN_VRRig.h"
#include "VR/TN_VRScreenWidget.h"
#include "VR/TN_VRSubsystem.h"
#include "VR/TN_VRMath.h"
#include "VR/TN_VRGrabComponent.h"
#include "VR/TN_VRInputTriggers.h"
#include "VR/TN_VRSeatComponent.h"

#include "Core/TN_Log.h"
#include "Core/TN_ProjectMaterials.h"
#include "Player/MP_GamePlayerController.h"
#include "Player/TortugaCharacter.h"
#include "Settings/TN_GameSettingsSubsystem.h"
#include "UI/Loading/TN_LoadingScreenSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/HitResult.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MotionControllerComponent.h"
#include "ProceduralMeshComponent.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#include "../World/ProcMap/TN_ProcMapRuntimeMesh.h"

// ─────────────────────────────────────────────────────────────────────────────
// Consola
// ─────────────────────────────────────────────────────────────────────────────

static TAutoConsoleVariable<float> CVarTNVRHudDistance(TEXT("TN.VR.HudDistance"), 150.f,
	TEXT("Distancia (cm) del HUD delante de los ojos en VR. Se acerca solo si hay una pared en medio."), ECVF_Default);
static TAutoConsoleVariable<float> CVarTNVRHudFov(TEXT("TN.VR.HudFov"), 80.f,
	TEXT("Ancho (grados) que ocupa el HUD en VR: el arco del panel curvo alrededor de los ojos."), ECVF_Default);
static TAutoConsoleVariable<float> CVarTNVRMenuDistance(TEXT("TN.VR.MenuDistance"), 160.f,
	TEXT("Distancia (cm) a la que se ponen los menús en VR."), ECVF_Default);
static TAutoConsoleVariable<float> CVarTNVRMenuFov(TEXT("TN.VR.MenuFov"), 100.f,
	TEXT("Ancho (grados) que ocupan los menús en VR: el arco del panel curvo que te rodea."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarTNVRHudFollow(TEXT("TN.VR.HudFollow"), 0,
	TEXT("HUD en VR: 0 anclado a la cámara (siempre fijo en la vista, como en la pantalla), 1 suelto delante siguiendo a la cabeza con retraso (marea menos a algunos)."), ECVF_Default);
static TAutoConsoleVariable<float> CVarTNVRLoadingDome(TEXT("TN.VR.LoadingDomeRadius"), 300.f,
	TEXT("Radio (cm) de la playa en 360 que rodea la cabeza mientras sale la pantalla de carga en VR (0 = sin ella)."), ECVF_Default);
static TAutoConsoleVariable<float> CVarTNVRSmoothTurnSpeed(TEXT("TN.VR.SmoothTurnSpeed"), 120.f,
	TEXT("Grados por segundo del giro suave en VR (ajuste «Giro en VR: suave»)."), ECVF_Default);

namespace TNVRRigDetail
{
	const TCHAR* const ControlsPath = TEXT("/Game/Blueprints/Gameplay/Controls/");

	/**
	 * Color sRGB 0xRRGGBB para M_CosmeticVertexColor (MakeStaticMesh decodifica una vez más: así llega lineal). El alfa
	 * es el brillo del material (0 = mate, sin metal): las aletas son piel, no metal.
	 */
	FLinearColor Pal(uint32 Hex)
	{
		return FLinearColor(TNProcRuntimeMesh::SRGBToLinear(((Hex >> 16) & 255) / 255.f), TNProcRuntimeMesh::SRGBToLinear(((Hex >> 8) & 255) / 255.f),
			TNProcRuntimeMesh::SRGBToLinear((Hex & 255) / 255.f), 0.f);
	}

	UMaterialInterface* VertexColorMaterial()
	{
		return TNMaterials::VertexColor();
	}

	UInputAction* LoadAction(const TCHAR* Name)
	{
		const FString Path = FString::Printf(TEXT("%s%s.%s"), ControlsPath, Name, Name);
		return LoadObject<UInputAction>(nullptr, *Path);
	}

	/**
	 * Aleta de tortuga de la mano: pala aplanada a lo largo de +X (de la muñeca a la punta, 17 cm), con la manga de
	 * caparazón en la muñeca y tres uñas claras en el borde.
	 */
	UStaticMesh* BuildFlipperMesh(UObject* Outer)
	{
		TNProcMesh::FTNProcMeshBuffers B;
		const FLinearColor Skin = Pal(0x6FC25C);
		const FLinearColor Shell = Pal(0x3F7F37);
		const FLinearColor Nail = Pal(0xEDE3B6);
		constexpr int32 Seg = 12;
		const double Xs[] = { 0.0, 3.0, 7.0, 11.0, 14.5, 17.0 };
		const double HalfW[] = { 3.0, 4.0, 4.9, 4.7, 3.5, 1.4 };
		const double HalfT[] = { 2.4, 1.9, 1.5, 1.2, 0.9, 0.5 };
		const int32 NumRings = static_cast<int32>(UE_ARRAY_COUNT(Xs));
		TArray<TArray<FVector>> Rings;
		for (int32 r = 0; r < NumRings; ++r)
		{
			TArray<FVector> Ring;
			for (int32 k = 0; k < Seg; ++k)
			{
				const double Ang = 2.0 * PI * k / Seg;
				Ring.Add(FVector(Xs[r], FMath::Cos(Ang) * HalfW[r], FMath::Sin(Ang) * HalfT[r]));
			}
			Rings.Add(Ring);
		}
		B.AddSweep(Rings, true, Skin);
		const FVector WristC(Xs[0], 0.0, 0.0);
		const FVector TipC(Xs[NumRings - 1] + 0.6, 0.0, 0.0);
		for (int32 k = 0; k < Seg; ++k)
		{
			B.AddTri(WristC, Rings[0][k], Rings[0][(k + 1) % Seg], FVector(-1.0, 0.0, 0.0), Skin);
			B.AddTri(TipC, Rings[NumRings - 1][k], Rings[NumRings - 1][(k + 1) % Seg], FVector(1.0, 0.0, 0.0), Skin);
		}
		// Manga de caparazón en la muñeca.
		TNProcMesh::TNProcAddCylinder(B, FVector(-5.0, 0.0, 0.0), FVector(0.5, 0.0, 0.0), 3.7, 3.3, Seg, Shell);
		// Uñas en el borde de delante.
		for (int32 n = -1; n <= 1; ++n)
		{
			const FVector Base(13.0, n * 2.6, 0.0);
			TNProcMesh::TNProcAddCylinder(B, Base, Base + FVector(4.2, n * 0.5, 0.0), 0.8, 0.2, 6, Nail);
		}
		return TNProcRuntimeMesh::MakeStaticMesh(Outer, B, VertexColorMaterial());
	}

	UCameraComponent* FindActiveCamera(AActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}
		TInlineComponentArray<UCameraComponent*> Cameras(Actor);
		for (UCameraComponent* Camera : Cameras)
		{
			if (Camera && Camera->IsActive())
			{
				return Camera;
			}
		}
		return nullptr;
	}

	/** Color de la playa en 360 de la carga según la altura (Z de -1 abajo a 1 arriba): cielo, bruma, mar y arena. */
	FLinearColor DomeColor(double Z)
	{
		struct FStop { double Z; FColor Color; };
		static const FStop Stops[] = {
			{ -1.0, FColor(233, 211, 161) }, { -0.55, FColor(218, 196, 146) }, { -0.35, FColor(27, 110, 143) },
			{ -0.05, FColor(64, 172, 196) }, { 0.0, FColor(255, 241, 214) }, { 0.12, FColor(170, 220, 245) },
			{ 0.45, FColor(96, 176, 234) }, { 1.0, FColor(52, 132, 212) } };
		for (int32 i = 1; i < static_cast<int32>(UE_ARRAY_COUNT(Stops)); ++i)
		{
			if (Z <= Stops[i].Z)
			{
				const float A = static_cast<float>((Z - Stops[i - 1].Z) / (Stops[i].Z - Stops[i - 1].Z));
				return FMath::Lerp(FLinearColor::FromSRGBColor(Stops[i - 1].Color), FLinearColor::FromSRGBColor(Stops[i].Color), FMath::Clamp(A, 0.f, 1.f));
			}
		}
		return FLinearColor::FromSRGBColor(Stops[UE_ARRAY_COUNT(Stops) - 1].Color);
	}

	/** Material de la cúpula: translúcido sin luz con el color del vértice (el de los avisos); si falta, el de color de vértice. */
	UMaterialInterface* DomeMaterial()
	{
		UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_ProcFXHard.M_ProcFXHard"), nullptr, LOAD_NoWarn);
		return Mat ? Mat : VertexColorMaterial();
	}

	/** Postura de las aletas en la vista simulada (sin gafas): abajo a los lados, apuntando al frente. */
	const FTransform SimLeftHand(FRotator(-8.0, 10.0, -20.0), FVector(34.0, -19.0, -22.0));
	const FTransform SimRightHand(FRotator(-8.0, -10.0, 20.0), FVector(34.0, 19.0, -22.0));
}

// ─────────────────────────────────────────────────────────────────────────────
// Construcción
// ─────────────────────────────────────────────────────────────────────────────

ATN_VRRig::ATN_VRRig()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	bReplicates = false;
	SetCanBeDamaged(false);

	RigRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RigRoot"));
	RootComponent = RigRoot;

	RigCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("RigCamera"));
	RigCamera->SetupAttachment(RigRoot);
	RigCamera->bLockToHmd = true;
	RigCamera->FieldOfView = 90.f;

	auto MakeController = [this](const TCHAR* Name, const TCHAR* Source)
	{
		UMotionControllerComponent* Controller = CreateDefaultSubobject<UMotionControllerComponent>(Name);
		Controller->SetupAttachment(RigRoot);
		Controller->MotionSource = FName(Source);
		return Controller;
	};
	LeftGrip = MakeController(TEXT("LeftGrip"), TEXT("LeftGrip"));
	RightGrip = MakeController(TEXT("RightGrip"), TEXT("RightGrip"));
	RightAim = MakeController(TEXT("RightAim"), TEXT("RightAim"));
	LeftAim = MakeController(TEXT("LeftAim"), TEXT("LeftAim"));

	LeftHand = CreateDefaultSubobject<USceneComponent>(TEXT("LeftHand"));
	LeftHand->SetupAttachment(LeftGrip);
	RightHand = CreateDefaultSubobject<USceneComponent>(TEXT("RightHand"));
	RightHand->SetupAttachment(RightGrip);

	auto MakeVisual = [this](const TCHAR* Name, USceneComponent* Parent)
	{
		UStaticMeshComponent* Visual = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Visual->SetupAttachment(Parent);
		Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Visual->SetCastShadow(false);
		Visual->SetGenerateOverlapEvents(false);
		return Visual;
	};
	LeftFlipper = MakeVisual(TEXT("LeftFlipper"), LeftHand);
	LeftFlipper->SetRelativeLocation(FVector(-3.0, 0.0, 0.0));
	RightFlipper = MakeVisual(TEXT("RightFlipper"), RightHand);
	RightFlipper->SetRelativeLocation(FVector(-3.0, 0.0, 0.0));

	// La interfaz, el puntero y los mandos siguen vivos aunque el juego esté en pausa.
	LeftGrip->PrimaryComponentTick.bTickEvenWhenPaused = true;
	RightGrip->PrimaryComponentTick.bTickEvenWhenPaused = true;
	RightAim->PrimaryComponentTick.bTickEvenWhenPaused = true;
	LeftAim->PrimaryComponentTick.bTickEvenWhenPaused = true;

	LaserBeam = MakeVisual(TEXT("LaserBeam"), RigRoot);
	LaserBeam->SetUsingAbsoluteLocation(true);
	LaserBeam->SetUsingAbsoluteRotation(true);
	LaserBeam->SetUsingAbsoluteScale(true);
	LaserBeam->SetVisibility(false);
	LaserDot = MakeVisual(TEXT("LaserDot"), RigRoot);
	LaserDot->SetUsingAbsoluteLocation(true);
	LaserDot->SetUsingAbsoluteRotation(true);
	LaserDot->SetUsingAbsoluteScale(true);
	LaserDot->SetVisibility(false);

	Pointer = CreateDefaultSubobject<UWidgetInteractionComponent>(TEXT("Pointer"));
	Pointer->SetupAttachment(RigRoot);
	Pointer->InteractionSource = EWidgetInteractionSource::Custom;
	Pointer->InteractionDistance = 2000.f;
	Pointer->bShowDebug = false;
	Pointer->bEnableHitTesting = true;
	// Usuario virtual propio: su foco y su captura no pisan los del jugador (teclado y mando).
	Pointer->VirtualUserIndex = 4;
	Pointer->PointerIndex = 0;
	Pointer->PrimaryComponentTick.bTickEvenWhenPaused = true;

	ScreenPanel = CreateDefaultSubobject<UWidgetComponent>(TEXT("ScreenPanel"));
	ScreenPanel->SetupAttachment(RigRoot);
	ScreenPanel->SetUsingAbsoluteLocation(true);
	ScreenPanel->SetUsingAbsoluteRotation(true);
	ScreenPanel->SetUsingAbsoluteScale(true);
	ScreenPanel->SetWidgetSpace(EWidgetSpace::World);
	ScreenPanel->SetDrawSize(FVector2D(UTN_VRScreenWidget::ScreenWidth, UTN_VRScreenWidget::ScreenHeight));
	ScreenPanel->SetPivot(FVector2D(0.5f, 0.5f));
	ScreenPanel->SetTwoSided(false);
	ScreenPanel->SetBlendMode(EWidgetBlendMode::Transparent);
	ScreenPanel->SetBackgroundColor(FLinearColor::Transparent);
	ScreenPanel->SetTickWhenOffscreen(true);
	ScreenPanel->SetWindowFocusable(true);
	ScreenPanel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ScreenPanel->SetGenerateOverlapEvents(false);
	ScreenPanel->SetCastShadow(false);
	ScreenPanel->SetTranslucentSortPriority(100);
	ScreenPanel->PrimaryComponentTick.bTickEvenWhenPaused = true;
	// El panel plano no se pinta: dibuja la interfaz en su textura (sigue «visible») y sirve al puntero; se ve el curvo.
	ScreenPanel->SetRenderInMainPass(false);
	ScreenPanel->SetRenderInDepthPass(false);

	CurvedPanel = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("CurvedPanel"));
	CurvedPanel->SetupAttachment(ScreenPanel);
	CurvedPanel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CurvedPanel->SetGenerateOverlapEvents(false);
	CurvedPanel->SetCastShadow(false);
	CurvedPanel->SetTranslucentSortPriority(100);
	CurvedPanel->bUseAsyncCooking = true;

	LoadingDome = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("LoadingDome"));
	LoadingDome->SetupAttachment(RigRoot);
	LoadingDome->SetUsingAbsoluteLocation(true);
	LoadingDome->SetUsingAbsoluteRotation(true);
	LoadingDome->SetUsingAbsoluteScale(true);
	LoadingDome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LoadingDome->SetGenerateOverlapEvents(false);
	LoadingDome->SetCastShadow(false);
	// Detrás de la interfaz (mismo orden de translúcidos, menos prioridad).
	LoadingDome->SetTranslucentSortPriority(50);
	LoadingDome->SetVisibility(false);
}

void ATN_VRRig::BeginPlay()
{
	Super::BeginPlay();
	Mode = TNVR::GetMode();
	BuildHands();
	BuildLaser();
	EnsureScreen();
	OnModeChanged(Mode);

	// Lo que alguien puso en el viewport antes de que existiera el rig (el mundo acaba de empezar) pasa al panel.
	TArray<UUserWidget*> Found;
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (Widget && Widget != Screen && Widget->GetWorld() == GetWorld() && Widget->IsInViewport())
		{
			Found.Add(Widget);
		}
	}
	for (UUserWidget* Widget : Found)
	{
		HostWidget(Widget, 0);
	}
	if (Found.Num() > 0)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[VR] %d widgets del viewport pasan al panel VR."), Found.Num());
	}
}

void ATN_VRRig::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bPointerDown)
	{
		PointerRelease();
	}
	ReleaseGrips(ViewTurtle.Get());
	StopHaptics(GetLocalPC());
	if (UTN_VRSeatComponent* Seat = ViewSeat.Get())
	{
		Seat->SetVRView(false, false);
	}
	ViewSeat = nullptr;
	RemoveVRMapping();

	if (Screen)
	{
		Screen->ReleaseAll(false);
	}
	// Apagado a mitad de partida (no al cambiar de mapa): la tortuga vuelve a su cámara de siempre.
	if (EndPlayReason == EEndPlayReason::Destroyed)
	{
		if (ATortugaCharacter* Turtle = ViewTurtle.Get())
		{
			Turtle->SetVRView(false, false);
		}
		if (bOwnsViewTarget)
		{
			if (APlayerController* PC = GetLocalPC())
			{
				PC->SetViewTarget(PC->GetPawn() ? static_cast<AActor*>(PC->GetPawn()) : static_cast<AActor*>(PC));
			}
		}
	}
	Super::EndPlay(EndPlayReason);
}

void ATN_VRRig::BuildHands()
{
	UStaticMesh* Flipper = TNVRRigDetail::BuildFlipperMesh(this);
	if (!Flipper)
	{
		return;
	}
	LeftFlipper->SetStaticMesh(Flipper);
	RightFlipper->SetStaticMesh(Flipper);
}

void ATN_VRRig::BuildLaser()
{
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* Basic = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (Basic)
	{
		LaserMaterial = UMaterialInstanceDynamic::Create(Basic, this);
		LaserMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.78f, 0.25f));
	}
	if (Cylinder)
	{
		LaserBeam->SetStaticMesh(Cylinder);
		if (LaserMaterial) { LaserBeam->SetMaterial(0, LaserMaterial); }
	}
	if (Sphere)
	{
		LaserDot->SetStaticMesh(Sphere);
		if (LaserMaterial) { LaserDot->SetMaterial(0, LaserMaterial); }
	}
}

void ATN_VRRig::EnsureScreen()
{
	if (Screen || !GetWorld())
	{
		return;
	}
	Screen = CreateWidget<UTN_VRScreenWidget>(GetWorld(), UTN_VRScreenWidget::StaticClass());
	if (Screen)
	{
		ScreenPanel->SetWidget(Screen);
	}
}

void ATN_VRRig::OnModeChanged(ETNVRMode NewMode)
{
	Mode = NewMode;
	const bool bHeadset = NewMode == ETNVRMode::Headset;
	// Con gafas los mandos se siguen solos; simulado, las aletas se quedan quietas delante de la cámara.
	LeftGrip->SetActive(bHeadset);
	RightGrip->SetActive(bHeadset);
	RightAim->SetActive(bHeadset);
	LeftAim->SetActive(bHeadset);
	if (!bHeadset)
	{
		LeftGrip->SetRelativeTransform(TNVRRigDetail::SimLeftHand);
		RightGrip->SetRelativeTransform(TNVRRigDetail::SimRightHand);
		RightAim->SetRelativeTransform(TNVRRigDetail::SimRightHand);
		LeftAim->SetRelativeTransform(TNVRRigDetail::SimLeftHand);
		RemoveVRMapping();
	}
	RigCamera->bLockToHmd = bHeadset;
	LaserBeam->SetVisibility(false);
	LaserDot->SetVisibility(false);
	bPanelPlaced = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Fotograma
// ─────────────────────────────────────────────────────────────────────────────

APlayerController* ATN_VRRig::GetLocalPC() const
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC && PC->IsLocalController() ? PC : nullptr;
}

void ATN_VRRig::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Mode = TNVR::GetMode();
	if (Mode == ETNVRMode::Off)
	{
		return;
	}
	EnsureScreen();
	APlayerController* PC = GetLocalPC();
	if (!PC)
	{
		// El que se ve es el curvo (hijo del plano): se ocultan los dos.
		ScreenPanel->SetVisibility(false, true);
		return;
	}

	// La tortuga propia en primera persona (y la que ya no es nuestra, de vuelta a la de siempre).
	ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(PC->GetPawn());
	if (Turtle && !Turtle->IsLocallyControlled())
	{
		Turtle = nullptr;
	}
	ATortugaCharacter* Previous = ViewTurtle.Get();
	if (Previous && Previous != Turtle)
	{
		ReleaseGrips(Previous);
		Previous->SetVRView(false, false);
	}
	if (Turtle)
	{
		Turtle->SetVRView(true, Mode == ETNVRMode::Headset);
	}
	ViewTurtle = Turtle;

	// Sentada en un vehículo propio (conductora o artillera): la vista va en su asiento.
	APawn* Pawn = PC->GetPawn();
	UTN_VRSeatComponent* Seat = !Turtle && Pawn && Pawn->IsLocallyControlled() ? UTN_VRSeatComponent::FindOn(Pawn) : nullptr;
	UTN_VRSeatComponent* PreviousSeat = ViewSeat.Get();
	if (PreviousSeat && PreviousSeat != Seat)
	{
		PreviousSeat->SetVRView(false, false);
	}
	if (Seat)
	{
		Seat->SetVRView(true, Mode == ETNVRMode::Headset);
	}
	ViewSeat = Seat;

	UpdateViewAttachment(PC, Turtle, Seat);
	// Los brazos del cuerpo solo siguen a los mandos si se ve desde la tortuga (no desde el probador o el espectador).
	UpdateHands(Turtle, Turtle && Turtle->IsLocalViewTarget(), DeltaSeconds);
	if (Seat)
	{
		UpdateSeatHands(PC, Seat);
	}

	// Hacia dónde apunta la aleta derecha: lanzar compañeros y objetos (con gafas; simulado, la cámara).
	if (Turtle)
	{
		if (Mode == ETNVRMode::Headset)
		{
			UMotionControllerComponent* AimSource = RightAim->IsTracked() ? RightAim.Get() : RightGrip.Get();
			Turtle->SetLocalVRAim(AimSource->GetComponentRotation(), AimSource->IsTracked());
		}
		else
		{
			Turtle->SetLocalVRAim(FRotator::ZeroRotator, false);
		}
	}

	// ¿Menú delante? El juego enseña el cursor con cualquier menú (tienda, probador, general, pausa, salas, campeón);
	// las ruedas de emotes y frases también lo enseñan, pero se manejan con el stick y siguen siendo HUD.
	const AMP_GamePlayerController* GamePC = Cast<AMP_GamePlayerController>(PC);
	const bool bWheelOpen = GamePC && GamePC->IsRadialWheelOpen();
	const bool bNowMenu = PC->ShouldShowMouseCursor() && !bWheelOpen && Screen && Screen->CountVisible() > 0;
	if (bNowMenu != bMenuMode)
	{
		bMenuMode = bNowMenu;
		bPanelPlaced = false;
		if (!bMenuMode && bPointerDown)
		{
			PointerRelease();
		}
	}

	UpdateInput(PC, Turtle, Seat, DeltaSeconds);
	UpdateGrips(PC, Turtle, DeltaSeconds);
	UpdatePanel(PC, DeltaSeconds);
	UpdatePointer(PC);
	UpdateLoadingDome(PC);
	UpdateComfortVignette(PC, Turtle, DeltaSeconds);
	UpdateHaptics(PC, Turtle);
}

void ATN_VRRig::UpdateViewAttachment(APlayerController* PC, ATortugaCharacter* Turtle, UTN_VRSeatComponent* Seat)
{
	const bool bHeadset = Mode == ETNVRMode::Headset;
	AActor* ViewTarget = PC->GetViewTarget();

	// Sin peón y mirando al propio PlayerController (menú principal, antes de aparecer): la vista es la del rig. Con gafas,
	// también si se mira a algo sin cámara (el peón por defecto de un menú): sin cámara no hay a quién ponerle la cabeza.
	const bool bPawnless = !PC->GetPawn() && (ViewTarget == PC || ViewTarget == nullptr);
	const bool bNoCameraView = bHeadset && ViewTarget && ViewTarget != this && !ViewTarget->IsA<ATortugaCharacter>()
		&& !TNVRRigDetail::FindActiveCamera(ViewTarget);
	if (bPawnless || bNoCameraView)
	{
		if (PC->GetViewTarget() != this)
		{
			FVector Location = GetActorLocation();
			FRotator Rotation = GetActorRotation();
			GetViewPoint(PC, Location, Rotation);
			RigRoot->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
			AttachedBase = nullptr;
			AttachedSocket = NAME_None;
			SetActorLocationAndRotation(Location, FRotator(0.0, Rotation.Yaw, 0.0));
			PC->SetViewTarget(this);
		}
		bOwnsViewTarget = true;
		ViewTarget = this;
	}
	else if (ViewTarget != this)
	{
		bOwnsViewTarget = false;
	}
	RigCamera->SetActive(ViewTarget == this);

	USceneComponent* Base = nullptr;
	FName Socket = NAME_None;
	if (ViewTarget != this)
	{
		if (Turtle && ViewTarget == Turtle && Turtle->GetVRCamera())
		{
			// Con gafas, el origen del seguimiento (la cámara se mueve con la cabeza dentro de él); simulado, la cámara.
			Base = bHeadset ? Turtle->GetVROrigin() : Turtle->GetVRCamera();
		}
		else if (Seat && ViewTarget == Seat->GetOwner() && Seat->GetVRCamera())
		{
			// Sentada en un vehículo: con gafas, el asiento es el origen del seguimiento; simulado, su cámara.
			Base = bHeadset ? static_cast<USceneComponent*>(Seat) : static_cast<USceneComponent*>(Seat->GetVRCamera());
		}
		else if (UCameraComponent* Camera = TNVRRigDetail::FindActiveCamera(ViewTarget))
		{
			// Otra cámara (espectador): con gafas su padre hace de origen del seguimiento (el motor le pone la pose de la
			// cabeza encima); simulado, ella misma.
			if (bHeadset)
			{
				Base = Camera->GetAttachParent();
				Socket = Camera->GetAttachSocketName();
			}
			else
			{
				Base = Camera;
			}
		}
	}
	if (Base != AttachedBase.Get() || Socket != AttachedSocket)
	{
		if (Base)
		{
			RigRoot->AttachToComponent(Base, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
		}
		else
		{
			RigRoot->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		}
		AttachedBase = Base;
		AttachedSocket = Socket;
		bPanelPlaced = false;
	}
	if (!Base && ViewTarget != this)
	{
		// Una vista sin cámara conocida: el rig donde está la cámara del juego.
		FVector Location;
		FRotator Rotation;
		if (GetViewPoint(PC, Location, Rotation))
		{
			SetActorLocationAndRotation(Location, bHeadset ? FRotator(0.0, Rotation.Yaw, 0.0) : Rotation);
		}
	}
}

void ATN_VRRig::UpdateHands(ATortugaCharacter* Turtle, bool bTurtleView, float DeltaSeconds)
{
	bool bLeftTracked = true;
	bool bRightTracked = true;
	if (Mode == ETNVRMode::Headset)
	{
		// Una aleta sin seguimiento (mando apagado, fuera de la vista de las cámaras) no cuenta.
		bLeftTracked = LeftGrip->IsTracked();
		bRightTracked = RightGrip->IsTracked();
	}
	else
	{
		// Simulado: quietas delante, con un vaivén muy suave para que se note que están vivas.
		const float Time = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.f;
		const FVector Bob(0.0, 0.0, FMath::Sin(Time * 1.7f) * 0.6);
		FTransform Left = TNVRRigDetail::SimLeftHand;
		Left.AddToTranslation(Bob);
		FTransform Right = TNVRRigDetail::SimRightHand;
		Right.AddToTranslation(-Bob);
		LeftGrip->SetRelativeTransform(Left);
		RightGrip->SetRelativeTransform(Right);
		RightAim->SetRelativeTransform(Right);
	}
	// Las manos no atraviesan el escenario: se quedan en la pared o en el suelo (y vibran al tocarlo).
	BlockHandsByWorld(Turtle, bTurtleView);
	// Con la tortuga, sus manos de verdad van a los mandos (IK de los brazos): lo que coge es su mano. Si la vista es otra
	// (probador, espectador), las manos no valen: el rig va con esa cámara y los brazos se quedan con su animación. Las
	// aletas sueltas solo se ven sin tortuga (menú principal, espectador), desde otra vista o dentro del caparazón, donde
	// el cuerpo propio no se pinta.
	if (Turtle)
	{
		Turtle->SetLocalVRHands(LeftHand->GetComponentLocation(), RightHand->GetComponentLocation(), bLeftTracked && bTurtleView,
			bRightTracked && bTurtleView);
	}
	// Sentada en un vehículo se ven los brazos de la tortuga sentada (siguen a los mandos): sin aletas sueltas.
	const bool bSeated = ViewSeat.IsValid() && ViewSeat->IsVRView();
	const bool bLooseFlippers = !bSeated && (!Turtle || !bTurtleView || Turtle->IsInShell());
	LeftFlipper->SetVisibility(bLeftTracked && bLooseFlippers);
	RightFlipper->SetVisibility(bRightTracked && bLooseFlippers);
}

void ATN_VRRig::UpdateSeatHands(APlayerController* PC, UTN_VRSeatComponent* Seat)
{
	const bool bHeadset = Mode == ETNVRMode::Headset;
	// Con un menú delante, las manos están en el menú: no agarran nada del vehículo.
	const bool bPlaying = bHeadset && !bMenuMode;
	auto MakeHand = [&](bool bRight)
	{
		FTNVRSeatHand Hand;
		const USceneComponent* Palm = bRight ? RightHand.Get() : LeftHand.Get();
		const UMotionControllerComponent* Grip = bRight ? RightGrip.Get() : LeftGrip.Get();
		const UMotionControllerComponent* Aim = bRight ? RightAim.Get() : LeftAim.Get();
		Hand.Location = Palm ? Palm->GetComponentLocation() : GetActorLocation();
		// La pose de apuntar del mando; sin ella, hacia donde va la aleta.
		Hand.AimDir = Aim && Aim->IsTracked() ? Aim->GetForwardVector() : (Palm ? Palm->GetForwardVector() : GetActorForwardVector());
		Hand.bTracked = !bHeadset || (Grip && Grip->IsTracked());
		Hand.Grip = bPlaying ? FMath::Max(PC->GetInputAnalogKeyState(bRight ? FTNVRKeys::RightGripAxis : FTNVRKeys::LeftGripAxis),
			PC->IsInputKeyDown(bRight ? FTNVRKeys::RightGrip : FTNVRKeys::LeftGrip) ? 1.f : 0.f) : 0.f;
		return Hand;
	};
	Seat->SetLocalHands(MakeHand(false), MakeHand(true));
}

void ATN_VRRig::UpdateInput(APlayerController* PC, ATortugaCharacter* Turtle, UTN_VRSeatComponent* Seat, float DeltaSeconds)
{
	// Sin giro suave mientras no se gira este fotograma (menú o rueda abiertos, sin gafas): si no, la viñeta de confort se
	// quedaría con el último giro.
	SmoothTurnRate = 0.f;
	if (Mode != ETNVRMode::Headset)
	{
		return;
	}
	// En un vehículo, los mandos son los suyos (UTN_BuggyInputSet y UTN_KartInputSet): los de la tortuga se quitan para que
	// no se queden los botones (A saltar, B caparazón...).
	if (Seat)
	{
		RemoveVRMapping();
	}
	else
	{
		EnsureVRMapping(PC);
	}

	// Clic del stick derecho: recentrar.
	const bool bRecenter = PC->IsInputKeyDown(FTNVRKeys::RightStickClick);
	if (bRecenter && !bRecenterHeld)
	{
		if (UTN_VRSubsystem* VR = UTN_VRSubsystem::Get(this))
		{
			VR->Recenter();
		}
	}
	bRecenterHeld = bRecenter;

	AMP_GamePlayerController* GamePC = Cast<AMP_GamePlayerController>(PC);
	// Sentada no se gira con el stick: hacia dónde se mira lo marca el vehículo.
	if (bMenuMode || (GamePC && GamePC->IsRadialWheelOpen()) || Seat)
	{
		bSnapLatched = true;
		return;
	}
	const float TurnAxis = PC->GetInputAnalogKeyState(FTNVRKeys::RightStickX);
	AActor* ViewTarget = PC->GetViewTarget();
	if (Turtle && ViewTarget == Turtle)
	{
		const UTN_GameSettingsSubsystem* Settings = UTN_GameSettingsSubsystem::Get(this);
		const uint8 TurnMode = Settings ? Settings->GetSettings().VRTurn : 0;
		if (TurnMode == 2)
		{
			// Giro suave.
			if (FMath::Abs(TurnAxis) > 0.2f)
			{
				SmoothTurnRate = TurnAxis * CVarTNVRSmoothTurnSpeed.GetValueOnGameThread();
				Turtle->AddVRYaw(SmoothTurnRate * DeltaSeconds);
			}
			return;
		}
		const int32 Step = TNVRMath::SnapTurnStep(TurnAxis, bSnapLatched);
		if (Step != 0)
		{
			Turtle->AddVRYaw(Step * (TurnMode == 1 ? 45.f : 30.f));
		}
		return;
	}
	// Mirando a otra tortuga (espectador): el stick derecho cambia de tortuga.
	if (GamePC && ViewTarget && ViewTarget != PC->GetPawn() && ViewTarget->IsA<ATortugaCharacter>())
	{
		const int32 Step = TNVRMath::SnapTurnStep(TurnAxis, bSnapLatched);
		if (Step > 0) { GamePC->SpectateNextPlayer(); }
		else if (Step < 0) { GamePC->SpectatePreviousPlayer(); }
	}
}

UInputTrigger* ATN_VRRig::MakeAnalogPressTrigger(UObject* Outer)
{
	// Pulsado al 55 % y suelto por debajo del 35 % (con histéresis: un gatillo que ronda el umbral no corta lo que se mantiene).
	return NewObject<UTN_InputTriggerAnalogDown>(Outer ? Outer : GetTransientPackage());
}

void ATN_VRRig::EnsureVRMapping(APlayerController* PC)
{
	ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Input)
	{
		return;
	}
	if (!VRMapping)
	{
		using namespace TNVRRigDetail;
		VRMapping = NewObject<UInputMappingContext>(this, TEXT("IMC_VR"), RF_Transient);
		auto Map = [this](UInputAction* Action, const FKey& Key, bool bSwizzle)
		{
			if (!Action || !Key.IsValid())
			{
				return;
			}
			FEnhancedActionKeyMapping& Mapping = VRMapping->MapKey(Action, Key);
			if (Action->ValueType != EInputActionValueType::Boolean)
			{
				Mapping.Modifiers.Add(NewObject<UInputModifierDeadZone>(VRMapping));
			}
			if (bSwizzle)
			{
				// El eje Y del stick va a la Y de la acción de dos ejes.
				Mapping.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(VRMapping));
			}
		};
		// Botón analógico (gatillo de los Touch): cuenta como pulsado a partir del 55 % (OpenXR solo da su valor), con un
		// disparador «Down» con ese umbral en la asignación. Sin disparadores, una acción se activa con cualquier valor
		// distinto de 0, y una zona muerta no hace nada en las acciones booleanas (IA_Interact, IA_OpenChatWheel).
		auto MapAnalogButton = [this](UInputAction* Action, const FKey& Key)
		{
			if (!Action || !Key.IsValid())
			{
				return;
			}
			FEnhancedActionKeyMapping& Mapping = VRMapping->MapKey(Action, Key);
			Mapping.Triggers.Add(MakeAnalogPressTrigger(VRMapping));
		};
		UInputAction* Move = LoadAction(TEXT("IA_Move"));
		Map(Move, FTNVRKeys::LeftStickX, false);
		Map(Move, FTNVRKeys::LeftStickY, true);
		Map(LoadAction(TEXT("IA_Jump")), FTNVRKeys::A, false);
		Map(LoadAction(TEXT("IA_Shell")), FTNVRKeys::B, false);
		UInputAction* Interact = LoadAction(TEXT("IA_Interact"));
		Map(Interact, FTNVRKeys::RightTrigger, false);
		MapAnalogButton(Interact, FTNVRKeys::RightTriggerAxis);
		// Agarres: UpdateGrips (coger con la mano, lanzar con el gesto, soltar; el izquierdo sin nada que coger, correr).
		SprintAction = LoadAction(TEXT("IA_Sprint"));
		Map(LoadAction(TEXT("IA_RotateInventory")), FTNVRKeys::X, false);
		Map(LoadAction(TEXT("IA_OpenEmoteWheel")), FTNVRKeys::Y, false);
		UInputAction* ChatWheel = LoadAction(TEXT("IA_OpenChatWheel"));
		Map(ChatWheel, FTNVRKeys::LeftTrigger, false);
		MapAnalogButton(ChatWheel, FTNVRKeys::LeftTriggerAxis);
		UInputAction* Radial = LoadAction(TEXT("IA_RadialNavigate"));
		Map(Radial, FTNVRKeys::RightStickX, false);
		Map(Radial, FTNVRKeys::RightStickY, true);
		UE_LOG(LogTortunabo, Log, TEXT("[VR] Mandos VR: %d asignaciones sobre las acciones del juego."), VRMapping->GetMappings().Num());
	}
	// La tortuga y los ajustes rehacen los mapeos al poseer o al cambiar teclas: se vuelve a poner si falta.
	if (!Input->HasMappingContext(VRMapping))
	{
		FModifyContextOptions Options;
		Options.bIgnoreAllPressedKeysUntilRelease = false;
		Input->AddMappingContext(VRMapping, 10, Options);
	}
	MappedPC = PC;
}

void ATN_VRRig::RemoveVRMapping()
{
	APlayerController* PC = MappedPC.Get();
	ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (Input && VRMapping && Input->HasMappingContext(VRMapping))
	{
		Input->RemoveMappingContext(VRMapping);
	}
	MappedPC = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Panel de la interfaz
// ─────────────────────────────────────────────────────────────────────────────

bool ATN_VRRig::GetViewPoint(APlayerController* PC, FVector& OutLocation, FRotator& OutRotation) const
{
	const APlayerCameraManager* Camera = PC ? PC->PlayerCameraManager.Get() : nullptr;
	if (!Camera)
	{
		return false;
	}
	OutLocation = Camera->GetCameraLocation();
	OutRotation = Camera->GetCameraRotation();
	return true;
}

float ATN_VRRig::FitDistance(const FVector& From, const FVector& Dir, float Desired) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return Desired;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TNVRPanelFit), false, this);
	AddViewIgnores(Params);
	FHitResult Hit;
	const bool bBlocked = World->LineTraceSingleByChannel(Hit, From, From + Dir * Desired, ECC_Visibility, Params);
	return TNVRMath::PanelDistance(Desired, bBlocked, bBlocked ? static_cast<float>(Hit.Distance) : Desired);
}

void ATN_VRRig::AddViewIgnores(FCollisionQueryParams& Params) const
{
	const APlayerController* PC = GetLocalPC();
	if (!PC)
	{
		return;
	}
	Params.AddIgnoredActor(PC->GetPawn());
	Params.AddIgnoredActor(PC->GetViewTarget());
	// Sentada: el vehículo entero (la artillera va enganchada al buggy).
	if (const UTN_VRSeatComponent* Seat = ViewSeat.Get(); Seat && Seat->GetOwner())
	{
		Params.AddIgnoredActor(Seat->GetOwner());
		if (const AActor* Vehicle = Seat->GetOwner()->GetAttachParentActor())
		{
			Params.AddIgnoredActor(Vehicle);
		}
	}
	// Lo que se lleva en las manos (objetos con física) no empuja el HUD contra la cara.
	const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(PC->GetPawn());
	if (const UTN_VRGrabComponent* Grab = Turtle ? Turtle->GetVRGrabComponent() : nullptr)
	{
		for (int32 Hand = 0; Hand < 2; ++Hand)
		{
			if (const UPrimitiveComponent* Held = Grab->GetHeld(Hand))
			{
				Params.AddIgnoredComponent(Held);
			}

		}
	}
}

float ATN_VRRig::FitHudDistance(const FVector& From, const FRotator& ViewRotation, float Desired, float ArcDeg) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return Desired;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TNVRHudFit), false, this);
	AddViewIgnores(Params);
	// El centro, los lados y el borde de abajo (lo que antes tapa el suelo al mirar abajo): el HUD se acerca lo que haga
	// falta para que ninguno quede detrás del escenario.
	static const FVector2D Probes[] = { { 0.5, 0.5 }, { 0.0, 0.5 }, { 1.0, 0.5 }, { 0.5, 1.0 }, { 0.0, 1.0 }, { 1.0, 1.0 }, { 0.5, 0.0 } };
	const float Aspect = static_cast<float>(UTN_VRScreenWidget::ScreenHeight) / static_cast<float>(UTN_VRScreenWidget::ScreenWidth);
	const FQuat ViewQuat = ViewRotation.Quaternion();
	float Allowed = Desired;
	bool bBlocked = false;
	for (const FVector2D& Probe : Probes)
	{
		const FVector Local = TNVRHands::HudProbeDirection(ArcDeg, Aspect, static_cast<float>(Probe.X), static_cast<float>(Probe.Y));
		// Lo que hay que mirar en esa dirección: hasta donde llegaría el panel a la distancia deseada.
		const float Reach = Desired / FMath::Max(0.2f, static_cast<float>(FVector(Local.X, Local.Y, 0.0).Size()));
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, From, From + ViewQuat.RotateVector(Local) * Reach, ECC_Visibility, Params))
		{
			bBlocked = true;
			Allowed = FMath::Min(Allowed, TNVRHands::HudRadiusForHit(Local, static_cast<float>(Hit.Distance)));
		}
	}
	return TNVRMath::PanelDistance(Desired, bBlocked, Allowed);
}

void ATN_VRRig::PlacePanel(const FVector& ViewLocation, const FRotator& Direction, float Distance, float HorizontalFov, float DropFraction)
{
	const FVector Dir = Direction.Vector();
	// DropFraction de la distancia por debajo de los ojos (con gafas, un poco: se lee sin levantar la vista).
	const FVector Location = ViewLocation + Dir * Distance - FVector(0.0, 0.0, Distance * DropFraction);
	// +X del panel hacia los ojos (su cara de delante).
	const FRotator Facing = (ViewLocation - Location).Rotation();
	ScreenPanel->SetWorldLocationAndRotation(Location, Facing);
	// Curvo, con el eje del cilindro en los ojos: todo el panel queda a la misma distancia y te rodea.
	ScreenPanel->SetWorldScale3D(FVector(TNVRMath::CurvedPanelScale(Distance, HorizontalFov, UTN_VRScreenWidget::ScreenWidth)));
	UpdateCurvedPanel(HorizontalFov);
}

void ATN_VRRig::UpdatePanel(APlayerController* PC, float DeltaSeconds)
{
	FVector ViewLocation;
	FRotator ViewRotation;
	if (!Screen || !GetViewPoint(PC, ViewLocation, ViewRotation))
	{
		return;
	}
	const bool bPanelVisible = Screen->CountVisible() > 0;
	ScreenPanel->SetVisibility(bPanelVisible);
	CurvedPanel->SetVisibility(bPanelVisible);

	if (bMenuMode)
	{
		DetachPanelFromCamera();
		// Menú: quieto delante, donde miraba la cabeza al abrirse (se vuelve a poner si la vista se va lejos).
		if (!bPanelPlaced || FVector::Dist(ViewLocation, MenuPlacedFrom) > 150.0)
		{
			const float Desired = CVarTNVRMenuDistance.GetValueOnGameThread();
			float Arc = CVarTNVRMenuFov.GetValueOnGameThread();
			// Con gafas: de pie delante y un poco por debajo de los ojos (se lee sin levantar la vista), rodeándote.
			FRotator Direction(0.0, ViewRotation.Yaw, 0.0);
			float Drop = 0.1f;
			if (Mode != ETNVRMode::Headset)
			{
				// Sin gafas la imagen es la de la ventana: el menú, centrado en la vista (también mirando arriba o abajo) y
				// con el arco que cabe con el campo de visión de la cámara (con 90° y 16:9, unos 76°).
				int32 SizeX = 0;
				int32 SizeY = 0;
				PC->GetViewportSize(SizeX, SizeY);
				const float Aspect = SizeX > 0 && SizeY > 0 ? static_cast<float>(SizeX) / static_cast<float>(SizeY) : 16.f / 9.f;
				const float Fov = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetFOVAngle() : 90.f;
				Direction = FRotator(ViewRotation.Pitch, ViewRotation.Yaw, 0.0);
				Drop = 0.f;
				Arc = TNVRMath::SimulatedMenuArc(Arc, Fov, Aspect, Drop);
			}
			const float Distance = FitDistance(ViewLocation, Direction.Vector(), Desired);
			PlacePanel(ViewLocation, Direction, Distance, Arc, Drop);
			MenuPlacedFrom = ViewLocation;
			HudYaw = static_cast<float>(ViewRotation.Yaw);
			PanelDistanceSmoothed = Distance;
			bPanelPlaced = true;
		}
		return;
	}

	// HUD: anclado a la cámara (siempre fijo en la vista, grande como el de la pantalla y curvo alrededor de los ojos).
	// Con TN.VR.HudFollow 1, suelto delante y siguiendo a la cabeza con retraso (se lee mirando de reojo).
	const float HudArc = CVarTNVRHudFov.GetValueOnGameThread();
	const float HudDistance = CVarTNVRHudDistance.GetValueOnGameThread();
	UCameraComponent* ViewCamera = CVarTNVRHudFollow.GetValueOnGameThread() == 0 ? GetViewCamera(PC) : nullptr;
	if (!bPanelPlaced)
	{
		HudYaw = static_cast<float>(ViewRotation.Yaw);
		PanelDistanceSmoothed = HudDistance;
		bHudFollowing = false;
		bPanelPlaced = true;
	}
	if (ViewCamera)
	{
		// Si hay una pared delante (o el suelo bajo su borde de abajo), se acerca de golpe; se aleja poco a poco.
		const float Wanted = FitHudDistance(ViewLocation, ViewRotation, HudDistance, HudArc);
		PanelDistanceSmoothed = Wanted < PanelDistanceSmoothed ? Wanted : FMath::FInterpTo(PanelDistanceSmoothed, Wanted, DeltaSeconds, 4.f);
		AttachPanelToCamera(ViewCamera);
		// Colgado de la cámara (así lo mueve también la última pose de las gafas): delante, con su cara (+X) hacia los ojos.
		ScreenPanel->SetRelativeLocationAndRotation(FVector(PanelDistanceSmoothed, 0.0, 0.0), FRotator(0.0, 180.0, 0.0));
		ScreenPanel->SetRelativeScale3D(FVector(TNVRMath::CurvedPanelScale(PanelDistanceSmoothed, HudArc, UTN_VRScreenWidget::ScreenWidth)));
		UpdateCurvedPanel(HudArc);
		return;
	}
	DetachPanelFromCamera();
	if (Mode == ETNVRMode::Headset)
	{
		HudYaw = TNVRMath::LazyFollowYaw(HudYaw, static_cast<float>(ViewRotation.Yaw), DeltaSeconds, bHudFollowing);
	}
	else
	{
		HudYaw = static_cast<float>(ViewRotation.Yaw);
	}
	const FRotator HudRotation(Mode == ETNVRMode::Headset ? 0.0 : ViewRotation.Pitch, HudYaw, 0.0);
	const float Wanted = FitDistance(ViewLocation, HudRotation.Vector(), HudDistance);
	PanelDistanceSmoothed = Wanted < PanelDistanceSmoothed ? Wanted : FMath::FInterpTo(PanelDistanceSmoothed, Wanted, DeltaSeconds, 4.f);
	PlacePanel(ViewLocation, FRotator(0.0, HudYaw, 0.0), PanelDistanceSmoothed, HudArc);
}

UCameraComponent* ATN_VRRig::GetViewCamera(APlayerController* PC) const
{
	AActor* ViewTarget = PC ? PC->GetViewTarget() : nullptr;
	if (!ViewTarget)
	{
		return nullptr;
	}
	if (ViewTarget == this)
	{
		return RigCamera;
	}
	if (const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(ViewTarget))
	{
		if (Turtle->IsVRView() && Turtle->GetVRCamera())
		{
			return Turtle->GetVRCamera();
		}
	}
	return TNVRRigDetail::FindActiveCamera(ViewTarget);
}

void ATN_VRRig::AttachPanelToCamera(UCameraComponent* Camera)
{
	if (!Camera || ScreenPanel->GetAttachParent() == Camera)
	{
		return;
	}
	ScreenPanel->SetUsingAbsoluteLocation(false);
	ScreenPanel->SetUsingAbsoluteRotation(false);
	ScreenPanel->SetUsingAbsoluteScale(false);
	ScreenPanel->AttachToComponent(Camera, FAttachmentTransformRules::KeepRelativeTransform);
}

void ATN_VRRig::DetachPanelFromCamera()
{
	if (ScreenPanel->GetAttachParent() == RigRoot)
	{
		return;
	}
	// De vuelta al rig y suelto en el mundo (los menús se quedan quietos donde se abren).
	ScreenPanel->AttachToComponent(RigRoot, FAttachmentTransformRules::KeepWorldTransform);
	ScreenPanel->SetUsingAbsoluteLocation(true);
	ScreenPanel->SetUsingAbsoluteRotation(true);
	ScreenPanel->SetUsingAbsoluteScale(true);
}

void ATN_VRRig::RecenterPanel()
{
	bPanelPlaced = false;
	bHudFollowing = false;
	// Al recentrar, las gafas mueven el seguimiento de golpe: la velocidad de las manos empieza de cero.
	ResetHandVelocity();
}

// ─────────────────────────────────────────────────────────────────────────────
// Puntero
// ─────────────────────────────────────────────────────────────────────────────

bool ATN_VRRig::GetPointerRay(APlayerController* PC, FVector& OutOrigin, FVector& OutDir) const
{
	if (Mode == ETNVRMode::Headset)
	{
		UMotionControllerComponent* Source = RightAim->IsTracked() ? RightAim.Get() : RightGrip.Get();
		if (!Source->IsTracked())
		{
			return false;
		}
		OutOrigin = Source->GetComponentLocation();
		OutDir = Source->GetForwardVector();
		return true;
	}
	// Simulado: el ratón (el juego enseña el cursor con el menú).
	return PC && PC->DeprojectMousePositionToWorld(OutOrigin, OutDir);
}

void ATN_VRRig::UpdatePointer(APlayerController* PC)
{
	const bool bHeadset = Mode == ETNVRMode::Headset;
	bool bHit = false;
	FVector HitPoint = FVector::ZeroVector;
	FVector Origin = FVector::ZeroVector;
	FVector Dir = FVector::ForwardVector;
	FVector FlatPoint = FVector::ZeroVector;
	FVector2D UV = FVector2D::ZeroVector;
	bool bHasRay = false;
	if (bMenuMode && ScreenPanel->IsVisible())
	{
		bHasRay = GetPointerRay(PC, Origin, Dir);
		const FVector2D Size(UTN_VRScreenWidget::ScreenWidth, UTN_VRScreenWidget::ScreenHeight);
		bHit = bHasRay && TNVRMath::RayCurvedPanelHit(Origin, Dir, ScreenPanel->GetComponentTransform(), Size, PanelArc, HitPoint, UV);
		if (bHit)
		{
			// El puntero del motor mira el panel plano (invisible): el mismo punto de la interfaz, en su plano.
			FlatPoint = ScreenPanel->GetComponentTransform().TransformPosition(FVector(0.0, (0.5 - UV.X) * Size.X, (0.5 - UV.Y) * Size.Y));
		}
	}
	// Una vibración muy corta al entrar el láser en algo que se puede pulsar.
	const bool bOverButton = bHit && bHeadset && Pointer->IsOverInteractableWidget();
	if (bOverButton && !bPointerOverButton)
	{
		PulseHaptic(1, TNVRHands::Haptics::Hover);
	}
	bPointerOverButton = bOverButton;
	if (bHit)
	{
		FHitResult Hit;
		Hit.bBlockingHit = true;
		Hit.Location = FlatPoint;
		Hit.ImpactPoint = FlatPoint;
		Hit.Normal = ScreenPanel->GetForwardVector();
		Hit.ImpactNormal = Hit.Normal;
		Hit.TraceStart = Origin;
		Hit.TraceEnd = Origin + Dir * Pointer->InteractionDistance;
		Hit.Distance = static_cast<float>(FVector::Dist(Origin, FlatPoint));
		Hit.Component = ScreenPanel.Get();
		Hit.HitObjectHandle = FActorInstanceHandle(this);
		Pointer->SetCustomHitResult(Hit);
	}
	else
	{
		Pointer->SetCustomHitResult(FHitResult());
	}

	// El láser, solo con gafas (simulado apunta el cursor del ratón).
	const bool bShowLaser = bHeadset && bMenuMode && bHasRay;
	LaserBeam->SetVisibility(bShowLaser);
	LaserDot->SetVisibility(bShowLaser && bHit);
	if (bShowLaser)
	{
		const FVector End = bHit ? HitPoint : Origin + Dir * 300.0;
		const double Length = FMath::Max(1.0, FVector::Dist(Origin, End));
		LaserBeam->SetWorldLocationAndRotation((Origin + End) * 0.5, FRotationMatrix::MakeFromZ(End - Origin).Rotator());
		// El cilindro básico mide 100 × 100: 0,5 cm de grueso y el largo del rayo.
		LaserBeam->SetWorldScale3D(FVector(0.005, 0.005, Length / 100.0));
		if (bHit)
		{
			LaserDot->SetWorldLocation(HitPoint);
			LaserDot->SetWorldScale3D(FVector(bPointerDown ? 0.03 : 0.022));
		}
	}
}

void ATN_VRRig::PointerPress()
{
	if (!bMenuMode || bPointerDown)
	{
		return;
	}
	bPointerDown = true;
	Pointer->PressPointerKey(EKeys::LeftMouseButton);
	PulseHaptic(1, TNVRHands::Haptics::Click);

}

void ATN_VRRig::PointerRelease()
{
	if (!bPointerDown)
	{
		return;
	}
	bPointerDown = false;
	Pointer->ReleasePointerKey(EKeys::LeftMouseButton);
}

void ATN_VRRig::PointerScroll(float Delta)
{
	if (bMenuMode)
	{
		Pointer->ScrollWheel(Delta);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Pantalla
// ─────────────────────────────────────────────────────────────────────────────

bool ATN_VRRig::HostWidget(UUserWidget* Widget, int32 ZOrder, bool bPlayerScreen)
{
	EnsureScreen();
	return Screen && Screen->Host(Widget, ZOrder, bPlayerScreen);
}

bool ATN_VRRig::IsHosting(const UUserWidget* Widget) const
{
	return Screen && Screen->IsHosting(Widget);
}

bool ATN_VRRig::HostSlate(const TSharedRef<SWidget>& Widget, int32 ZOrder)
{
	EnsureScreen();
	return Screen && Screen->HostSlate(Widget, ZOrder);
}

bool ATN_VRRig::UnhostSlate(const TSharedRef<SWidget>& Widget)
{
	return Screen && Screen->UnhostSlate(Widget);
}

void ATN_VRRig::ReleaseScreenToViewport()
{
	if (Screen)
	{
		Screen->ReleaseAll(true);
	}
}

USceneComponent* ATN_VRRig::GetHand(bool bRight) const
{
	return bRight ? RightHand.Get() : LeftHand.Get();
}

// ─────────────────────────────────────────────────────────────────────────────
// Panel curvo y playa en 360 de la carga
// ─────────────────────────────────────────────────────────────────────────────

void ATN_VRRig::UpdateCurvedPanel(float ArcDeg)
{
	PanelArc = ArcDeg;
	if (!FMath::IsNearlyEqual(CurvedArcBuilt, ArcDeg, 0.01f))
	{
		// Un trozo de cilindro en el espacio del panel (TNVRMath::CurvedPanelPoint), con la UV de la interfaz. Los
		// triángulos van en los dos sentidos: el material del panel es de una cara y así se ve desde donde toca.
		const FVector2D Size(UTN_VRScreenWidget::ScreenWidth, UTN_VRScreenWidget::ScreenHeight);
		const double Radius = TNVRMath::CurvedPanelRadius(Size, ArcDeg);
		constexpr int32 Segments = 32;
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FLinearColor> Colors;
		const TArray<FProcMeshTangent> NoTangents;
		for (int32 i = 0; i <= Segments; ++i)
		{
			const double U = static_cast<double>(i) / Segments;
			for (int32 Row = 0; Row < 2; ++Row)
			{
				const FVector2D UV(U, static_cast<double>(Row));
				const FVector Point = TNVRMath::CurvedPanelPoint(UV, Size, ArcDeg);
				Vertices.Add(Point);
				Normals.Add(FVector(Radius - Point.X, -Point.Y, 0.0).GetSafeNormal());
				UVs.Add(UV);
				Colors.Add(FLinearColor::White);
			}
		}
		for (int32 i = 0; i < Segments; ++i)
		{
			const int32 TopA = i * 2;
			const int32 BottomA = TopA + 1;
			const int32 TopB = TopA + 2;
			const int32 BottomB = TopA + 3;
			Triangles.Append({ TopA, BottomA, TopB, TopB, BottomA, BottomB });
			Triangles.Append({ TopA, TopB, BottomA, TopB, BottomB, BottomA });
		}
		CurvedPanel->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, NoTangents, false);
		CurvedArcBuilt = ArcDeg;
	}
	// El material del panel (con la textura de la interfaz); lo crea el UWidgetComponent al dibujar la primera vez.
	if (UMaterialInstanceDynamic* Material = ScreenPanel->GetMaterialInstance())
	{
		if (CurvedPanel->GetMaterial(0) != Material)
		{
			CurvedPanel->SetMaterial(0, Material);
		}
	}
}

void ATN_VRRig::BuildLoadingDome()
{
	// Esfera de radio 1 vista desde dentro, con el color de vértice de la playa (cielo, horizonte, mar y arena).
	constexpr int32 Rings = 18;
	constexpr int32 Sides = 36;
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	const TArray<FProcMeshTangent> NoTangents;
	for (int32 r = 0; r <= Rings; ++r)
	{
		const double Theta = PI * r / Rings;
		const double Z = FMath::Cos(Theta);
		const double Ring = FMath::Sin(Theta);
		const FLinearColor Color = TNVRRigDetail::DomeColor(Z);
		for (int32 s = 0; s <= Sides; ++s)
		{
			const double Phi = 2.0 * PI * s / Sides;
			const FVector Point(Ring * FMath::Cos(Phi), Ring * FMath::Sin(Phi), Z);
			Vertices.Add(Point);
			Normals.Add(-Point);
			UVs.Add(FVector2D(static_cast<double>(s) / Sides, static_cast<double>(r) / Rings));
			Colors.Add(Color);
		}
	}
	for (int32 r = 0; r < Rings; ++r)
	{
		for (int32 s = 0; s < Sides; ++s)
		{
			const int32 A = r * (Sides + 1) + s;
			const int32 B = A + Sides + 1;
			Triangles.Append({ A, B, A + 1, A + 1, B, B + 1 });
			Triangles.Append({ A, A + 1, B, A + 1, B + 1, B });
		}
	}
	LoadingDome->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, NoTangents, false);
	LoadingDome->SetMaterial(0, TNVRRigDetail::DomeMaterial());
}

void ATN_VRRig::UpdateLoadingDome(APlayerController* PC)
{
	const float Radius = CVarTNVRLoadingDome.GetValueOnGameThread();
	const UGameInstance* GameInstance = GetGameInstance();
	const UTN_LoadingScreenSubsystem* Loading = GameInstance ? GameInstance->GetSubsystem<UTN_LoadingScreenSubsystem>() : nullptr;
	const bool bShow = Radius > 1.f && Loading && Loading->IsShowing();
	FVector ViewLocation;
	FRotator ViewRotation;
	if (!bShow || !GetViewPoint(PC, ViewLocation, ViewRotation))
	{
		LoadingDome->SetVisibility(false);
		return;
	}
	if (LoadingDome->GetNumSections() == 0)
	{
		BuildLoadingDome();
	}
	// Alrededor de la cabeza, sin girar con ella: la playa se queda quieta como un mapa.
	LoadingDome->SetWorldLocationAndRotation(ViewLocation, FRotator::ZeroRotator);
	LoadingDome->SetWorldScale3D(FVector(Radius));
	LoadingDome->SetVisibility(true);
}
