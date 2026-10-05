#include "Lobby/TN_GeneralBriefing.h"
#include "Core/TN_ProjectMaterials.h"
#include "Art/TN_Art.h"
#include "Multiplayer/TN_LocalViews.h"
#include "Art/TN_TurtleArt.h"
#include "Core/TN_CosmeticLook.h"
#include "Core/TN_Log.h"
#include "Lobby/TN_LobbyMission.h"
#include "Lobby/TN_NpcAnimInstance.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Player/MP_GamePlayerController.h"
#include "Settings/TN_LanguageSettings.h"
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
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "../World/ProcMap/TN_ProcMapRuntimeMesh.h"

namespace TNGeneralDetail
{
	/** Color sRGB 0xRRGGBB para mallas en ejecución con M_CosmeticVertexColor (como en el puesto del tendero). */
	FLinearColor Pal(uint32 Hex)
	{
		return FLinearColor(TNProcRuntimeMesh::SRGBToLinear(((Hex >> 16) & 255) / 255.f), TNProcRuntimeMesh::SRGBToLinear(((Hex >> 8) & 255) / 255.f),
			TNProcRuntimeMesh::SRGBToLinear((Hex & 255) / 255.f), 1.f);
	}

	/** Mesa de madera delante del general (en su +X). */
	constexpr double TableX = 150.0;
	constexpr double TableHalfDepth = 70.0;
	constexpr double TableHalfWidth = 125.0;
	constexpr double TableTop = 92.0;
	/** Maqueta: el lobby (±2500 cm) a escala 1:45 sobre la mesa; la salida del castillo mira al general. */
	constexpr double ModelScale = 0.022;
	constexpr double ModelBase = TableTop + 0.8;
	/**
	 * Cartel clavado en el frontón de la tienda militar, sobre la entrada (el poste de delante pasa por detrás): bajo y
	 * estrecho para quedar entero dentro del triángulo del frontón (sin asomar por encima del tejado).
	 */
	constexpr double SignX = 290.0;
	constexpr double SignZ = 295.0;
	constexpr double SignHalfW = 122.0;
	constexpr double SignHalfH = 24.0;
	/**
	 * Pizarra de la orden del día (la misión de la próxima partida) en un caballete junto a la mesa, dentro de la tienda
	 * y mirando a los reclutas (+X): centro, medio ancho y medio alto de la pizarra y tamaño de la tiza.
	 */
	constexpr double BoardX = 238.0;
	constexpr double BoardY = -215.0;
	constexpr double BoardZ = 150.0;
	constexpr double BoardHalfW = 68.0;
	constexpr double BoardHalfH = 40.0;
	constexpr float BoardChalkSize = 13.f;

	/** Punto del lobby (cm, x a la izquierda de la salida e y hacia la salida) sobre la maqueta, a altura Z sobre su base. */
	FVector ModelPoint(double LobbyX, double LobbyY, double Z)
	{
		return FVector(TableX - LobbyY * ModelScale, LobbyX * ModelScale, ModelBase + Z);
	}

	/** Tramo de muralla de arena con almenas entre dos puntos del lobby (la puerta se deja con dos tramos). */
	void AddWall(TNProcMesh::FTNProcMeshBuffers& B, double Ax, double Ay, double Bx, double By, const FLinearColor& Sand, const FLinearColor& SandDark)
	{
		const FVector A = ModelPoint(Ax, Ay, 0.0);
		const FVector C = ModelPoint(Bx, By, 0.0);
		const FVector Dir = (C - A).GetSafeNormal2D();
		const double Len = FVector::Dist2D(A, C);
		B.AddBox((A + C) * 0.5 + FVector(0.0, 0.0, 3.4), Dir, FVector(Len * 0.5, 1.2, 3.0), Sand);
		const int32 Teeth = FMath::Max(1, static_cast<int32>(Len / 3.2));
		for (int32 t = 0; t < Teeth; t += 2)
		{
			const FVector P = A + Dir * ((t + 0.5) * Len / Teeth);
			B.AddBox(P + FVector(0.0, 0.0, 7.0), Dir, FVector(Len / Teeth * 0.5, 1.3, 0.8), SandDark);
		}
	}

	void AddTower(TNProcMesh::FTNProcMeshBuffers& B, double Lx, double Ly, double Height, const FLinearColor& Sand, const FLinearColor& SandDark)
	{
		const FVector Base = ModelPoint(Lx, Ly, 0.0);
		TNProcMesh::TNProcAddCylinder(B, Base, Base + FVector(0.0, 0.0, Height), 4.2, 3.6, 10, Sand);
		for (int32 k = 0; k < 6; ++k)
		{
			const double A = k * TNProcMap::TwoPi / 6.0;
			const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0.0);
			B.AddBox(Base + Dir * 3.2 + FVector(0.0, 0.0, Height + 0.8), Dir, FVector(0.7, 0.9, 0.8), SandDark);
		}
	}

	void AddPin(TNProcMesh::FTNProcMeshBuffers& B, const FVector& Foot, const FLinearColor& Color, const FLinearColor& Stick)
	{
		B.AddBeam(Foot, Foot + FVector(0.0, 0.0, 9.0), 0.25, Stick);
		const FVector Top = Foot + FVector(0.0, 0.0, 9.0);
		B.AddTri(Top, Top - FVector(0.0, 0.0, 3.0), Top + FVector(0.0, 4.0, -1.5), FVector(1.0, 0.0, 0.0), Color);
		B.AddTri(Top, Top - FVector(0.0, 0.0, 3.0), Top + FVector(0.0, 4.0, -1.5), FVector(-1.0, 0.0, 0.0), Color * 0.85f);
	}

	void AddEgg(TNProcMesh::FTNProcMeshBuffers& B, const FVector& Base, double Size, uint32 Seed, const FLinearColor& Color)
	{
		const TArray<double> Z = { 0.0, 0.3 * Size, 0.65 * Size, 1.0 * Size, 1.3 * Size };
		const TArray<double> R = { 0.3 * Size, 0.48 * Size, 0.46 * Size, 0.34 * Size, 0.14 * Size };
		TNProcMesh::TNProcAddLathe(B, Base, Z, R, 0.0, Seed, Color, 10, 0.3);
	}
}

ATN_GeneralBriefing::ATN_GeneralBriefing()
{
	using namespace TNGeneralDetail;
	PrimaryActorTick.bCanEverTick = true;
	NetDormancy = DORM_Awake;
	bAlwaysRelevant = true;
	CooldownSeconds = 0.6f;
	PromptText = NSLOCTEXT("Tortunabo", "GeneralPrompt", "Hablar con el general");
	GeneralName = NSLOCTEXT("Tortunabo", "GeneralName", "General Galápago");
	HeadquartersName = NSLOCTEXT("Tortunabo", "GeneralHQ", "Cuartel general");
	GeneralLook.HelmetId = TEXT("Helmet_Captain");
	GeneralLook.ShellId = TEXT("Shell_Moss");
	GeneralLook.SkinId = TEXT("Body_Forest");

	// Hitbox de interacción (invisible) delante de la mesa; el aviso flota sobre él.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Mesh && Cylinder.Succeeded())
	{
		Mesh->SetStaticMesh(Cylinder.Object);
		Mesh->SetRelativeLocation(FVector(TableX + TableHalfDepth + 60.0, 0.0, 60.0));
		Mesh->SetRelativeScale3D(FVector(2.4f, 2.4f, 1.2f));
		// Tampoco se ve en el editor: el nivel se enseña tal cual se juega.
		Mesh->SetVisibility(false);
		Mesh->SetHiddenInGame(true);
	}
	if (PromptWidgetComponent)
	{
		PromptWidgetComponent->SetUsingAbsoluteScale(true);
		PromptWidgetComponent->SetRelativeLocation(FVector(0.f, 0.f, 160.f));
	}

	General = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("General"));
	General->SetupAttachment(SceneRoot);
	General->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	General->SetRelativeScale3D(FVector(GeneralScale));
	General->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	General->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	// La malla es la del personaje de la tortuga (TNTurtleArt::ApplyBody en DressGeneral), no una ruta fija.

	GeneralHat = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GeneralHat"));
	GeneralHat->SetupAttachment(General);
	GeneralHat->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	GeneralBlock = CreateDefaultSubobject<UCapsuleComponent>(TEXT("GeneralBlock"));
	GeneralBlock->SetupAttachment(SceneRoot);
	GeneralBlock->InitCapsuleSize(55.f, 95.f);
	GeneralBlock->SetRelativeLocation(FVector(0.f, 0.f, 95.f));
	GeneralBlock->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	GeneralBlock->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	Table = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Table"));
	Table->SetupAttachment(SceneRoot);
	Table->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	TableBlock = CreateDefaultSubobject<UBoxComponent>(TEXT("TableBlock"));
	TableBlock->SetupAttachment(SceneRoot);
	TableBlock->InitBoxExtent(FVector(TableHalfDepth + 2.0, TableHalfWidth + 2.0, TableTop * 0.5));
	TableBlock->SetRelativeLocation(FVector(TableX, 0.0, TableTop * 0.5));
	TableBlock->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	TableBlock->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	// Cartel detrás del general: mira hacia los reclutas (+X).
	Sign = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Sign"));
	Sign->SetupAttachment(SceneRoot);
	Sign->SetRelativeLocation(FVector(SignX + 6.5, 0.0, SignZ));
	Sign->SetHorizontalAlignment(EHTA_Center);
	Sign->SetVerticalAlignment(EVRTA_TextCenter);
	Sign->SetWorldSize(36.f);
	Sign->SetTextRenderColor(FColor(255, 214, 90));
	Sign->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// La orden del día escrita con tiza en la pizarra del caballete (la pizarra es parte de la malla de la mesa).
	MissionBoard = CreateDefaultSubobject<UTextRenderComponent>(TEXT("MissionBoard"));
	MissionBoard->SetupAttachment(SceneRoot);
	MissionBoard->SetRelativeLocation(FVector(BoardX + 3.6, BoardY, BoardZ));
	MissionBoard->SetHorizontalAlignment(EHTA_Center);
	MissionBoard->SetVerticalAlignment(EVRTA_TextCenter);
	MissionBoard->SetWorldSize(BoardChalkSize);
	MissionBoard->SetTextRenderColor(FColor(242, 240, 226));
	MissionBoard->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Luz cálida del farol de dentro de la tienda: se ve bien al general y la mesa. Con sombras y alcance corto, la lona la
	// tapa y no se sale por las paredes ni alumbra la muralla de detrás (solo asoma por la entrada).
	TentLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("TentLight"));
	TentLight->SetupAttachment(SceneRoot);
	TentLight->SetRelativeLocation(FVector(TableX - 40.0, 0.0, 285.0));
	TentLight->SetIntensityUnits(ELightUnits::Lumens);
	TentLight->SetIntensity(2000.f);
	TentLight->SetAttenuationRadius(430.f);
	TentLight->SetLightColor(FLinearColor(1.f, 0.78f, 0.5f));
	TentLight->SetCastShadows(true);
}

FTransform ATN_GeneralBriefing::GeneralTransform() const
{
	// Como en BP_TortugaCharacter: la malla de demo mira a su +Y; girada -90 mira a los reclutas (+X), y se gira hacia el
	// jugador. Con otra malla en el personaje, la misma diferencia (TNTurtleArt::GetCopyCorrection).
	return GeneralCorrection * FTransform(FRotator(0.f, -90.f + LookYaw, 0.f), FVector::ZeroVector, FVector(GeneralScale));
}

void ATN_GeneralBriefing::DressGeneral()
{
	// La tortuga del personaje (malla, materiales y escala) y sus animaciones de los ajustes de arte.
	GeneralCorrection = FTransform::Identity;
	if (TNTurtleArt::ApplyBody(General, GeneralTransform())) { GeneralDefaults.Reset(); }
	GeneralCorrection = TNTurtleArt::GetCopyCorrection();
	IdleAnim = TNTurtleArt::GetClip(ETNTurtleClip::Idle);
	SaluteAnim = TNTurtleArt::GetClip(ETNTurtleClip::Salute);
}

void ATN_GeneralBriefing::BeginPlay()
{
	Super::BeginPlay();
	DressGeneral();
	FitSignText();
	// Espera en bucle con el saludo militar fundido encima (sin cortes al empezar y acabar el gesto).
	UTN_NpcAnimInstance::SetupOn(General, IdleAnim);
	UTN_CosmeticLook::ApplyLook(this, General, GeneralHat, GeneralLook, GeneralDefaults);
	BuildTable();
	HideBlockout();
	// La misión del anfitrión (sobrevive a los viajes en su GameInstance); en los clientes llega replicada.
	if (HasAuthority())
	{
		SyncMissionFromGameInstance();
	}
	RefreshMissionBoard();
	LanguageHandle = TNLanguage::OnApplied().AddUObject(this, &ATN_GeneralBriefing::HandleLanguageApplied);
}

void ATN_GeneralBriefing::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	TNLanguage::OnApplied().Remove(LanguageHandle);
	LanguageHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

void ATN_GeneralBriefing::HandleLanguageApplied()
{
	FitSignText();
	RefreshMissionBoard();
}

void ATN_GeneralBriefing::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_GeneralBriefing, MissionMode);
	DOREPLIFETIME(ATN_GeneralBriefing, MissionDifficulty);
	DOREPLIFETIME(ATN_GeneralBriefing, MissionRallyVariant);
	DOREPLIFETIME(ATN_GeneralBriefing, MissionRallySeats);
}

void ATN_GeneralBriefing::SyncMissionFromGameInstance()
{
	if (!HasAuthority())
	{
		return;
	}
	const UMP_GameInstance* GI = Cast<UMP_GameInstance>(GetGameInstance());
	if (!GI)
	{
		return;
	}
	const FName RallyVariant = TNLobbyMission::GetHostMissionMap(this);
	const uint8 RallySeats = static_cast<uint8>(TNLobbyMission::GetHostRallySeats(this));
	if (MissionMode == GI->SelectedProcMode && MissionDifficulty == GI->SelectedProcDifficulty && MissionRallyVariant == RallyVariant
		&& MissionRallySeats == RallySeats)
	{
		return;
	}
	MissionMode = GI->SelectedProcMode;
	MissionDifficulty = GI->SelectedProcDifficulty;
	MissionRallyVariant = RallyVariant;
	MissionRallySeats = RallySeats;
	// En el servidor el RepNotify no salta solo; a los demás, cuanto antes.
	OnRep_Mission();
	ForceNetUpdate();
}

void ATN_GeneralBriefing::OnRep_Mission()
{
	RefreshMissionBoard();
}

void ATN_GeneralBriefing::RefreshMissionBoard()
{
	using namespace TNGeneralDetail;
	if (!MissionBoard)
	{
		return;
	}
	MissionBoard->SetText(FText::Format(NSLOCTEXT("Tortunabo", "GeneralMissionBoard", "ORDEN DEL DÍA<br>MISIÓN: {0}<br>DIFICULTAD: {1}"),
		TNLobbyMission::MissionTitle(MissionMode, MissionRallyVariant).ToUpper(), TNLobbyMission::DifficultyName(MissionDifficulty).ToUpper()));
	// Siempre dentro de la pizarra, con un margen de tiza alrededor.
	MissionBoard->SetWorldSize(BoardChalkSize);
	const FVector TextSize = MissionBoard->GetTextLocalSize();
	const double MaxWidth = 2.0 * BoardHalfW - 16.0;
	const double MaxHeight = 2.0 * BoardHalfH - 12.0;
	double Fit = 1.0;
	if (TextSize.Y > MaxWidth) { Fit = FMath::Min(Fit, MaxWidth / TextSize.Y); }
	if (TextSize.Z > MaxHeight) { Fit = FMath::Min(Fit, MaxHeight / TextSize.Z); }
	if (Fit < 1.0)
	{
		MissionBoard->SetWorldSize(static_cast<float>(BoardChalkSize * Fit));
	}
}

void ATN_GeneralBriefing::FitSignText()
{
	using namespace TNGeneralDetail;
	// El rótulo cabe siempre dentro del cartel (también en el editor): parte del tamaño de siempre y se encoge si el
	// nombre es largo, con un margen a cada lado.
	// El servidor dedicado no carga los componentes sin colisión (UPrimitiveComponent::NeedsLoadForServer): sin cartel (#658).
	if (!Sign) { return; }
	Sign->SetText(HeadquartersName.ToUpper());
	Sign->SetWorldSize(36.f);
	const double MaxWidth = 2.0 * SignHalfW - 34.0;
	const double Width = Sign->GetTextLocalSize().Y;
	if (Width > MaxWidth)
	{
		Sign->SetWorldSize(static_cast<float>(36.0 * MaxWidth / Width));
	}
}

void ATN_GeneralBriefing::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	DressGeneral();
	FitSignText();
	RefreshMissionBoard();
	UTN_CosmeticLook::ApplyLook(this, General, GeneralHat, GeneralLook, GeneralDefaults);
	// En el editor, el general en su espera (no en T).
	UTN_NpcAnimInstance::PreviewInEditor(General, IdleAnim);
	BuildTable();
}

void ATN_GeneralBriefing::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
#if WITH_EDITOR
	// Al abrir el nivel en el editor, la tienda y la mesa (malla transitoria) llegan vacías: se rehacen.
	if (!IsTemplate() && GetWorld() && GetWorld()->WorldType == EWorldType::Editor && Table && !Table->GetStaticMesh())
	{
		DressGeneral();
		UTN_CosmeticLook::ApplyLook(this, General, GeneralHat, GeneralLook, GeneralDefaults);
		UTN_NpcAnimInstance::PreviewInEditor(General, IdleAnim);
		BuildTable();
	}
#endif
}

FVector ATN_GeneralBriefing::GetInteractionPoint() const
{
	using namespace TNGeneralDetail;
	return GetActorTransform().TransformPosition(FVector(TableX + TableHalfDepth + 60.0, 0.0, 0.0));
}

void ATN_GeneralBriefing::BuildTable()
{
	using namespace TNGeneralDetail;
	TNProcMesh::FTNProcMeshBuffers B;
	const FVector AxisX(1.0, 0.0, 0.0);
	const FLinearColor Wood = Pal(0xB07A4A);
	const FLinearColor WoodDark = Pal(0x7A4E2B);
	const FLinearColor WoodTop = Pal(0xD9A066);
	const FLinearColor SeaCloth = Pal(0x3AA7C9);
	const FLinearColor SandLight = Pal(0xF5DDA8);
	const FLinearColor SandShade = Pal(0xD9B77A);
	const FLinearColor Cream = Pal(0xFFF6E0);
	const FLinearColor Coral = Pal(0xFF6A52);
	const FLinearColor Navy = Pal(0x12305A);
	const FLinearColor Gold = Pal(0xFFCB3D);
	const FLinearColor Bottle = Pal(0x3FA66B);
	const FLinearColor Teal = Pal(0x2EC4B6);

	// Mesa: tablero, faldón y cuatro patas torneadas.
	B.AddBox(FVector(TableX, 0.0, TableTop - 3.5), AxisX, FVector(TableHalfDepth, TableHalfWidth, 3.5), WoodTop);
	B.AddBox(FVector(TableX, 0.0, TableTop - 11.0), AxisX, FVector(TableHalfDepth - 6.0, TableHalfWidth - 6.0, 4.5), Wood);
	for (const double Sx : { -1.0, 1.0 })
	{
		for (const double Sy : { -1.0, 1.0 })
		{
			const FVector Foot(TableX + Sx * (TableHalfDepth - 10.0), Sy * (TableHalfWidth - 10.0), 0.0);
			TNProcMesh::TNProcAddCylinder(B, Foot, Foot + FVector(0.0, 0.0, TableTop - 7.0), 5.5, 4.0, 8, WoodDark);
		}
	}

	// Tapete azul (el mar) y la maqueta del castillo de arena encima.
	B.AddBox(FVector(TableX, 0.0, TableTop + 0.4), AxisX, FVector(TableHalfDepth - 6.0, 66.0, 0.4), SeaCloth);
	B.AddBox(ModelPoint(0.0, 0.0, 1.2), AxisX, FVector(57.0, 57.0, 1.2), SandLight);

	// Murallas con almenas (la puerta, en y = +2350, queda abierta en el centro) y torres en las esquinas y a la puerta.
	const double W = 2350.0;
	const double Gate = 360.0;
	AddWall(B, -W, -W, W, -W, SandLight, SandShade);
	AddWall(B, W, -W, W, W, SandLight, SandShade);
	AddWall(B, -W, W, -W, -W, SandLight, SandShade);
	AddWall(B, W, W, Gate, W, SandLight, SandShade);
	AddWall(B, -Gate, W, -W, W, SandLight, SandShade);
	for (const double Tx : { -W, W })
	{
		for (const double Ty : { -W, W })
		{
			AddTower(B, Tx, Ty, 10.0, SandLight, SandShade);
		}
	}
	AddTower(B, -Gate - 120.0, W, 12.0, SandLight, SandShade);
	AddTower(B, Gate + 120.0, W, 12.0, SandLight, SandShade);
	// Puerta abierta: dos hojas de madera giradas hacia dentro.
	for (const double Side : { -1.0, 1.0 })
	{
		// Hacia dentro del castillo es +X de la maqueta: cada hoja se abre hacia dentro y hacia su lado.
		const FVector Hinge = ModelPoint(Side * Gate, W, 0.0);
		const FVector Dir = FVector(0.8, Side * 0.6, 0.0).GetSafeNormal2D();
		B.AddBox(Hinge + Dir * 3.6 + FVector(0.0, 0.0, 3.0), Dir, FVector(3.6, 0.4, 3.0), WoodDark);
	}

	// Sala de espera con los huevos junto a la puerta.
	uint32 Seed = 11u;
	for (const FVector2D EggSpot : { FVector2D(-260.0, 1950.0), FVector2D(-90.0, 2080.0), FVector2D(90.0, 2080.0), FVector2D(260.0, 1950.0) })
	{
		AddEgg(B, ModelPoint(EggSpot.X, EggSpot.Y, 2.4), 3.2, Seed++, Cream);
	}
	AddPin(B, ModelPoint(0.0, 1820.0, 2.4), Gold, WoodDark);

	// Tienda a la izquierda: puesto con toldo de rayas.
	{
		const FVector Shop = ModelPoint(900.0, 1500.0, 2.4);
		B.AddBox(Shop + FVector(0.0, 0.0, 1.2), AxisX, FVector(2.2, 3.6, 1.2), Pal(0xB07A4A));
		for (int32 k = 0; k < 4; ++k)
		{
			const double Y0 = -4.0 + k * 2.0;
			B.AddBox(Shop + FVector(-0.4, Y0 + 1.0, 4.6), AxisX, FVector(2.8, 1.0, 0.3), (k % 2) ? Cream : Coral);
		}
		AddPin(B, Shop + FVector(0.0, 0.0, 5.0), Coral, WoodDark);
	}

	// El general y su mesa a la derecha, y los probadores (botellas) detrás.
	{
		const FVector Here = ModelPoint(-892.0, 1479.0, 2.4);
		B.AddBox(Here + FVector(0.0, 0.0, 1.0), AxisX, FVector(1.6, 2.6, 1.0), WoodDark);
		AddPin(B, Here + FVector(-2.0, 0.0, 2.0), Navy, WoodDark);
	}
	for (const FVector2D BottleSpot : { FVector2D(-1400.0, 1100.0), FVector2D(-1800.0, 700.0), FVector2D(-1600.0, 200.0), FVector2D(-1800.0, -300.0) })
	{
		const FVector Foot = ModelPoint(BottleSpot.X, BottleSpot.Y, 2.4);
		TNProcMesh::TNProcAddCylinder(B, Foot, Foot + FVector(0.0, 0.0, 3.6), 1.5, 1.5, 8, Bottle);
		TNProcMesh::TNProcAddCylinder(B, Foot + FVector(0.0, 0.0, 3.6), Foot + FVector(0.0, 0.0, 5.4), 1.5, 0.6, 8, Bottle);
		TNProcMesh::TNProcAddCylinder(B, Foot + FVector(0.0, 0.0, 5.4), Foot + FVector(0.0, 0.0, 6.2), 0.6, 0.6, 6, Coral);
	}

	// Parkour: escalones, pilares de cubo y una pasarela a lo largo de las murallas.
	for (int32 s = 0; s < 5; ++s)
	{
		const FVector Step = ModelPoint(1400.0 + s * 180.0, -1900.0 + s * 260.0, 2.4);
		B.AddBox(Step + FVector(0.0, 0.0, 0.6 + s * 0.55), AxisX, FVector(2.0, 2.0, 0.6 + s * 0.55), SandShade);
	}
	for (int32 p = 0; p < 4; ++p)
	{
		const FVector Pillar = ModelPoint(-1500.0 + p * 700.0, -1700.0 + (p % 2) * 300.0, 2.4);
		TNProcMesh::TNProcAddCylinder(B, Pillar, Pillar + FVector(0.0, 0.0, 3.0 + p * 0.8), 1.8, 2.2, 8, (p % 2) ? Teal : Coral);
	}
	B.AddBox(ModelPoint(2000.0, 0.0, 6.8), FVector(0.0, 1.0, 0.0), FVector(18.0, 0.8, 0.25), WoodTop);

	// Puntero de madera con la punta roja y una taza de café a un lado.
	B.AddBeam(FVector(TableX + 40.0, -104.0, TableTop + 1.2), FVector(TableX - 8.0, -72.0, TableTop + 1.6), 0.9, WoodDark);
	B.AddBeam(FVector(TableX - 8.0, -72.0, TableTop + 1.6), FVector(TableX - 13.0, -69.0, TableTop + 1.7), 1.0, Coral);
	{
		const FVector Mug(TableX + 30.0, 98.0, TableTop);
		TNProcMesh::TNProcAddCylinder(B, Mug, Mug + FVector(0.0, 0.0, 10.0), 5.0, 5.4, 12, Cream);
		B.AddBeam(Mug + FVector(0.0, 5.6, 7.5), Mug + FVector(0.0, 8.4, 5.0), 0.9, Cream);
		B.AddBeam(Mug + FVector(0.0, 8.4, 5.0), Mug + FVector(0.0, 5.6, 2.5), 0.9, Cream);
	}

	// Pizarra de la orden del día en su caballete (la tiza es MissionBoard): marco de madera, pizarra verde oscuro, dos
	// patas delante a los lados, una detrás y la repisa con dos tizas.
	{
		const FLinearColor Slate = Pal(0x2F4538);
		B.AddBox(FVector(BoardX - 1.0, BoardY, BoardZ), AxisX, FVector(2.5, BoardHalfW + 5.0, BoardHalfH + 5.0), Wood);
		B.AddBox(FVector(BoardX + 0.5, BoardY, BoardZ), AxisX, FVector(2.5, BoardHalfW, BoardHalfH), Slate);
		for (const double Side : { -1.0, 1.0 })
		{
			const FVector Foot(BoardX + 6.0, BoardY + Side * (BoardHalfW + 12.0), 0.0);
			const FVector Top(BoardX + 1.0, BoardY + Side * (BoardHalfW + 6.0), BoardZ + BoardHalfH + 10.0);
			B.AddBeam(Foot, Top, 2.2, WoodDark);
		}
		B.AddBeam(FVector(BoardX - 38.0, BoardY, 0.0), FVector(BoardX - 3.0, BoardY, BoardZ + BoardHalfH), 2.2, WoodDark);
		B.AddBox(FVector(BoardX + 5.0, BoardY, BoardZ - BoardHalfH - 5.0), AxisX, FVector(4.0, BoardHalfW + 2.0, 1.2), WoodDark);
		B.AddBeam(FVector(BoardX + 5.0, BoardY - 30.0, BoardZ - BoardHalfH - 3.2), FVector(BoardX + 5.0, BoardY - 20.0, BoardZ - BoardHalfH - 3.2), 0.9, Cream);
		B.AddBeam(FVector(BoardX + 5.0, BoardY + 12.0, BoardZ - BoardHalfH - 3.2), FVector(BoardX + 5.0, BoardY + 19.0, BoardZ - BoardHalfH - 3.2), 0.9, Gold);
	}

	// ── Tienda militar de lona verde oliva: techo a dos aguas, paredes, frontón con el cartel, faldón enrollado sobre
	// la entrada, lonas de las esquinas atadas hacia fuera, postes, vientos, sacos terreros y cajas. Abierta por delante
	// (hacia la mesa), con el general dentro.
	{
		constexpr double XB = -175.0, XF = 280.0, HalfW = 320.0, EaveZ = 185.0, RidgeZ = 440.0;
		const FLinearColor Olive = Pal(0x6E7B3C);
		const FLinearColor OliveIn = Pal(0x55602D);
		const FLinearColor PatchDark = Pal(0x56622E);
		const FLinearColor PatchLight = Pal(0x8A8F4E);
		const FLinearColor Khaki = Pal(0xC8B98A);
		const FLinearColor Rope = Pal(0xD8C8A0);
		auto Canvas = [&B, &Olive, &OliveIn](const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, const FVector& Out)
		{
			B.AddQuad(P0, P1, P2, P3, Out, Olive);
			B.AddQuad(P0, P1, P2, P3, -Out, OliveIn);
		};
		// Techo, paredes y pared de atrás (con su hastial).
		for (const double Side : { -1.0, 1.0 })
		{
			const FVector RoofOut(0.0, Side * (RidgeZ - EaveZ), HalfW);
			Canvas(FVector(XB, 0.0, RidgeZ), FVector(XF, 0.0, RidgeZ), FVector(XF, Side * HalfW, EaveZ), FVector(XB, Side * HalfW, EaveZ), RoofOut);
			Canvas(FVector(XB, Side * HalfW, 0.0), FVector(XF, Side * HalfW, 0.0), FVector(XF, Side * HalfW, EaveZ), FVector(XB, Side * HalfW, EaveZ), FVector(0.0, Side, 0.0));
			// Lona de la esquina de delante, recogida y atada hacia fuera.
			B.AddTri(FVector(XF, Side * HalfW, 0.0), FVector(XF, Side * HalfW, EaveZ), FVector(XF + 55.0, Side * (HalfW + 45.0), EaveZ * 0.35), FVector(1.0, Side, 0.0), Olive);
			B.AddTri(FVector(XF, Side * HalfW, 0.0), FVector(XF, Side * HalfW, EaveZ), FVector(XF + 55.0, Side * (HalfW + 45.0), EaveZ * 0.35), FVector(-1.0, -Side, 0.0), OliveIn);
			// Sacos terreros a los lados de la entrada.
			for (int32 Row = 0; Row < 3; ++Row)
			{
				for (int32 Bag = 0; Bag < 3 - Row; ++Bag)
				{
					const FVector BagC(XF + 70.0, Side * (HalfW - 20.0 - Bag * 58.0 - Row * 29.0), 12.0 + Row * 22.0);
					B.AddBox(BagC, FVector(1.0, 0.0, 0.0), FVector(26.0, 27.0, 11.0), (Bag + Row) % 2 ? Khaki : Khaki * 0.9f);
				}
			}
			// Cajas de madera al fondo.
			B.AddBox(FVector(XB + 55.0, Side * (HalfW - 60.0), 32.0), FVector(1.0, 0.0, 0.0), FVector(34.0, 34.0, 32.0), WoodDark);
			B.AddBox(FVector(XB + 55.0, Side * (HalfW - 60.0), 66.0), FVector(1.0, 0.0, 0.0), FVector(35.0, 35.0, 3.0), Wood);
			// Vientos: de las esquinas del alero a estacas en el suelo (los de atrás, cortos: la tienda va pegada a la muralla).
			for (const double X : { XB, XF })
			{
				const FVector Eave(X, Side * HalfW, EaveZ);
				const FVector Stake(X + (X > 0.0 ? 60.0 : -25.0), Side * (HalfW + 60.0), 0.0);
				B.AddBeam(Eave, Stake + FVector(0.0, 0.0, 12.0), 1.2, Rope);
				B.AddBox(Stake + FVector(0.0, 0.0, 8.0), FVector(1.0, 0.0, 0.0), FVector(3.0, 3.0, 9.0), WoodDark);
			}
		}
		Canvas(FVector(XB, -HalfW, 0.0), FVector(XB, HalfW, 0.0), FVector(XB, HalfW, EaveZ), FVector(XB, -HalfW, EaveZ), FVector(-1.0, 0.0, 0.0));
		B.AddTri(FVector(XB, -HalfW, EaveZ), FVector(XB, HalfW, EaveZ), FVector(XB, 0.0, RidgeZ), FVector(-1.0, 0.0, 0.0), Olive);
		B.AddTri(FVector(XB, -HalfW, EaveZ), FVector(XB, HalfW, EaveZ), FVector(XB, 0.0, RidgeZ), FVector(1.0, 0.0, 0.0), OliveIn);
		// Frontón de delante (por encima de la entrada) y el faldón enrollado.
		constexpr double GableZ = 250.0;
		const double GableHalf = HalfW * (RidgeZ - GableZ) / (RidgeZ - EaveZ);
		B.AddTri(FVector(XF, -GableHalf, GableZ), FVector(XF, GableHalf, GableZ), FVector(XF, 0.0, RidgeZ), FVector(1.0, 0.0, 0.0), Olive);
		B.AddTri(FVector(XF, -GableHalf, GableZ), FVector(XF, GableHalf, GableZ), FVector(XF, 0.0, RidgeZ), FVector(-1.0, 0.0, 0.0), OliveIn);
		TNProcMesh::TNProcAddCylinder(B, FVector(XF + 6.0, -GableHalf, GableZ - 6.0), FVector(XF + 6.0, GableHalf, GableZ - 6.0), 14.0, 14.0, 10, PatchDark);
		for (const double Y : { -GableHalf * 0.6, GableHalf * 0.6 })
		{
			B.AddBeam(FVector(XF + 4.0, Y, GableZ + 10.0), FVector(XF + 22.0, Y, GableZ - 22.0), 1.4, Rope);
		}
		// Manchas de camuflaje en el techo y las paredes (por fuera, un pelo separadas de la lona).
		for (int32 m = 0; m < 26; ++m)
		{
			const double U = FMath::Frac(m * 0.618034 + 0.13), V = FMath::Frac(m * 0.754877 + 0.41);
			const double Side = (m % 2) ? 1.0 : -1.0;
			const double X = FMath::Lerp(XB + 30.0, XF - 30.0, U);
			const double Sz = 26.0 + 16.0 * FMath::Frac(m * 0.31);
			const FLinearColor PatchC = (m % 3) ? PatchDark : PatchLight;
			if (m % 4 == 0)
			{
				// En la pared: rombo vertical.
				const double Z = FMath::Lerp(30.0, EaveZ - 30.0, V);
				const FVector C(X, Side * (HalfW + 1.0), Z);
				B.AddQuad(C + FVector(-Sz, 0.0, 0.0), C + FVector(0.0, 0.0, -Sz * 0.6), C + FVector(Sz, 0.0, 0.0), C + FVector(0.0, 0.0, Sz * 0.6), FVector(0.0, Side, 0.0), PatchC);
			}
			else
			{
				// En el techo: rombo sobre el faldón inclinado.
				const double T = FMath::Lerp(0.12, 0.88, V);
				const double Y = Side * HalfW * T;
				const double Z = FMath::Lerp(RidgeZ, EaveZ, T) + 1.2;
				const FVector C(X, Y, Z);
				const FVector Down = FVector(0.0, Side * HalfW, EaveZ - RidgeZ).GetSafeNormal();
				const FVector RoofOut(0.0, Side * (RidgeZ - EaveZ), HalfW);
				B.AddQuad(C + FVector(-Sz, 0.0, 0.0), C + Down * Sz * 0.6, C + FVector(Sz, 0.0, 0.0), C - Down * Sz * 0.6, RoofOut, PatchC);
			}
		}
		// Postes de delante y de atrás; el de delante sigue hasta la bandera.
		TNProcMesh::TNProcAddCylinder(B, FVector(XB, 0.0, 0.0), FVector(XB, 0.0, RidgeZ + 15.0), 5.0, 4.5, 8, Wood);
		TNProcMesh::TNProcAddCylinder(B, FVector(XF, 0.0, 0.0), FVector(XF, 0.0, RidgeZ + 230.0), 5.0, 4.0, 8, Wood);
		for (const double Side : { -1.0, 1.0 })
		{
			B.AddBeam(FVector(XF, 0.0, RidgeZ), FVector(XF + 150.0, Side * 190.0, 12.0), 1.2, Rope);
			B.AddBeam(FVector(XB, 0.0, RidgeZ), FVector(XB - 25.0, Side * 190.0, 12.0), 1.2, Rope);
		}
		// Farol colgado del caballete sobre la mesa (la luz es TentLight).
		{
			const FVector LampTop(TableX - 30.0, 0.0, RidgeZ - 6.0);
			const FLinearColor Iron = Pal(0x3B3F4A);
			B.AddBeam(LampTop, LampTop - FVector(0.0, 0.0, 92.0), 1.2, Rope);
			TNProcMesh::TNProcAddCylinder(B, LampTop - FVector(0.0, 0.0, 100.0), LampTop - FVector(0.0, 0.0, 90.0), 13.0, 3.0, 8, Iron);
			TNProcMesh::TNProcAddCylinder(B, LampTop - FVector(0.0, 0.0, 130.0), LampTop - FVector(0.0, 0.0, 100.0), 11.0, 11.0, 8, Pal(0xFFE27A));
			TNProcMesh::TNProcAddCylinder(B, LampTop - FVector(0.0, 0.0, 136.0), LampTop - FVector(0.0, 0.0, 130.0), 13.0, 13.0, 8, Iron);
		}
	}

	// Cartel clavado en el frontón (tablero azul marino con marco dorado) y bandera en lo alto del poste de delante.
	B.AddBox(FVector(SignX, 0.0, SignZ), AxisX, FVector(4.0, SignHalfW, SignHalfH), Navy);
	B.AddBox(FVector(SignX - 1.5, 0.0, SignZ), AxisX, FVector(4.0, SignHalfW + 7.0, SignHalfH + 7.0), Gold);
	{
		const FVector PoleFoot(280.0, 0.0, 440.0);
		const FVector PoleTop = PoleFoot + FVector(0.0, 0.0, 230.0);
		TNProcMesh::TNProcAddCylinder(B, PoleFoot, PoleTop, 4.0, 3.0, 8, Cream);
		TNProcMesh::TNProcAddCylinder(B, PoleTop, PoleTop + FVector(0.0, 0.0, 8.0), 6.0, 6.0, 8, Gold);
		const FVector F0 = PoleTop - FVector(0.0, 0.0, 12.0);
		const FVector F1 = F0 + FVector(0.0, 110.0, -8.0);
		const FVector F2 = F1 - FVector(0.0, 0.0, 70.0);
		const FVector F3 = F0 - FVector(0.0, 0.0, 74.0);
		B.AddQuad(F0, F1, F2, F3, FVector(1.0, 0.0, 0.0), Navy);
		B.AddQuad(F0, F1, F2, F3, FVector(-1.0, 0.0, 0.0), Navy * 0.85f);
		// Franja dorada en medio de la bandera (las dos caras).
		const FVector G0 = FMath::Lerp(F0, F3, 0.42), G1 = FMath::Lerp(F1, F2, 0.42);
		const FVector G2 = FMath::Lerp(F1, F2, 0.58), G3 = FMath::Lerp(F0, F3, 0.58);
		B.AddQuad(G0 + FVector(0.6, 0.0, 0.0), G1 + FVector(0.6, 0.0, 0.0), G2 + FVector(0.6, 0.0, 0.0), G3 + FVector(0.6, 0.0, 0.0), FVector(1.0, 0.0, 0.0), Gold);
		B.AddQuad(G0 - FVector(0.6, 0.0, 0.0), G1 - FVector(0.6, 0.0, 0.0), G2 - FVector(0.6, 0.0, 0.0), G3 - FVector(0.6, 0.0, 0.0), FVector(-1.0, 0.0, 0.0), Gold * 0.85f);
	}

	// Toda la tienda del general (mesa con la maqueta, pizarra del caballete, cartel y bandera) es una pieza de arte.
	TNArt::SetMesh(Table, TNProcRuntimeMesh::MakeStaticMesh(this, B, TNMaterials::VertexColor()), TN_ART("Lobby.Briefing.Tent"));
}

void ATN_GeneralBriefing::HideBlockout()
{
	// La tortuga de la maqueta queda debajo de este general y su mesa («Boolean», «Boolean2») al lado.
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
	for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
	{
		AStaticMeshActor* Piece = *It;
		const UStaticMeshComponent* Comp = Piece ? Piece->GetStaticMeshComponent() : nullptr;
		const UStaticMesh* PieceMesh = Comp ? Comp->GetStaticMesh() : nullptr;
		const bool bTablePiece = Piece && (Piece->GetName().Contains(TEXT("Boolean")) || (PieceMesh && PieceMesh->GetName().Contains(TEXT("Boolean"))));
		if (bTablePiece && FVector::Dist2D(Piece->GetActorLocation(), GetActorLocation()) < 400.0)
		{
			Piece->SetActorHiddenInGame(true);
			Piece->SetActorEnableCollision(false);
		}
	}
}

void ATN_GeneralBriefing::OnInteracted_Implementation(APawn* Interactor)
{
	Super::OnInteracted_Implementation(Interactor);
	if (!HasAuthority() || !Interactor) { return; }
	if (AMP_GamePlayerController* PC = Cast<AMP_GamePlayerController>(Interactor->GetController()))
	{
		UE_LOG(LogTortunabo, Log, TEXT("[General] %s abre la sesión informativa."), *GetNameSafe(Interactor));
		PC->ClientOpenBriefing(this);
	}
}

void ATN_GeneralBriefing::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GetNetMode() == NM_DedicatedServer) { return; }

	// Se gira hacia el jugador local si está cerca y le saluda de vez en cuando (a lo militar).
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
			if (Dist < 650.f && SaluteCooldown <= 0.f && SaluteAnim)
			{
				UTN_NpcAnimInstance::PlayGestureOn(General, SaluteAnim);
				SaluteTimeLeft = SaluteAnim->GetPlayLength();
				SaluteCooldown = 14.f;
			}
		}
	}
	LookYaw = FMath::FInterpTo(LookYaw, TargetYaw, DeltaSeconds, 3.f);
	General->SetRelativeTransform(GeneralTransform());
	SaluteCooldown -= DeltaSeconds;
	if (SaluteTimeLeft > 0.f)
	{
		SaluteTimeLeft -= DeltaSeconds;
		if (SaluteTimeLeft <= 0.f) { UTN_NpcAnimInstance::ReturnToIdleOn(General, IdleAnim); }
	}
}
