#include "Lobby/TN_CosmeticPreview.h"
#include "Art/TN_Art.h"
#include "Art/TN_ArtMeshComponent.h"
#include "Core/TN_CosmeticLook.h"
#include "Animation/AnimationAsset.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "../World/ProcMap/TN_ProcMapRuntimeMesh.h"

namespace TNPreviewDetail
{
	/** Donde vive el escaparate: muy alto, lejos de todo. */
	const FVector StageLocation(0.0, 0.0, 60000.0);
	constexpr float TurtleScale = 2.5f;
	constexpr float PedestalTop = 12.f;
	constexpr float AutoSpinSpeed = 24.f;
	constexpr int32 LiveSize = 1024;
	constexpr int32 ThumbSize = 192;
	constexpr float CaptureFOV = 30.f;

	/** Color sRGB 0xRRGGBB para las mallas en ejecución (MakeStaticMesh decodifica una vez más: así llega lineal). */
	FLinearColor Pal(uint32 Hex)
	{
		return FLinearColor(TNProcRuntimeMesh::SRGBToLinear(((Hex >> 16) & 255) / 255.f), TNProcRuntimeMesh::SRGBToLinear(((Hex >> 8) & 255) / 255.f),
			TNProcRuntimeMesh::SRGBToLinear((Hex & 255) / 255.f), 1.f);
	}

	/** Solo el canal de luz 1: el sol y las luces del nivel (canal 0) no tocan el escaparate. */
	FLightingChannels StudioChannel()
	{
		FLightingChannels Channels;
		Channels.bChannel0 = false;
		Channels.bChannel1 = true;
		Channels.bChannel2 = false;
		return Channels;
	}

	void SetupStudioPrimitive(UPrimitiveComponent* Prim)
	{
		Prim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Prim->SetVisibleInSceneCaptureOnly(true);
		Prim->SetCastShadow(true);
		Prim->LightingChannels = StudioChannel();
	}

	/**
	 * La malla de arte de la peana (si la hay) es otro componente del escaparate: solo se ve en la captura, con las mismas
	 * luces y sombras de estudio, y entra en la lista de lo que dibuja el captor (el componente generado deja de dibujarse).
	 */
	void AddStudioArt(const UStaticMeshComponent* Base, USceneCaptureComponent2D* Cap)
	{
		for (USceneComponent* Child : Base->GetAttachChildren())
		{
			if (UTN_ArtMeshComponent* Art = Cast<UTN_ArtMeshComponent>(Child))
			{
				SetupStudioPrimitive(Art);
				Cap->ShowOnlyComponent(Art);
			}
		}
	}

	void SetupStudioLight(UPointLightComponent* Light, float Candelas, const FLinearColor& Color)
	{
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetIntensity(Candelas);
		Light->SetLightColor(Color);
		Light->SetAttenuationRadius(1600.f);
		Light->SetSourceRadius(40.f);
		Light->SetCastShadows(true);
		Light->LightingChannels = StudioChannel();
	}

	void SetupCapture(USceneCaptureComponent2D* Cap)
	{
		Cap->bCaptureEveryFrame = false;
		Cap->bCaptureOnMovement = false;
		Cap->bAlwaysPersistRenderingState = true;
		Cap->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		Cap->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
		Cap->FOVAngle = CaptureFOV;
		Cap->ShowFlags.SetFog(false);
		Cap->ShowFlags.SetVolumetricFog(false);
		Cap->ShowFlags.SetAtmosphere(false);
		Cap->ShowFlags.SetMotionBlur(false);
	}

	FRotator LookRotation(const FVector& From, const FVector& To)
	{
		return (To - From).Rotation();
	}
}

ATN_CosmeticPreview::ATN_CosmeticPreview()
{
	using namespace TNPreviewDetail;
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	SetCanBeDamaged(false);

	StageRoot = CreateDefaultSubobject<USceneComponent>(TEXT("StageRoot"));
	SetRootComponent(StageRoot);

	Turntable = CreateDefaultSubobject<USceneComponent>(TEXT("Turntable"));
	Turntable->SetupAttachment(StageRoot);

	Pedestal = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pedestal"));
	Pedestal->SetupAttachment(Turntable);
	SetupStudioPrimitive(Pedestal);

	Turtle = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Turtle"));
	Turtle->SetupAttachment(Turntable);
	// Como en BP_TortugaCharacter: la malla mira a su +Y; girada -90 mira al +X del escaparate (hacia la cámara).
	Turtle->SetRelativeLocation(FVector(0.f, 0.f, PedestalTop));
	Turtle->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	Turtle->SetRelativeScale3D(FVector(TurtleScale));
	Turtle->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	SetupStudioPrimitive(Turtle);
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> TurtleMesh(TEXT("/Game/Meshses/Characters/Player/TotugaDemo_Rig.TotugaDemo_Rig"));
	if (TurtleMesh.Succeeded()) { Turtle->SetSkeletalMeshAsset(TurtleMesh.Object); }

	Helmet = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Helmet"));
	Helmet->SetupAttachment(Turtle);
	SetupStudioPrimitive(Helmet);

	// Cámara de frente, un pelín por encima: la tortuga (133 cm) y un sombrero alto caben con aire.
	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(StageRoot);
	const FVector CamPos(430.f, 0.f, 112.f);
	Capture->SetRelativeLocation(CamPos);
	Capture->SetRelativeRotation(LookRotation(CamPos, FVector(0.f, 0.f, 80.f)));
	SetupCapture(Capture);

	ThumbCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("ThumbCapture"));
	ThumbCapture->SetupAttachment(StageRoot);
	SetupCapture(ThumbCapture);

	// Luces de estudio: principal cálida arriba a la izquierda, relleno frío a la derecha y contraluz por detrás.
	KeyLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("KeyLight"));
	KeyLight->SetupAttachment(StageRoot);
	KeyLight->SetRelativeLocation(FVector(300.f, 230.f, 300.f));
	SetupStudioLight(KeyLight, 60.f, FLinearColor(1.f, 0.93f, 0.82f));

	FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
	FillLight->SetupAttachment(StageRoot);
	FillLight->SetRelativeLocation(FVector(280.f, -300.f, 120.f));
	SetupStudioLight(FillLight, 22.f, FLinearColor(0.78f, 0.9f, 1.f));
	FillLight->SetCastShadows(false);

	RimLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("RimLight"));
	RimLight->SetupAttachment(StageRoot);
	RimLight->SetRelativeLocation(FVector(-260.f, 60.f, 260.f));
	SetupStudioLight(RimLight, 45.f, FLinearColor(0.85f, 0.97f, 1.f));
	RimLight->SetCastShadows(false);

	static ConstructorHelpers::FObjectFinder<UAnimationAsset> Idle(TEXT("/Game/Animations/Character/TortugaDemo/Anim/Old_Man_Idle.Old_Man_Idle"));
	static ConstructorHelpers::FObjectFinder<UAnimationAsset> Salute(TEXT("/Game/Animations/Character/TortugaDemo/Anim/Salute.Salute"));
	static ConstructorHelpers::FObjectFinder<UAnimationAsset> Cheer(TEXT("/Game/Animations/Character/TortugaDemo/Anim/Yelling.Yelling"));
	IdleAnim = Idle.Succeeded() ? Idle.Object : nullptr;
	SaluteAnim = Salute.Succeeded() ? Salute.Object : nullptr;
	CheerAnim = Cheer.Succeeded() ? Cheer.Object : nullptr;
}

ATN_CosmeticPreview* ATN_CosmeticPreview::Get(UWorld* World, int32 Slot)
{
	if (!World) { return nullptr; }
	const int32 WantedSlot = FMath::Clamp(Slot, 0, 7);
	for (TActorIterator<ATN_CosmeticPreview> It(World); It; ++It)
	{
		if (It->Slot == WantedSlot) { return *It; }
	}
	// Cada escaparate a 40 m del anterior: sus luces de estudio (1600 cm) no llegan al de al lado.
	const FVector Where = TNPreviewDetail::StageLocation + FVector(4000.0 * WantedSlot, 0.0, 0.0);
	ATN_CosmeticPreview* Stage = World->SpawnActorDeferred<ATN_CosmeticPreview>(ATN_CosmeticPreview::StaticClass(), FTransform(Where), nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Stage)
	{
		Stage->Slot = WantedSlot;
		Stage->FinishSpawning(FTransform(Where));
	}
	return Stage;
}

ATN_CosmeticPreview* ATN_CosmeticPreview::GetFor(const APlayerController* PC)
{
	UWorld* World = PC ? PC->GetWorld() : nullptr;
	const ULocalPlayer* Player = PC ? PC->GetLocalPlayer() : nullptr;
	const UGameInstance* GameInstance = PC ? PC->GetGameInstance() : nullptr;
	const int32 Index = Player && GameInstance ? GameInstance->GetLocalPlayers().IndexOfByKey(Player) : 0;
	return Get(World, FMath::Max(0, Index));
}

void ATN_CosmeticPreview::BeginPlay()
{
	using namespace TNPreviewDetail;
	Super::BeginPlay();
	BuildPedestal();

	Target = UKismetRenderingLibrary::CreateRenderTarget2D(this, LiveSize, LiveSize, RTF_RGBA16f, FLinearColor(0.f, 0.f, 0.f, 1.f));
	Capture->TextureTarget = Target;
	Capture->ClearShowOnlyComponents();
	Capture->ShowOnlyComponent(Turtle);
	Capture->ShowOnlyComponent(Helmet);
	Capture->ShowOnlyComponent(Pedestal);
	AddStudioArt(Pedestal, Capture);

	if (IdleAnim) { Turtle->PlayAnimation(IdleAnim, true); }
	ApplyLookNow(Look);
}

void ATN_CosmeticPreview::BuildPedestal()
{
	using namespace TNPreviewDetail;
	TNProcMesh::FTNProcMeshBuffers B;
	// Islita de arena con canto de roca, una estrella de mar y dos conchas.
	TNProcMesh::TNProcAddLathe(B, FVector(0.0, 0.0, -34.0), { 0.0, 18.0, 30.0 }, { 26.0, 52.0, 62.0 }, 0.05, 11u, Pal(0x8A7A66), 14, 0.0);
	TNProcMesh::TNProcAddLathe(B, FVector(0.0, 0.0, -4.0), { 0.0, 6.0, 12.0, 15.0 }, { 64.0, 66.0, 62.0, 50.0 }, 0.04, 23u, Pal(0xF2D49B), 18, 0.02);
	TArray<FVector2D> StarPoly;
	for (int32 i = 0; i < 10; ++i)
	{
		const double A = PI * i / 5.0 + 0.4;
		const double R = (i % 2) ? 4.0 : 10.0;
		StarPoly.Add(FVector2D(-34.0 + FMath::Cos(A) * R, 22.0 + FMath::Sin(A) * R));
	}
	B.AddPrism(StarPoly, PedestalTop + 1.6, PedestalTop - 1.0, Pal(0xFF8C42));
	for (int32 k = 0; k < 2; ++k)
	{
		TArray<FVector2D> Fan;
		const FVector2D C = k == 0 ? FVector2D(30.0, 34.0) : FVector2D(-10.0, -44.0);
		Fan.Add(C + FVector2D(0.0, -3.0));
		for (int32 j = 0; j <= 6; ++j)
		{
			const double A = PI * (0.1 + 0.8 * j / 6.0);
			Fan.Add(C + FVector2D(FMath::Cos(A) * 6.0, FMath::Sin(A) * 6.0));
		}
		B.AddPrism(Fan, PedestalTop + 1.8, PedestalTop - 1.0, Pal(k == 0 ? 0xFF9A80 : 0xFFD4BA));
	}
	UMaterialInterface* VertexColorMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Cosmetics/Materials/M_CosmeticVertexColor.M_CosmeticVertexColor"));
	TNArt::SetMesh(Pedestal, TNProcRuntimeMesh::MakeStaticMesh(this, B, VertexColorMat), TN_ART("Lobby.CosmeticPreview.Pedestal"));
}

void ATN_CosmeticPreview::ApplyLookNow(const FTN_TurtleLook& InLook)
{
	UTN_CosmeticLook::ApplyLook(this, Turtle, Helmet, InLook, DefaultMaterials);
}

void ATN_CosmeticPreview::SetLook(const FTN_TurtleLook& InLook)
{
	Look = InLook;
	ApplyLookNow(Look);
}

void ATN_CosmeticPreview::PlayPose(bool bCelebrate)
{
	UAnimationAsset* Pose = bCelebrate ? CheerAnim.Get() : SaluteAnim.Get();
	if (!Pose) { return; }
	Turtle->PlayAnimation(Pose, false);
	PoseTimeLeft = FMath::Max(0.5f, Pose->GetPlayLength());
}

void ATN_CosmeticPreview::AddSpin(float Degrees)
{
	SpinDeg = FMath::Fmod(SpinDeg + Degrees, 360.f);
	ManualSpinHold = 2.5f;
}

void ATN_CosmeticPreview::SetLiveCapture(bool bEnabled)
{
	bLive = bEnabled;
	Capture->bCaptureEveryFrame = bEnabled;
	if (bEnabled) { Capture->CaptureScene(); }
}

UTextureRenderTarget2D* ATN_CosmeticPreview::GetThumbnail(ETNCosmeticCategory Category, FName Id)
{
	using namespace TNPreviewDetail;
	const TCHAR* Prefix = Category == ETNCosmeticCategory::Helmet ? TEXT("H")
		: Category == ETNCosmeticCategory::Shell ? TEXT("S") : Category == ETNCosmeticCategory::Eyes ? TEXT("E") : TEXT("B");
	const FName Key(*FString::Printf(TEXT("%s_%s"), Prefix, *Id.ToString()));
	if (TObjectPtr<UTextureRenderTarget2D>* Found = Thumbnails.Find(Key)) { return *Found; }
	UTextureRenderTarget2D* RT = UKismetRenderingLibrary::CreateRenderTarget2D(this, ThumbSize, ThumbSize, RTF_RGBA16f, FLinearColor(0.f, 0.f, 0.f, 1.f));
	Thumbnails.Add(Key, RT);
	FThumbRequest Request;
	Request.Category = Category;
	Request.Id = Id;
	Request.Target = RT;
	PendingThumbs.Add(Request);
	return RT;
}

void ATN_CosmeticPreview::CaptureThumbnail(const FThumbRequest& Request)
{
	using namespace TNPreviewDetail;
	FTN_TurtleLook ThumbLook;
	ThumbLook.Set(Request.Category, Request.Id);
	ApplyLookNow(ThumbLook);

	const FTransform TurtleXf = Turtle->GetComponentTransform();
	FVector Focus;
	FVector ViewDir;
	float Distance;
	if (Request.Category == ETNCosmeticCategory::Helmet)
	{
		if (Request.Id == NAME_None || !Helmet->GetStaticMesh())
		{
			// El de serie: la cabeza con su casco rojo, de tres cuartos.
			Focus = TurtleXf.TransformPosition(FVector(0.0, 7.0, 45.0));
			ViewDir = FVector(0.85, 0.45, 0.28).GetSafeNormal();
			Distance = 105.f;
			ThumbCapture->ClearShowOnlyComponents();
			ThumbCapture->ShowOnlyComponent(Turtle);
		}
		else
		{
			const FBoxSphereBounds Bounds = Helmet->Bounds;
			Focus = Bounds.Origin;
			ViewDir = FVector(0.85, 0.45, 0.42).GetSafeNormal();
			Distance = Bounds.SphereRadius / FMath::Tan(FMath::DegreesToRadians(CaptureFOV * 0.5f)) * 1.05f;
			ThumbCapture->ClearShowOnlyComponents();
			ThumbCapture->ShowOnlyComponent(Helmet);
		}
	}
	else if (Request.Category == ETNCosmeticCategory::Eyes)
	{
		// Primer plano de la cara de frente: los dos ojos.
		Focus = TurtleXf.TransformPosition(FVector(0.0, 9.0, 46.0));
		ViewDir = FVector(0.96, 0.2, 0.2).GetSafeNormal();
		Distance = 62.f;
		ThumbCapture->ClearShowOnlyComponents();
		ThumbCapture->ShowOnlyComponent(Turtle);
	}
	else if (Request.Category == ETNCosmeticCategory::Shell)
	{
		// De espaldas y algo de lado: el caparazón entero.
		Focus = TurtleXf.TransformPosition(FVector(0.0, -3.0, 30.5));
		ViewDir = FVector(-0.9, 0.42, 0.22).GetSafeNormal();
		Distance = 118.f;
		ThumbCapture->ClearShowOnlyComponents();
		ThumbCapture->ShowOnlyComponent(Turtle);
	}
	else
	{
		// De frente: cara y pecho.
		Focus = TurtleXf.TransformPosition(FVector(0.0, 5.0, 38.0));
		ViewDir = FVector(0.9, 0.38, 0.12).GetSafeNormal();
		Distance = 175.f;
		ThumbCapture->ClearShowOnlyComponents();
		ThumbCapture->ShowOnlyComponent(Turtle);
	}
	const FVector CamPos = Focus + ViewDir * Distance;
	ThumbCapture->SetWorldLocationAndRotation(CamPos, LookRotation(CamPos, Focus));
	ThumbCapture->TextureTarget = Request.Target;
	ThumbCapture->CaptureScene();
}

void ATN_CosmeticPreview::Tick(float DeltaSeconds)
{
	using namespace TNPreviewDetail;
	Super::Tick(DeltaSeconds);

	// Miniaturas: unas pocas por fotograma, con la peana quieta de frente, y luego vuelve el conjunto de verdad.
	if (PendingThumbs.Num() > 0)
	{
		Turntable->SetRelativeRotation(FRotator::ZeroRotator);
		const int32 Count = FMath::Min(4, PendingThumbs.Num());
		for (int32 i = 0; i < Count; ++i) { CaptureThumbnail(PendingThumbs[i]); }
		PendingThumbs.RemoveAt(0, Count);
		ApplyLookNow(Look);
	}

	if (ManualSpinHold > 0.f) { ManualSpinHold -= DeltaSeconds; }
	else if (bLive) { SpinDeg = FMath::Fmod(SpinDeg + AutoSpinSpeed * DeltaSeconds, 360.f); }
	Turntable->SetRelativeRotation(FRotator(0.f, SpinDeg, 0.f));

	if (PoseTimeLeft > 0.f)
	{
		PoseTimeLeft -= DeltaSeconds;
		if (PoseTimeLeft <= 0.f && IdleAnim) { Turtle->PlayAnimation(IdleAnim, true); }
	}
}
