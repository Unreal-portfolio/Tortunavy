#include "Lobby/TN_CosmeticPreview.h"
#include "Art/TN_Art.h"
#include "Art/TN_ArtMeshComponent.h"
#include "Art/TN_TurtleArt.h"
#include "Core/TN_CosmeticLook.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyLookComponent.h"
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
#include "../World/ProcMap/TN_ProcMapRuntimeMesh.h"
#include "../Vehicles/TN_BuggyArt.h"

namespace TNPreviewDetail
{
	/** Donde vive el escaparate: muy alto, lejos de todo. */
	const FVector StageLocation(0.0, 0.0, 60000.0);
	constexpr float TurtleScale = 2.5f;
	/** El buggy de 4,2 m en la peana, como un juguete de 1,5 m; con la tortuga al volante, de pie sobre el cojín. */
	constexpr float BuggyScale = 0.36f;
	constexpr float SeatedTurtleHeightCm = 90.f;
	/** Equipo del escaparate: el de serie con su skin y su color, y en las tortugas, iris y banderín de ese color. */
	constexpr int32 BuggyPreviewTeam = 1;
	const FVector TurtleCamPos(430.f, 0.f, 112.f);
	const FVector TurtleCamFocus(0.f, 0.f, 80.f);
	/** Con el buggy, la cámara más alta para ver el lomo de placas. */
	const FVector BuggyCamPos(430.f, 0.f, 156.f);
	const FVector BuggyCamFocus(0.f, 0.f, 46.f);
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

	/** La tortuga y sus piezas de Arte (TNTurtleArt) en la lista de lo que dibuja un captor. */
	void ShowTurtle(USkeletalMeshComponent* Turtle, USceneCaptureComponent2D* Cap)
	{
		Cap->ShowOnlyComponent(Turtle);
		TArray<UPrimitiveComponent*> Pieces;
		TNTurtleArt::GetPieceComponents(Turtle, Pieces);
		for (UPrimitiveComponent* Piece : Pieces) { Cap->ShowOnlyComponent(Piece); }
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
	// La malla es la del personaje de la tortuga (TNTurtleArt::ApplyBody en BeginPlay), no una ruta fija.

	Helmet = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Helmet"));
	Helmet->SetupAttachment(Turtle);
	SetupStudioPrimitive(Helmet);

	BuggyRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BuggyRoot"));
	BuggyRoot->SetupAttachment(Turntable);
	BuggyRoot->SetRelativeLocation(FVector(0.f, 0.f, PedestalTop));
	BuggyRoot->SetRelativeScale3D(FVector(BuggyScale));
	Buggy = CreateDefaultSubobject<UTN_BuggyLookComponent>(TEXT("Buggy"));
	Buggy->SetupAttachment(BuggyRoot);

	ThumbBuggyRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ThumbBuggyRoot"));
	ThumbBuggyRoot->SetupAttachment(StageRoot);
	ThumbBuggyRoot->SetRelativeLocation(FVector(0.f, 0.f, PedestalTop));
	ThumbBuggyRoot->SetRelativeScale3D(FVector(BuggyScale));
	ThumbBuggy = CreateDefaultSubobject<UTN_BuggyLookComponent>(TEXT("ThumbBuggy"));
	ThumbBuggy->SetupAttachment(ThumbBuggyRoot);

	// Cámara de frente, un pelín por encima: la tortuga (133 cm) y un sombrero alto caben con aire.
	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(StageRoot);
	Capture->SetRelativeLocation(TurtleCamPos);
	Capture->SetRelativeRotation(LookRotation(TurtleCamPos, TurtleCamFocus));
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
	// La tortuga del personaje (malla, materiales y escala) sobre la peana, mirando a la cámara, y sus animaciones.
	if (TNTurtleArt::ApplyBody(Turtle, FTransform(FRotator(0.f, -90.f, 0.f), FVector(0.f, 0.f, PedestalTop), FVector(TurtleScale))))
	{
		DefaultMaterials.Reset();
	}
	IdleAnim = TNTurtleArt::GetClip(ETNTurtleClip::Idle);
	SaluteAnim = TNTurtleArt::GetClip(ETNTurtleClip::Salute);
	CheerAnim = TNTurtleArt::GetClip(ETNTurtleClip::Cheer);

	Target = UKismetRenderingLibrary::CreateRenderTarget2D(this, LiveSize, LiveSize, RTF_RGBA16f, FLinearColor(0.f, 0.f, 0.f, 1.f));
	Capture->TextureTarget = Target;
	Capture->ClearShowOnlyComponents();
	Capture->ShowOnlyComponent(Turtle);
	Capture->ShowOnlyComponent(Helmet);
	Capture->ShowOnlyComponent(Pedestal);
	AddStudioArt(Pedestal, Capture);

	// El buggy: sus piezas se crean al vestirlo y solo las ven las capturas.
	Buggy->SetStudio(StudioChannel());
	Buggy->ApplyLook(BuggyLookState, BuggyPreviewTeam, true);
	ThumbBuggy->SetStudio(StudioChannel());
	ThumbBuggy->ApplyLook(FTN_BuggyLook(), BuggyPreviewTeam, true);
	TArray<UPrimitiveComponent*> BuggyParts;
	BuggyPrimitives(BuggyParts);
	for (UPrimitiveComponent* Part : BuggyParts) { Capture->ShowOnlyComponent(Part); }

	if (IdleAnim) { Turtle->PlayAnimation(IdleAnim, true); }
	ApplyLookNow(Look);
	ApplyMode(bBuggyMode);
}

void ATN_CosmeticPreview::BuggyPrimitives(TArray<UPrimitiveComponent*>& Out) const
{
	if (Buggy) { Buggy->GetPrimitives(Out); }
}

void ATN_CosmeticPreview::ApplyMode(bool bBuggy)
{
	using namespace TNPreviewDetail;
	bBuggyMode = bBuggy;
	BuggyRoot->SetVisibility(bBuggy, true);
	if (bBuggy)
	{
		// La tortuga del jugador va al volante, de pie sobre el cojín de la conductora (igual en todos los modelos): la
		// cabeza queda donde la lleva la tortuga sentada del Rally.
		Turtle->AttachToComponent(BuggyRoot, FAttachmentTransformRules::KeepRelativeTransform);
		Turtle->SetRelativeLocationAndRotation(FVector(ATN_Buggy::DriverSeatLocal.X - 4.f, 0.f, TNBuggyArt::Frame::DriverCushionZ), FRotator(0.f, -90.f, 0.f));
		const USkeletalMesh* TurtleMesh = Turtle->GetSkeletalMeshAsset();
		const float Height = TurtleMesh ? static_cast<float>(TurtleMesh->GetBounds().BoxExtent.Z) * 2.f : 0.f;
		Turtle->SetRelativeScale3D(FVector(Height > KINDA_SMALL_NUMBER ? SeatedTurtleHeightCm / Height : 1.f));
	}
	else
	{
		Turtle->AttachToComponent(Turntable, FAttachmentTransformRules::KeepRelativeTransform);
		Turtle->SetRelativeLocationAndRotation(FVector(0.f, 0.f, PedestalTop), FRotator(0.f, -90.f, 0.f));
		Turtle->SetRelativeScale3D(FVector(TurtleScale));
	}
	Turtle->SetVisibility(true, true);
	Pedestal->SetRelativeScale3D(FVector(bBuggy ? 1.35f : 1.f, bBuggy ? 1.35f : 1.f, 1.f));
	const FVector CamPos = bBuggy ? BuggyCamPos : TurtleCamPos;
	Capture->SetRelativeLocationAndRotation(CamPos, LookRotation(CamPos, bBuggy ? BuggyCamFocus : TurtleCamFocus));
	if (bLive) { Capture->CaptureScene(); }
}

void ATN_CosmeticPreview::SetBuggyMode(bool bInBuggyMode)
{
	if (bInBuggyMode != bBuggyMode) { ApplyMode(bInBuggyMode); }
}

void ATN_CosmeticPreview::SetBuggyLook(const FTN_BuggyLook& InLook)
{
	BuggyLookState = InLook;
	if (Buggy) { Buggy->ApplyLook(InLook, TNPreviewDetail::BuggyPreviewTeam); }
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
	// Las piezas de Arte que acaba de pegar (o de cambiar) también salen en la vista en directo.
	if (Capture->TextureTarget) { TNPreviewDetail::ShowTurtle(Turtle, Capture); }
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
		: Category == ETNCosmeticCategory::Shell ? TEXT("S") : Category == ETNCosmeticCategory::Eyes ? TEXT("E")
		: Category == ETNCosmeticCategory::BuggyModel ? TEXT("M") : Category == ETNCosmeticCategory::BuggyPaint ? TEXT("P") : TEXT("B");
	const FName Key(*FString::Printf(TEXT("%s_%s"), Prefix, *Id.ToString()));
	if (TObjectPtr<UTextureRenderTarget2D>* Found = Thumbnails.Find(Key)) { return *Found; }
	UTextureRenderTarget2D* RT = UKismetRenderingLibrary::CreateRenderTarget2D(this, ThumbSize, ThumbSize, RTF_RGBA16f, FLinearColor(0.f, 0.f, 0.f, 1.f));
	Thumbnails.Add(Key, RT);
	FThumbRequest Request;
	Request.Category = Category;
	Request.Id = Id;
	Request.Target = RT;
	(TNIsBuggyCategory(Category) ? PendingBuggyThumbs : PendingThumbs).Add(Request);
	return RT;
}

void ATN_CosmeticPreview::TickBuggyThumbs()
{
	if (PendingBuggyThumbs.Num() == 0 || !ThumbBuggy) { return; }
	const FThumbRequest& Request = PendingBuggyThumbs[0];
	FTN_BuggyLook Wanted;
	Wanted.Set(Request.Category, Request.Id);
	if (ThumbBuggy->GetLook() != Wanted)
	{
		ThumbBuggy->ApplyLook(Wanted, TNPreviewDetail::BuggyPreviewTeam);
		BuggyThumbFrames = 0;
		bBuggyThumbPrimed = false;
		return;
	}
	// Un fotograma para que la escena tenga las mallas nuevas y, si hay precarga de PSO, a que acabe (con tope). Mientras
	// precarga, una pieza puede no tener aún su proxy de render (no se dibuja): también se espera.
	bool bPrecaching = false;
	TArray<UPrimitiveComponent*> Parts;
	ThumbBuggy->GetPrimitives(Parts);
	for (UPrimitiveComponent* Part : Parts)
	{
		if (!Part || !Part->IsVisible()) { continue; }
		bPrecaching |= Part->CheckPSOPrecachingAndBoostPriority(EPSOPrecachePriority::Highest) || (Part->IsRegistered() && !Part->SceneProxy);
	}
	if (++BuggyThumbFrames < 2 || (bPrecaching && BuggyThumbFrames < 600)) { return; }
	// Dos capturas en fotogramas seguidos: la captura guarda su estado de render, y la oclusión de la miniatura anterior
	// (otra cámara, otra carrocería) taparía piezas que ahora se ven. La primera la pone al día; la segunda es la buena.
	CaptureBuggyThumbnail(Request);
	if (!bBuggyThumbPrimed)
	{
		bBuggyThumbPrimed = true;
		return;
	}
	bBuggyThumbPrimed = false;
	PendingBuggyThumbs.RemoveAt(0);
}

void ATN_CosmeticPreview::CaptureBuggyThumbnail(const FThumbRequest& Request)
{
	using namespace TNPreviewDetail;
	// Buggy solo, de tres cuartos por el lado de la conductora: el modelo entero o, la pintura (en el de serie), desde más
	// arriba para que se vean los pontones y el capó.
	ThumbCapture->ClearShowOnlyComponents();
	TArray<UPrimitiveComponent*> Parts;
	ThumbBuggy->GetPrimitives(Parts);
	for (UPrimitiveComponent* Part : Parts) { ThumbCapture->ShowOnlyComponent(Part); }
	const bool bModel = Request.Category == ETNCosmeticCategory::BuggyModel;
	const FTransform BuggyXf = ThumbBuggyRoot->GetComponentTransform();
	const FVector Focus = BuggyXf.TransformPosition(bModel ? FVector(0.f, 0.f, 90.f) : FVector(10.f, 0.f, 80.f));
	const FVector ViewDir = (bModel ? FVector(0.78, -0.52, 0.36) : FVector(0.55, -0.62, 0.56)).GetSafeNormal();
	const float Reach = (bModel ? 270.f : 235.f) * BuggyScale;
	const float Distance = Reach / FMath::Tan(FMath::DegreesToRadians(CaptureFOV * 0.5f)) * 1.02f;
	const FVector CamPos = Focus + ViewDir * Distance;
	ThumbCapture->SetWorldLocationAndRotation(CamPos, LookRotation(CamPos, Focus));
	ThumbCapture->TextureTarget = Request.Target;
	ThumbCapture->CaptureScene();
}

void ATN_CosmeticPreview::CaptureThumbnail(const FThumbRequest& Request)
{
	using namespace TNPreviewDetail;
	// Las miniaturas de la tortuga son con la tortuga en la peana.
	if (bBuggyMode) { ApplyMode(false); }
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
			ShowTurtle(Turtle, ThumbCapture);
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
		ShowTurtle(Turtle, ThumbCapture);
	}
	else if (Request.Category == ETNCosmeticCategory::Shell)
	{
		// De espaldas y algo de lado: el caparazón entero.
		Focus = TurtleXf.TransformPosition(FVector(0.0, -3.0, 30.5));
		ViewDir = FVector(-0.9, 0.42, 0.22).GetSafeNormal();
		Distance = 118.f;
		ThumbCapture->ClearShowOnlyComponents();
		ShowTurtle(Turtle, ThumbCapture);
	}
	else
	{
		// De frente: cara y pecho.
		Focus = TurtleXf.TransformPosition(FVector(0.0, 5.0, 38.0));
		ViewDir = FVector(0.9, 0.38, 0.12).GetSafeNormal();
		Distance = 175.f;
		ThumbCapture->ClearShowOnlyComponents();
		ShowTurtle(Turtle, ThumbCapture);
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
		const bool bWasBuggy = bBuggyMode;
		Turntable->SetRelativeRotation(FRotator::ZeroRotator);
		const int32 Count = FMath::Min(4, PendingThumbs.Num());
		for (int32 i = 0; i < Count; ++i) { CaptureThumbnail(PendingThumbs[i]); }
		PendingThumbs.RemoveAt(0, Count);
		ApplyLookNow(Look);
		if (bBuggyMode != bWasBuggy) { ApplyMode(bWasBuggy); }
		Turtle->SetVisibility(true, true);
	}
	TickBuggyThumbs();

	if (ManualSpinHold > 0.f) { ManualSpinHold -= DeltaSeconds; }
	else if (bLive) { SpinDeg = FMath::Fmod(SpinDeg + AutoSpinSpeed * DeltaSeconds, 360.f); }
	Turntable->SetRelativeRotation(FRotator(0.f, SpinDeg, 0.f));

	if (PoseTimeLeft > 0.f)
	{
		PoseTimeLeft -= DeltaSeconds;
		if (PoseTimeLeft <= 0.f && IdleAnim) { Turtle->PlayAnimation(IdleAnim, true); }
	}
}
