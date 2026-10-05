#include "Lobby/TN_ShopKeeper.h"
#include "Art/TN_Art.h"
#include "Multiplayer/TN_LocalViews.h"
#include "Art/TN_TurtleArt.h"
#include "Audio/TN_MusicSynthComponent.h"
#include "Core/TN_CosmeticLook.h"
#include "Core/TN_Log.h"
#include "Lobby/TN_NpcAnimInstance.h"
#include "Player/MP_GamePlayerController.h"
#include "Animation/AnimationAsset.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Scene.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "../World/ProcMap/TN_ProcMapRuntimeMesh.h"
#include "TN_CastleKit.h"

namespace TNShopKeeperDetail
{
	/** Color sRGB 0xRRGGBB para mallas en ejecución con M_CosmeticVertexColor (ver ATN_CosmeticPreview). */
	FLinearColor Pal(uint32 Hex)
	{
		return FLinearColor(TNProcRuntimeMesh::SRGBToLinear(((Hex >> 16) & 255) / 255.f), TNProcRuntimeMesh::SRGBToLinear(((Hex >> 8) & 255) / 255.f),
			TNProcRuntimeMesh::SRGBToLinear((Hex & 255) / 255.f), 1.f);
	}

	/** Mostrador delante del tendero (en su +X): largo, para que el puesto llene el hueco entre dos torres. */
	constexpr double CounterX = 125.0;
	constexpr double CounterHalfDepth = 32.0;
	constexpr double CounterHalfWidth = 260.0;
	constexpr double CounterHeight = 108.0;
	/** Toldo: el borde de delante (con el volante de picos) y el de detrás, más alto. */
	constexpr double CanopyFrontX = CounterX + 75.0;
	constexpr double CanopyBackX = -115.0;
	constexpr double CanopyFrontZ = 305.0;
	constexpr double CanopyBackZ = 378.0;
	constexpr double CanopyHalfWidth = CounterHalfWidth + 45.0;
	/** Tablero del cartel: de pie encima del borde de delante del toldo, por detrás del volante. */
	constexpr double SignX = CanopyFrontX - 12.0;
	constexpr double SignZ = CanopyFrontZ + 34.0;
	constexpr double SignHalfW = 215.0;
	constexpr double SignHalfH = 27.0;
	/** Estantería del fondo (pegada a la muralla): centro en X, medio ancho, fondo, alto y cota de cada balda. */
	constexpr double ShelfX = CanopyBackX - 20.0;
	constexpr double ShelfHalfW = CounterHalfWidth + 10.0;
	constexpr double ShelfHalfD = 20.0;
	constexpr double ShelfH = 252.0;
	constexpr double ShelfZ[4] = { 18.0, 92.0, 166.0, 240.0 };

	using FBuffers = TNProcMesh::FTNProcMeshBuffers;

	/** Sombrero de paja con cinta roja (centro en la base del ala). */
	void AddStrawHat(FBuffers& B, const FVector& C, double S)
	{
		TNProcMesh::TNProcAddCylinder(B, C, C + FVector(0.0, 0.0, 1.6 * S), 21.0 * S, 21.0 * S, 14, Pal(0xE9C46A));
		TNProcMesh::TNProcAddCylinder(B, C, C + FVector(0.0, 0.0, 10.0 * S), 10.5 * S, 9.0 * S, 12, Pal(0xE0B654));
		TNProcMesh::TNProcAddCylinder(B, C + FVector(0.0, 0.0, 1.6 * S), C + FVector(0.0, 0.0, 4.2 * S), 10.8 * S, 10.5 * S, 12, Pal(0xE63946));
	}

	/** Gorra de marinero blanca con franja azul y borla roja. */
	void AddSailorCap(FBuffers& B, const FVector& C, double S)
	{
		TNProcMesh::TNProcAddCylinder(B, C, C + FVector(0.0, 0.0, 7.5 * S), 11.0 * S, 12.0 * S, 12, Pal(0xFFFFFF));
		TNProcMesh::TNProcAddCylinder(B, C, C + FVector(0.0, 0.0, 2.6 * S), 11.3 * S, 11.4 * S, 12, Pal(0x12305A));
		TNProcMesh::TNProcAddCylinder(B, C + FVector(0.0, 0.0, 7.5 * S), C + FVector(0.0, 0.0, 10.0 * S), 3.2 * S, 2.0 * S, 8, Pal(0xE63946));
	}

	/** Corona dorada con cinco picos y gemas. */
	void AddCrown(FBuffers& B, const FVector& C, double S)
	{
		TNProcMesh::TNProcAddCylinder(B, C, C + FVector(0.0, 0.0, 6.0 * S), 10.0 * S, 10.5 * S, 15, Pal(0xFFCB3D));
		for (int32 k = 0; k < 5; ++k)
		{
			const double A = TNProcMap::TwoPi * k / 5.0;
			const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0.0);
			const FVector Base = C + Dir * 10.0 * S + FVector(0.0, 0.0, 6.0 * S);
			const FVector Side = FVector(-Dir.Y, Dir.X, 0.0) * 4.5 * S;
			B.AddTri(Base - Side, Base + Side, Base + FVector(0.0, 0.0, 8.0 * S), Dir, Pal(0xFFCB3D));
			B.AddTri(Base - Side, Base + Side, Base + FVector(0.0, 0.0, 8.0 * S), -Dir, Pal(0xE0A92E));
			B.AddBox(C + Dir * 10.6 * S + FVector(0.0, 0.0, 3.0 * S), Dir, FVector(0.8, 1.6 * S, 1.6 * S), (k % 2) ? Pal(0xE63946) : Pal(0x2EC4B6));
		}
	}

	/** Gorro de fiesta: cono a rayas con pompón. */
	void AddPartyHat(FBuffers& B, const FVector& C, double S)
	{
		for (int32 k = 0; k < 4; ++k)
		{
			const double Z0 = 22.0 * S * k / 4.0, Z1 = 22.0 * S * (k + 1) / 4.0;
			TNProcMesh::TNProcAddCylinder(B, C + FVector(0.0, 0.0, Z0), C + FVector(0.0, 0.0, Z1), 9.0 * S * (1.0 - k / 4.0) + 0.5, 9.0 * S * (1.0 - (k + 1) / 4.0) + 0.5, 10,
				(k % 2) ? Pal(0xFF6FA8) : Pal(0x4CC9F0), false);
		}
		TNProcMesh::TNProcAddCylinder(B, C + FVector(0.0, 0.0, 21.0 * S), C + FVector(0.0, 0.0, 25.0 * S), 3.0 * S, 2.0 * S, 8, Pal(0xFFD23F));
	}

	/** Gorro de hélice: cúpula de colores y hélice encima. */
	void AddPropellerCap(FBuffers& B, const FVector& C, double S)
	{
		TNProcMesh::TNProcAddCylinder(B, C, C + FVector(0.0, 0.0, 5.0 * S), 11.0 * S, 9.5 * S, 12, Pal(0xFF6A52));
		TNProcMesh::TNProcAddCylinder(B, C + FVector(0.0, 0.0, 5.0 * S), C + FVector(0.0, 0.0, 9.0 * S), 9.5 * S, 4.0 * S, 12, Pal(0x3DDC62));
		B.AddBeam(C + FVector(0.0, 0.0, 9.0 * S), C + FVector(0.0, 0.0, 13.0 * S), 0.8 * S, Pal(0x3B3F4A));
		B.AddBox(C + FVector(0.0, 0.0, 13.2 * S), FVector(0.8, 0.6, 0.0).GetSafeNormal(), FVector(12.0 * S, 2.2 * S, 0.5 * S), Pal(0xFFD23F));
	}

	/** Caparazón de muestra: cúpula a gajos de dos colores. */
	void AddShellDome(FBuffers& B, const FVector& C, double S, const FLinearColor& A, const FLinearColor& Accent)
	{
		const TArray<double> Zs = { 0.0, 5.0 * S, 10.0 * S, 14.0 * S, 16.0 * S };
		const TArray<double> Rs = { 20.0 * S, 18.5 * S, 14.0 * S, 7.5 * S, 1.0 };
		TNCastleKit::AddRevolution(B, C, Zs, Rs, 12, A, true, Accent, 2);
	}

	/** Ojo de muestra: bola blanca con iris de color y pupila, mirando hacia Dir. */
	void AddEyeball(FBuffers& B, const FVector& C, double Rad, const FVector& Dir, const FLinearColor& Iris)
	{
		const TArray<double> Zs = { -Rad, -Rad * 0.7, 0.0, Rad * 0.7, Rad };
		const TArray<double> Rs = { 0.5, Rad * 0.71, Rad, Rad * 0.71, 0.5 };
		TNCastleKit::AddRevolution(B, C, Zs, Rs, 10, Pal(0xFFFFFF), true);
		TNProcMesh::TNProcAddCylinder(B, C + Dir * (Rad - 0.4), C + Dir * (Rad + 0.6), Rad * 0.55, Rad * 0.5, 10, Iris);
		TNProcMesh::TNProcAddCylinder(B, C + Dir * (Rad + 0.4), C + Dir * (Rad + 1.0), Rad * 0.25, Rad * 0.22, 8, Pal(0x101418));
	}
}

ATN_ShopKeeper::ATN_ShopKeeper()
{
	using namespace TNShopKeeperDetail;
	PrimaryActorTick.bCanEverTick = true;
	NetDormancy = DORM_Awake;
	bAlwaysRelevant = true;
	CooldownSeconds = 0.6f;
	PromptText = NSLOCTEXT("Tortunabo", "ShopPrompt", "Hablar con el tendero");
	KeeperName = NSLOCTEXT("Tortunabo", "ShopKeeperName", "Don Tortugo");
	ShopName = NSLOCTEXT("Tortunabo", "ShopName", "La Concha Dorada");
	KeeperLook.HelmetId = TEXT("Helmet_Straw");
	KeeperLook.ShellId = TEXT("Shell_Scutes");
	KeeperLook.SkinId = TEXT("Body_Sand");

	// Hitbox de interacción (invisible) delante del mostrador; el aviso flota sobre él.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Mesh && Cylinder.Succeeded())
	{
		Mesh->SetStaticMesh(Cylinder.Object);
		Mesh->SetRelativeLocation(FVector(CounterX + 70.0, 0.0, 60.0));
		Mesh->SetRelativeScale3D(FVector(2.4f, 2.4f, 1.2f));
		// Tampoco se ve en el editor: el nivel se enseña tal cual se juega.
		Mesh->SetVisibility(false);
		Mesh->SetHiddenInGame(true);
	}
	if (PromptWidgetComponent)
	{
		// La hitbox va estirada: el aviso no hereda su escala.
		PromptWidgetComponent->SetUsingAbsoluteScale(true);
		PromptWidgetComponent->SetRelativeLocation(FVector(0.f, 0.f, 160.f));
	}

	Keeper = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Keeper"));
	Keeper->SetupAttachment(SceneRoot);
	Keeper->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	Keeper->SetRelativeScale3D(FVector(KeeperScale));
	Keeper->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Keeper->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	// La malla es la del personaje de la tortuga (TNTurtleArt::ApplyBody en BuildVisuals), no una ruta fija.

	KeeperHat = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("KeeperHat"));
	KeeperHat->SetupAttachment(Keeper);
	KeeperHat->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	KeeperBlock = CreateDefaultSubobject<UCapsuleComponent>(TEXT("KeeperBlock"));
	KeeperBlock->SetupAttachment(SceneRoot);
	KeeperBlock->InitCapsuleSize(55.f, 95.f);
	KeeperBlock->SetRelativeLocation(FVector(0.f, 0.f, 95.f));
	KeeperBlock->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	KeeperBlock->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	Stall = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Stall"));
	Stall->SetupAttachment(SceneRoot);
	Stall->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	CounterBlock = CreateDefaultSubobject<UBoxComponent>(TEXT("CounterBlock"));
	CounterBlock->SetupAttachment(SceneRoot);
	CounterBlock->InitBoxExtent(FVector(CounterHalfDepth + 4.0, CounterHalfWidth + 6.0, CounterHeight * 0.5));
	CounterBlock->SetRelativeLocation(FVector(CounterX, 0.0, CounterHeight * 0.5));
	CounterBlock->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	CounterBlock->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	// Cartel del toldo: mira hacia los clientes (+X).
	Sign = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Sign"));
	Sign->SetupAttachment(SceneRoot);
	// Delante del tablero del cartel (su cara está en SignX + 4).
	Sign->SetRelativeLocation(FVector(SignX + 6.5, 0.0, SignZ));
	Sign->SetHorizontalAlignment(EHTA_Center);
	Sign->SetVerticalAlignment(EVRTA_TextCenter);
	Sign->SetWorldSize(40.f);
	Sign->SetTextRenderColor(FColor(255, 214, 90));
	Sign->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Luces cálidas del puesto (sin sombras, suaves): se colocan a escala en BuildVisuals.
	auto MakeLight = [this](const TCHAR* Name, float Lumens, const FLinearColor& Color)
	{
		UPointLightComponent* Light = CreateDefaultSubobject<UPointLightComponent>(Name);
		Light->SetupAttachment(SceneRoot);
		Light->SetIntensityUnits(ELightUnits::Lumens);
		Light->SetIntensity(Lumens);
		Light->SetLightColor(Color);
		Light->SetCastShadows(false);
		return Light;
	};
	CanopyLight = MakeLight(TEXT("CanopyLight"), 2400.f, FLinearColor(1.f, 0.82f, 0.6f));
	ShelfLight = MakeLight(TEXT("ShelfLight"), 1500.f, FLinearColor(1.f, 0.86f, 0.66f));
	LampLight = MakeLight(TEXT("LampLight"), 1100.f, FLinearColor(1.f, 0.72f, 0.42f));
}

void ATN_ShopKeeper::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	BuildVisuals();
}

void ATN_ShopKeeper::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
#if WITH_EDITOR
	// Al abrir el nivel en el editor, la malla del puesto (transitoria) llega vacía: se rehace para verlo sin jugar.
	if (!IsTemplate() && GetWorld() && GetWorld()->WorldType == EWorldType::Editor && Stall && !Stall->GetStaticMesh()) { BuildVisuals(); }
#endif
}

FTransform ATN_ShopKeeper::KeeperTransform() const
{
	// Como en BP_TortugaCharacter: la malla de demo mira a su +Y; girada -90 mira a los clientes (+X), y se gira hacia el
	// jugador. Con otra malla en el personaje, la misma diferencia (TNTurtleArt::GetCopyCorrection).
	return KeeperCorrection * FTransform(FRotator(0.f, -90.f + LookYaw, 0.f), FVector::ZeroVector, FVector(KeeperScale));
}

void ATN_ShopKeeper::BuildVisuals()
{
	using namespace TNShopKeeperDetail;
	// El puesto crece alrededor del tendero (que se queda detrás del mostrador): choques y aviso, en todas las máquinas.
	const double S = StallScale;
	if (CounterBlock)
	{
		CounterBlock->SetBoxExtent(FVector(CounterHalfDepth + 4.0, CounterHalfWidth + 6.0, CounterHeight * 0.5) * S);
		CounterBlock->SetRelativeLocation(FVector(CounterX, 0.0, CounterHeight * 0.5) * S);
	}
	if (Mesh) { Mesh->SetRelativeLocation(FVector((CounterX + 70.0) * S, 0.0, 60.0)); }
	// Lo demás solo se ve. El servidor dedicado no carga los componentes sin colisión (tendero, sombrero, puesto y cartel:
	// UPrimitiveComponent::NeedsLoadForServer) y llegan nulos al viajar al lobby (#658): sin pantalla no se construye nada.
	if (IsRunningDedicatedServer() || !Keeper || !Stall || !Sign) { return; }

	// La tortuga del personaje (malla, materiales y escala) y sus animaciones de los ajustes de arte.
	KeeperCorrection = FTransform::Identity;
	if (TNTurtleArt::ApplyBody(Keeper, KeeperTransform())) { KeeperDefaults.Reset(); }
	KeeperCorrection = TNTurtleArt::GetCopyCorrection();
	IdleAnim = TNTurtleArt::GetClip(ETNTurtleClip::Idle);
	WaveAnim = TNTurtleArt::GetClip(ETNTurtleClip::Salute);
	Sign->SetText(ShopName.ToUpper());
	UTN_CosmeticLook::ApplyLook(this, Keeper, KeeperHat, KeeperLook, KeeperDefaults);
	// En el editor, el tendero en su espera (no en T).
	UTN_NpcAnimInstance::PreviewInEditor(Keeper, IdleAnim);
	BuildStall();
	// Malla y cartel a la escala del puesto.
	Stall->SetRelativeScale3D(FVector(S));
	Sign->SetRelativeLocation(FVector(SignX + 6.5, 0.0, SignZ) * S);
	Sign->SetWorldSize(static_cast<float>(40.0 * S));
	// Luces: bajo el toldo sobre el tendero y el mostrador, delante de la estantería y en el farol del lado derecho.
	if (CanopyLight)
	{
		CanopyLight->SetRelativeLocation(FVector(40.0, 0.0, 255.0) * S);
		CanopyLight->SetAttenuationRadius(static_cast<float>(560.0 * S));
	}
	if (ShelfLight)
	{
		ShelfLight->SetRelativeLocation(FVector(ShelfX + 90.0, 0.0, 190.0) * S);
		ShelfLight->SetAttenuationRadius(static_cast<float>(400.0 * S));
	}
	if (LampLight)
	{
		LampLight->SetRelativeLocation(FVector(CounterX + 68.0, CanopyHalfWidth + 40.0, 184.0) * S);
		LampLight->SetAttenuationRadius(static_cast<float>(380.0 * S));
	}
}

void ATN_ShopKeeper::BeginPlay()
{
	Super::BeginPlay();
	BuildVisuals();
	// Espera en bucle con el saludo fundido encima (sin cortes al empezar y acabar el gesto).
	UTN_NpcAnimInstance::SetupOn(Keeper, IdleAnim);
	HideBlockoutKeeper();
	if (GetNetMode() != NM_DedicatedServer)
	{
		Radio = UTN_MusicSynthComponent::AttachMusic3D(this, GetActorTransform().TransformPosition(FVector(0.0, 0.0, 180.0)),
			ETNMusicTrack::Shop, RadioVolume);
	}
}

FVector ATN_ShopKeeper::GetInteractionPoint() const
{
	using namespace TNShopKeeperDetail;
	return GetActorTransform().TransformPosition(FVector((CounterX + CounterHalfDepth) * StallScale + 60.0, 0.0, 0.0));
}

void ATN_ShopKeeper::SetRadiosDucked(UWorld* World, bool bDucked)
{
	if (!World) { return; }
	for (TActorIterator<ATN_ShopKeeper> It(World); It; ++It)
	{
		if (UTN_MusicSynthComponent* ShopRadio = It->Radio)
		{
			ShopRadio->SetMusicVolume(bDucked ? It->RadioVolume * 0.15f : It->RadioVolume);
		}
	}
}

void ATN_ShopKeeper::BuildStall()
{
	using namespace TNShopKeeperDetail;
	TNProcMesh::FTNProcMeshBuffers B;
	const FLinearColor Wood = Pal(0xB07A4A);
	const FLinearColor WoodDark = Pal(0x8A5A32);
	const FLinearColor WoodTop = Pal(0xD9A066);
	const FLinearColor Coral = Pal(0xFF6A52);
	const FLinearColor Cream = Pal(0xFFF2D4);
	const FLinearColor Navy = Pal(0x12305A);
	const FLinearColor Gold = Pal(0xFFCB3D);
	const FLinearColor Flags[4] = { Pal(0xFF6FA8), Pal(0xFFD23F), Pal(0x4CC9F0), Pal(0x3DDC62) };

	// Mostrador de tablones con tapa clara y franja coral delante.
	B.AddBox(FVector(CounterX, 0.0, CounterHeight * 0.5 - 4.0), FVector(1.0, 0.0, 0.0), FVector(CounterHalfDepth, CounterHalfWidth, CounterHeight * 0.5 - 4.0), Wood);
	B.AddBox(FVector(CounterX + 2.0, 0.0, CounterHeight - 3.0), FVector(1.0, 0.0, 0.0), FVector(CounterHalfDepth + 8.0, CounterHalfWidth + 10.0, 4.5), WoodTop);
	for (int32 i = -5; i <= 5; ++i)
	{
		const double Y = i * (CounterHalfWidth / 5.6);
		B.AddBox(FVector(CounterX + CounterHalfDepth + 0.8, Y, CounterHeight * 0.5 - 4.0), FVector(1.0, 0.0, 0.0), FVector(1.2, 2.4, CounterHeight * 0.5 - 8.0), WoodDark);
	}
	B.AddBox(FVector(CounterX + CounterHalfDepth + 1.8, 0.0, CounterHeight * 0.62), FVector(1.0, 0.0, 0.0), FVector(0.8, CounterHalfWidth - 8.0, 10.0), Coral);

	// Postes (delante, a la altura del borde del toldo; detrás, más altos).
	for (const double PX : { CanopyFrontX - 20.0, CanopyBackX + 20.0 })
	{
		for (const double PY : { -CanopyHalfWidth + 20.0, CanopyHalfWidth - 20.0 })
		{
			const double Top = PX > 0.0 ? CanopyFrontZ : CanopyBackZ;
			B.AddBeam(FVector(PX, PY, 0.0), FVector(PX, PY, Top), 6.5, WoodDark);
		}
	}

	// Toldo inclinado a rayas (las dos caras) y volante de picos colgando del borde de delante, un poco por fuera.
	const int32 Stripes = 12;
	for (int32 k = 0; k < Stripes; ++k)
	{
		const double Ya = FMath::Lerp(-CanopyHalfWidth, CanopyHalfWidth, static_cast<double>(k) / Stripes);
		const double Yb = FMath::Lerp(-CanopyHalfWidth, CanopyHalfWidth, static_cast<double>(k + 1) / Stripes);
		const FLinearColor C = (k % 2) ? Cream : Coral;
		const FVector A(CanopyFrontX, Ya, CanopyFrontZ), Bf(CanopyFrontX, Yb, CanopyFrontZ), Cb(CanopyBackX, Yb, CanopyBackZ), D(CanopyBackX, Ya, CanopyBackZ);
		B.AddQuad(A, Bf, Cb, D, FVector(0.3, 0.0, 1.0), C);
		B.AddQuad(A, Bf, Cb, D, FVector(-0.3, 0.0, -1.0), C * 0.8f);
		const FVector Va(CanopyFrontX + 2.0, Ya, CanopyFrontZ + 1.0), Vb(CanopyFrontX + 2.0, Yb, CanopyFrontZ + 1.0);
		const FVector Tip(CanopyFrontX + 2.0, (Ya + Yb) * 0.5, CanopyFrontZ - 26.0);
		B.AddTri(Va, Vb, Tip, FVector(1.0, 0.0, 0.0), C);
		B.AddTri(Va, Vb, Tip, FVector(-1.0, 0.0, 0.0), C * 0.8f);
	}

	// Cartel de pie sobre el borde de delante del toldo, con marco dorado y dos patas (el texto es el componente Sign).
	B.AddBox(FVector(SignX, 0.0, SignZ), FVector(1.0, 0.0, 0.0), FVector(4.0, SignHalfW, SignHalfH), Navy);
	B.AddBox(FVector(SignX - 1.5, 0.0, SignZ), FVector(1.0, 0.0, 0.0), FVector(4.0, SignHalfW + 7.0, SignHalfH + 7.0), Gold);
	for (const double LegY : { -SignHalfW * 0.6, SignHalfW * 0.6 })
	{
		B.AddBeam(FVector(SignX - 2.0, LegY, CanopyFrontZ - 4.0), FVector(SignX - 2.0, LegY, SignZ - SignHalfH), 4.0, WoodDark);
	}

	// Guirnalda de banderines de fiesta de poste a poste, delante del volante (no lo toca).
	const FVector G0(CanopyFrontX + 16.0, -CanopyHalfWidth + 20.0, CanopyFrontZ - 34.0);
	const FVector G1(CanopyFrontX + 16.0, CanopyHalfWidth - 20.0, CanopyFrontZ - 34.0);
	const int32 FlagCount = 14;
	for (int32 f = 0; f <= FlagCount; ++f)
	{
		const double T0 = static_cast<double>(f) / (FlagCount + 1);
		const double T1 = static_cast<double>(f + 1) / (FlagCount + 1);
		auto Sag = [&](double T) { return FMath::Lerp(G0, G1, T) - FVector(0.0, 0.0, 38.0 * 4.0 * T * (1.0 - T)); };
		B.AddBeam(Sag(T0), Sag(T1), 0.9, Cream * 0.8f);
		if (f == FlagCount) { continue; }
		const FVector Top0 = Sag(T0 + 0.18 / (FlagCount + 1)), Top1 = Sag(T1 - 0.18 / (FlagCount + 1));
		const FVector Tip = (Top0 + Top1) * 0.5 - FVector(0.0, 0.0, 26.0);
		B.AddTri(Top0, Top1, Tip, FVector(1.0, 0.0, 0.0), Flags[f % 4]);
		B.AddTri(Top0, Top1, Tip, FVector(-1.0, 0.0, 0.0), Flags[f % 4] * 0.8f);
	}

	// Estantería del fondo, pegada a la muralla: costados, fondo y cuatro baldas con lo que se vende.
	const FLinearColor ShelfWood = Pal(0x9A6538);
	const FLinearColor Tag = Pal(0xFFF6E8);
	B.AddBox(FVector(ShelfX - ShelfHalfD + 2.0, 0.0, ShelfH * 0.5), FVector(1.0, 0.0, 0.0), FVector(2.0, ShelfHalfW, ShelfH * 0.5), WoodDark);
	for (const double SideY : { -ShelfHalfW, ShelfHalfW })
	{
		B.AddBox(FVector(ShelfX, SideY, ShelfH * 0.5), FVector(1.0, 0.0, 0.0), FVector(ShelfHalfD, 4.0, ShelfH * 0.5), ShelfWood);
	}
	for (const double Z : ShelfZ)
	{
		B.AddBox(FVector(ShelfX, 0.0, Z), FVector(1.0, 0.0, 0.0), FVector(ShelfHalfD, ShelfHalfW, 3.0), ShelfWood);
	}
	// Balda baja: botes de pintura de los colores de la tortuga, con su chorretón.
	const uint32 Paints[9] = { 0x3A9A3F, 0xF4A261, 0x4CC9F0, 0xFF6FA8, 0x9B5DE5, 0xFFD23F, 0xE63946, 0x2EC4B6, 0x264653 };
	for (int32 p = 0; p < 9; ++p)
	{
		const FVector Pot(ShelfX + 2.0, -ShelfHalfW + 34.0 + p * (2.0 * ShelfHalfW - 68.0) / 8.0, ShelfZ[0] + 3.0);
		TNProcMesh::TNProcAddCylinder(B, Pot, Pot + FVector(0.0, 0.0, 17.0), 10.5, 10.5, 10, Pal(0xB8C0C8));
		TNProcMesh::TNProcAddCylinder(B, Pot + FVector(0.0, 0.0, 17.0), Pot + FVector(0.0, 0.0, 18.4), 10.2, 10.2, 10, Pal(Paints[p]));
		B.AddBox(Pot + FVector(10.2, 0.0, 12.0), FVector(1.0, 0.0, 0.0), FVector(0.8, 2.4, 6.0), Pal(Paints[p]));
	}
	// Balda del medio: caparazones de muestra y dos tarros de ojos.
	const uint32 ShellA[5] = { 0x2F7A34, 0xE9C46A, 0x4CC9F0, 0xFF6A52, 0x9B5DE5 };
	const uint32 ShellB[5] = { 0x1F5424, 0xB5651D, 0xFFFFFF, 0xFFD23F, 0x3DDC62 };
	for (int32 s = 0; s < 5; ++s)
	{
		AddShellDome(B, FVector(ShelfX + 2.0, -ShelfHalfW + 48.0 + s * 78.0, ShelfZ[1] + 3.0), 1.0, Pal(ShellA[s]), Pal(ShellB[s]));
	}
	for (int32 j = 0; j < 2; ++j)
	{
		const FVector Jar(ShelfX + 2.0, ShelfHalfW - 110.0 + j * 62.0, ShelfZ[1] + 3.0);
		TNProcMesh::TNProcAddCylinder(B, Jar, Jar + FVector(0.0, 0.0, 30.0), 14.0, 14.0, 12, Pal(0xCDEBF2));
		TNProcMesh::TNProcAddCylinder(B, Jar + FVector(0.0, 0.0, 30.0), Jar + FVector(0.0, 0.0, 35.0), 11.0, 11.0, 12, Pal(0xE63946));
		AddEyeball(B, Jar + FVector(6.0, -5.0, 38.0), 6.0, FVector(1.0, 0.0, 0.0), Pal(j ? 0x3DDC62 : 0x4CC9F0));
		AddEyeball(B, Jar + FVector(6.0, 5.5, 39.0), 6.0, FVector(1.0, 0.0, 0.0), Pal(j ? 0x3DDC62 : 0x4CC9F0));
	}
	// Balda de arriba: cascos (paja, marinero, corona, fiesta y hélice), dos veces.
	for (int32 h = 0; h < 8; ++h)
	{
		const FVector Hat(ShelfX + 2.0, -ShelfHalfW + 38.0 + h * (2.0 * ShelfHalfW - 76.0) / 7.0, ShelfZ[2] + 3.0);
		switch (h % 5)
		{
		case 0: AddStrawHat(B, Hat, 1.0); break;
		case 1: AddSailorCap(B, Hat, 1.2); break;
		case 2: AddCrown(B, Hat, 1.1); break;
		case 3: AddPartyHat(B, Hat, 1.1); break;
		default: AddPropellerCap(B, Hat, 1.1); break;
		}
	}
	// Etiquetas de precio colgando de las baldas y banderines en lo alto.
	for (int32 t = 0; t < 12; ++t)
	{
		const double Y = -ShelfHalfW + 30.0 + t * (2.0 * ShelfHalfW - 60.0) / 11.0;
		const double Z = ShelfZ[t % 3 + 1] - 8.0;
		B.AddBox(FVector(ShelfX + ShelfHalfD + 1.0, Y, Z), FVector(1.0, 0.0, 0.0), FVector(0.6, 6.0, 4.0), Tag);
		B.AddBox(FVector(ShelfX + ShelfHalfD + 1.8, Y + 2.5, Z), FVector(1.0, 0.0, 0.0), FVector(0.3, 1.6, 1.6), Coral);
	}

	// Lado izquierdo: perchero con sombreros colgados y un barril con una pala de playa.
	{
		const FVector Rack(CounterX - 40.0, -CanopyHalfWidth - 55.0, 0.0);
		TNProcMesh::TNProcAddCylinder(B, Rack, Rack + FVector(0.0, 0.0, 6.0), 26.0, 24.0, 10, WoodDark);
		B.AddBeam(Rack, Rack + FVector(0.0, 0.0, 190.0), 4.0, Wood);
		for (int32 k = 0; k < 4; ++k)
		{
			const double A = TNProcMap::TwoPi * k / 4.0 + 0.4;
			const FVector Peg = Rack + FVector(0.0, 0.0, 150.0 + 18.0 * (k % 2));
			const FVector PegTip = Peg + FVector(FMath::Cos(A) * 30.0, FMath::Sin(A) * 30.0, 12.0);
			B.AddBeam(Peg, PegTip, 2.2, WoodDark);
			const FVector HatAt = PegTip - FVector(0.0, 0.0, 6.0);
			if (k == 0) { AddStrawHat(B, HatAt, 0.9); }
			else if (k == 1) { AddSailorCap(B, HatAt, 1.0); }
			else if (k == 2) { AddPartyHat(B, HatAt, 0.9); }
			else { AddPropellerCap(B, HatAt, 0.9); }
		}
		const FVector Barrel(CanopyBackX + 40.0, -CanopyHalfWidth - 60.0, 0.0);
		TNProcMesh::TNProcAddCylinder(B, Barrel, Barrel + FVector(0.0, 0.0, 74.0), 30.0, 30.0, 12, Wood);
		for (const double Z : { 12.0, 62.0 })
		{
			TNProcMesh::TNProcAddCylinder(B, Barrel + FVector(0.0, 0.0, Z - 2.5), Barrel + FVector(0.0, 0.0, Z + 2.5), 31.0, 31.0, 12, Pal(0x3B3F4A));
		}
		B.AddBeam(Barrel + FVector(0.0, 0.0, 70.0), Barrel + FVector(18.0, 10.0, 150.0), 2.6, Pal(0xFFD23F));
		B.AddBox(Barrel + FVector(21.0, 12.0, 160.0), FVector(0.23, 0.12, 0.96).GetSafeNormal(), FVector(15.0, 11.0, 1.5), Pal(0xFF6A52));
	}

	// Lado derecho: cofre del tesoro con monedas, pila de cajas con conchas y un farol.
	{
		const FVector Crates(CanopyBackX + 45.0, CanopyHalfWidth + 62.0, 0.0);
		B.AddBox(Crates + FVector(0.0, 0.0, 30.0), FVector(1.0, 0.0, 0.0), FVector(34.0, 34.0, 30.0), Wood);
		B.AddBox(Crates + FVector(-4.0, 6.0, 82.0), FVector(0.98, 0.2, 0.0).GetSafeNormal(), FVector(27.0, 27.0, 22.0), WoodTop);
		for (int32 s = 0; s < 4; ++s)
		{
			const double A = s * 1.6;
			TNCastleKit::AddScallop(B, Crates + FVector(12.0 * FMath::Cos(A) - 4.0, 12.0 * FMath::Sin(A) + 6.0, 104.5), FVector(0.0, 0.0, 1.0),
				FVector(FMath::Cos(A), FMath::Sin(A), 0.0), 11.0, (s % 2) ? Pal(0xFFE0C2) : Pal(0xFFB4A2));
		}
		const FVector Lamp(CounterX + 40.0, CanopyHalfWidth + 40.0, 0.0);
		B.AddBeam(Lamp, Lamp + FVector(0.0, 0.0, 210.0), 3.2, Pal(0x3B3F4A));
		B.AddBeam(Lamp + FVector(0.0, 0.0, 205.0), Lamp + FVector(28.0, 0.0, 205.0), 2.2, Pal(0x3B3F4A));
		TNProcMesh::TNProcAddCylinder(B, Lamp + FVector(28.0, 0.0, 170.0), Lamp + FVector(28.0, 0.0, 198.0), 9.0, 9.0, 8, Pal(0xFFE27A));
		TNProcMesh::TNProcAddCylinder(B, Lamp + FVector(28.0, 0.0, 198.0), Lamp + FVector(28.0, 0.0, 206.0), 11.0, 3.0, 8, Pal(0x3B3F4A));
	}

	// Cofre del tesoro con monedas a un lado del mostrador.
	const FVector Chest(CounterX - 10.0, CanopyHalfWidth + 70.0, 0.0);
	B.AddBox(Chest + FVector(0.0, 0.0, 26.0), FVector(1.0, 0.0, 0.0), FVector(30.0, 40.0, 26.0), Wood);
	B.AddBox(Chest + FVector(-4.0, 0.0, 58.0), FVector(1.0, 0.0, 0.0), FVector(28.0, 41.0, 6.0), WoodDark);
	B.AddBox(Chest + FVector(30.5, 0.0, 26.0), FVector(1.0, 0.0, 0.0), FVector(1.0, 7.0, 9.0), Gold);
	for (int32 c = 0; c < 9; ++c)
	{
		const double A = c * 0.8;
		TNProcMesh::TNProcAddCylinder(B, Chest + FVector(8.0 + 12.0 * FMath::Cos(A), 14.0 * FMath::Sin(A), 52.0 + (c % 3) * 1.4),
			Chest + FVector(8.0 + 12.0 * FMath::Cos(A), 14.0 * FMath::Sin(A), 53.8 + (c % 3) * 1.4), 5.0, 5.0, 8, Gold);
	}

	UMaterialInterface* VertexColorMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Cosmetics/Materials/M_CosmeticVertexColor.M_CosmeticVertexColor"));
	// El puesto entero es una pieza de arte; con StallScale se escala el componente, y con él la malla de arte.
	TNArt::SetMesh(Stall, TNProcRuntimeMesh::MakeStaticMesh(this, B, VertexColorMat), TN_ART("Lobby.Shop.Stall"));
}

void ATN_ShopKeeper::HideBlockoutKeeper()
{
	// El tendero de la maqueta (una tortuga suelta con la misma malla) queda debajo de este: se esconde en cada máquina.
	for (TActorIterator<ASkeletalMeshActor> It(GetWorld()); It; ++It)
	{
		ASkeletalMeshActor* Blockout = *It;
		const USkeletalMeshComponent* Comp = Blockout ? Blockout->GetSkeletalMeshComponent() : nullptr;
		const USkinnedAsset* Asset = Comp ? Comp->GetSkinnedAsset() : nullptr;
		if (TNTurtleArt::IsTurtleMesh(Asset) && FVector::Dist2D(Blockout->GetActorLocation(), GetActorLocation()) < 150.0)
		{
			Blockout->SetActorHiddenInGame(true);
			Blockout->SetActorEnableCollision(false);
		}
	}
}

void ATN_ShopKeeper::OnInteracted_Implementation(APawn* Interactor)
{
	Super::OnInteracted_Implementation(Interactor);
	if (!HasAuthority() || !Interactor) { return; }
	if (AMP_GamePlayerController* PC = Cast<AMP_GamePlayerController>(Interactor->GetController()))
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Tienda] %s abre la tienda."), *GetNameSafe(Interactor));
		PC->ClientOpenShop(this);
	}
}

void ATN_ShopKeeper::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GetNetMode() == NM_DedicatedServer) { return; }

	// Se gira hacia el jugador local si está cerca y le saluda de vez en cuando.
	// Con la pantalla partida (#311), a la tortuga local más cercana.
	const APawn* LocalPawn = TNLocalViews::ClosestLocalPawn(GetWorld(), GetActorLocation());
	float TargetYaw = 0.f;
	if (LocalPawn)
	{
		const FVector Local = GetActorTransform().InverseTransformPosition(LocalPawn->GetActorLocation());
		const float Dist = Local.Size2D();
		if (Dist < 900.f && Local.X > -50.f)
		{
			TargetYaw = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X)), -55.f, 55.f);
			if (Dist < 650.f && WaveCooldown <= 0.f && WaveAnim)
			{
				UTN_NpcAnimInstance::PlayGestureOn(Keeper, WaveAnim);
				WaveTimeLeft = WaveAnim->GetPlayLength();
				WaveCooldown = 12.f;
			}
		}
	}
	LookYaw = FMath::FInterpTo(LookYaw, TargetYaw, DeltaSeconds, 3.f);
	Keeper->SetRelativeTransform(KeeperTransform());
	WaveCooldown -= DeltaSeconds;
	if (WaveTimeLeft > 0.f)
	{
		WaveTimeLeft -= DeltaSeconds;
		if (WaveTimeLeft <= 0.f) { UTN_NpcAnimInstance::ReturnToIdleOn(Keeper, IdleAnim); }
	}
}
