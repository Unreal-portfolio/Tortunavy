#include "World/TN_SupplyCrate.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TN_InventoryTypes.h"
#include "Core/TN_Log.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Game/TN_CoopItems.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraFunctionLibrary.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace TNSupplyCrateDetail
{
	/** Mallas de la caja y de su tapa (arte de María, sin material todavía) y la de reserva del motor. */
	const TCHAR* const CrateMeshPath = TEXT("/Game/Meshses/Assets/CajaMadera_V3.CajaMadera_V3");
	const TCHAR* const LidMeshPath = TEXT("/Game/Meshses/Assets/TapaCajaMadera_V3.TapaCajaMadera_V3");
	const TCHAR* const FallbackMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* const PlaceholderMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

	/** Medidas de CajaMadera_V3 (cm, pivote abajo en el centro): alto, y la bisagra de la tapa (borde -X, arriba). */
	constexpr float CrateHeight = 57.f;
	constexpr float LidHingeX = -40.4f;
	/** Huella para el aviso, el anillo y el escaneo: radio que abarca la caja (94 x 120 cm). */
	constexpr float FootRadius = 70.f;
	/** Segundos que tarda la tapa en abrirse. */
	constexpr float LidOpenSeconds = 0.45f;
	/** Polvo al abrirse: madera clara. */
	const FLinearColor Dust(0.62f, 0.48f, 0.32f);
}

ATN_SupplyCrate::ATN_SupplyCrate()
{
	using namespace TNSupplyCrateDetail;
	PromptText = NSLOCTEXT("Tortunabo", "SupplyCratePrompt", "Mantén para abrir");
	SearchSeconds = 1.5f;
	LootChance = 1.f;
	bRepeatable = false;
	// Más agudo que la arena: madera y chismes.
	RummagePitch = 1.35f;
	MarkerRadius = FootRadius + 20.f;
	DefaultLoot = TNLootRules::SupplyCrateDefaults();

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CrateFinder(CrateMeshPath);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> LidFinder(LidMeshPath);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> FallbackFinder(FallbackMeshPath);

	CrateMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CrateMesh"));
	CrateMesh->SetupAttachment(SceneRoot);
	CrateMesh->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	CrateMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	CrateMesh->SetGenerateOverlapEvents(false);
	CrateMesh->SetCanEverAffectNavigation(false);
	if (CrateFinder.Succeeded())
	{
		CrateMesh->SetStaticMesh(CrateFinder.Object);
	}
	else if (FallbackFinder.Succeeded())
	{
		// Cubo de 100 cm con el pivote en el centro: se sube para que apoye en el suelo y se aplasta a la altura de la caja.
		CrateMesh->SetStaticMesh(FallbackFinder.Object);
		CrateMesh->SetRelativeLocation(FVector(0.f, 0.f, CrateHeight * 0.5f));
		CrateMesh->SetRelativeScale3D(FVector(0.94f, 1.2f, CrateHeight / 100.f));
	}

	LidMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LidMesh"));
	LidMesh->SetupAttachment(SceneRoot);
	LidMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LidMesh->SetGenerateOverlapEvents(false);
	LidMesh->SetCanEverAffectNavigation(false);
	LidMesh->SetRelativeLocation(FVector(LidHingeX, 0.f, CrateHeight));
	if (LidFinder.Succeeded() && CrateFinder.Succeeded())
	{
		LidMesh->SetStaticMesh(LidFinder.Object);
	}
}

ATN_SupplyCrate* ATN_SupplyCrate::ServerSpawn(UWorld* World, const FVector& Location, float YawDeg)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATN_SupplyCrate* Crate = World->SpawnActor<ATN_SupplyCrate>(StaticClass(), FTransform(FRotator(0.f, YawDeg, 0.f), Location), Params);
	UE_LOG(LogTNLoot, Log, TEXT("Caja de suministros en %s: %s."), *Location.ToCompactString(), Crate ? TEXT("creada") : TEXT("no se ha podido crear"));
	return Crate;
}

void ATN_SupplyCrate::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		SetupSpot(TNSupplyCrateDetail::FootRadius, 0.f, TNSupplyCrateDetail::CrateHeight + 30.f, TNSupplyCrateDetail::Dust);
		// La chapa, ya ahora: al abrir no se carga nada del disco.
		const TSoftClassPtr<AActor>& ChapaClass = GetLootDef().ChapaClass;
		if (!ChapaClass.IsNull())
		{
			LoadedChapaClass = ChapaClass.LoadSynchronous();
		}
	}
	ApplyPlaceholderMaterial(CrateMesh);
	ApplyPlaceholderMaterial(LidMesh);
	SnapLid();
}

void ATN_SupplyCrate::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickLid(DeltaSeconds);
}

const FTNLootTableDef& ATN_SupplyCrate::GetLootDef() const
{
	return CrateLoot ? CrateLoot->Loot : DefaultLoot;
}

void ATN_SupplyCrate::OverrideLoot(const FTNLootTableDef& InLoot)
{
	CrateLoot = nullptr;
	DefaultLoot = InLoot;
	LoadedChapaClass = InLoot.ChapaClass.IsNull() ? nullptr : InLoot.ChapaClass.LoadSynchronous();
}

bool ATN_SupplyCrate::ServerOpen()
{
	if (!HasAuthority() || IsOpened() || !IsReadyToOpen())
	{
		return false;
	}
	FinishSearch();
	return true;
}

float ATN_SupplyCrate::GetLootWeight(FName RowName, const FTN_InventoryItem& Row) const
{
	const TMap<FName, float>& Weights = GetLootDef().ItemWeights;
	if (const float* ByRow = Weights.Find(RowName))
	{
		return *ByRow;
	}
	if (const float* ById = Weights.Find(Row.ItemId))
	{
		return *ById;
	}
	return Super::GetLootWeight(RowName, Row);
}

bool ATN_SupplyCrate::PickItem(FRandomStream& Stream, FTN_InventoryItem& OutItem) const
{
	return TNCoopItems::RollLoot(GetLootTable(), [this](FName RowName, const FTN_InventoryItem& Row) { return GetLootWeight(RowName, Row); },
		Stream.FRand(), OutItem);
}

AActor* ATN_SupplyCrate::SpawnSearchReward(APawn* Pawn, const FVector& From, FVector& OutLanding)
{
	// Una sola vez por partida: el rebuscable no repetible ya no deja volver a empezar, pero por si acaso.
	if (IsOpened())
	{
		return nullptr;
	}
	FRandomStream Stream(RollSeed != 0 ? RollSeed : FMath::Rand());
	LastRoll = TNLootRules::Roll(GetLootDef(), Stream);
	LastSpawnedChapas = 0;

	AActor* First = nullptr;
	int32 ItemsSpawned = 0;
	for (int32 i = 0; i < LastRoll.ItemCount; ++i)
	{
		FTN_InventoryItem Item;
		if (!PickItem(Stream, Item))
		{
			continue;
		}
		const FVector Landing = FindLanding(Pawn, From);
		AActor* Pickup = SpawnLoot(Item, Landing);
		if (!Pickup)
		{
			continue;
		}
		++ItemsSpawned;
		if (!First)
		{
			First = Pickup;
			OutLanding = Landing;
		}
	}

	AActor* FirstChapa = SpawnChapas(Pawn, From, LastRoll.ChapaCount);
	if (!First && FirstChapa)
	{
		First = FirstChapa;
		OutLanding = FirstChapa->GetActorLocation();
	}
	UE_LOG(LogTNLoot, Log, TEXT("%s abierta por %s: %d objetos (de %d) y %d chapas."), *GetName(), *GetNameSafe(Pawn), ItemsSpawned,
		LastRoll.ItemCount, LastRoll.ChapaCount);
	return First;
}

AActor* ATN_SupplyCrate::SpawnChapas(const APawn* Pawn, const FVector& From, int32 Count)
{
	if (Count <= 0)
	{
		return nullptr;
	}
	UClass* ChapaClass = LoadedChapaClass;
	if (!ChapaClass)
	{
		// Hasta que exista la chapa (#858) el reparto solo se registra: una línea por apertura, no un aviso en bucle.
		UE_LOG(LogTNLoot, Log, TEXT("%s: %d chapas sin soltar (la tabla no tiene clase de chapa todavía)."), *GetName(), Count);
		return nullptr;
	}
	UWorld* World = GetWorld();
	AActor* First = nullptr;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 i = 0; i < Count && World; ++i)
	{
		const FVector Landing = FindLanding(Pawn, From) + FVector(0.f, 0.f, 10.f);
		AActor* Chapa = World->SpawnActor<AActor>(ChapaClass, FTransform(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), Landing), Params);
		if (!Chapa)
		{
			continue;
		}
		++LastSpawnedChapas;
		First = First ? First : Chapa;
	}
	return First;
}

void ATN_SupplyCrate::OnSearchStateChanged(const FTNSearchSpotState& OldState)
{
	Super::OnSearchStateChanged(OldState);
	const bool bJustOpened = OldState.Outcome == ETNSearchOutcome::None && IsOpened();
	if (!bJustOpened)
	{
		return;
	}
	// Recién abierta (no al entrar tarde: entonces la tapa ya está arriba sin animar).
	const double Since = ServerNow() - static_cast<double>(GetSearchState().OutcomeTime);
	if (Since > 2.0)
	{
		SnapLid();
		return;
	}
	bLidAnimating = true;
	SetActorTickInterval(0.f);
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const FVector Top = GetActorLocation() + FVector(0.f, 0.f, TNSupplyCrateDetail::CrateHeight);
	if (OpenSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, OpenSound, Top);
	}
	if (OpenFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, OpenFX, Top);
	}
}

void ATN_SupplyCrate::SnapLid()
{
	LidOpen = IsOpened() ? 1.f : 0.f;
	bLidAnimating = false;
	if (LidMesh)
	{
		LidMesh->SetRelativeRotation(FRotator(LidOpen * LidOpenDegrees, 0.f, 0.f));
	}
}

void ATN_SupplyCrate::TickLid(float DeltaSeconds)
{
	if (!bLidAnimating)
	{
		return;
	}
	LidOpen = FMath::Min(1.f, LidOpen + DeltaSeconds / TNSupplyCrateDetail::LidOpenSeconds);
	// Sale de golpe y frena al final (la tapa se queda echada hacia atrás).
	const float Eased = 1.f - FMath::Square(1.f - LidOpen);
	if (LidMesh)
	{
		LidMesh->SetRelativeRotation(FRotator(Eased * LidOpenDegrees, 0.f, 0.f));
	}
	bLidAnimating = LidOpen < 1.f;
}

void ATN_SupplyCrate::ApplyPlaceholderMaterial(UStaticMeshComponent* Target) const
{
	const UStaticMesh* Asset = Target ? Target->GetStaticMesh() : nullptr;
	if (!Asset || GetNetMode() == NM_DedicatedServer || Asset->GetMaterial(0))
	{
		return;
	}
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TNSupplyCrateDetail::PlaceholderMaterialPath, nullptr, LOAD_NoWarn);
	if (!Base)
	{
		return;
	}
	UMaterialInstanceDynamic* Wood = UMaterialInstanceDynamic::Create(Base, Target);
	Wood->SetVectorParameterValue(TEXT("Color"), PlaceholderWood);
	Target->SetMaterial(0, Wood);
}
