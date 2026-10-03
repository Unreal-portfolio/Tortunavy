#include "World/TN_TctArena.h"
#include "Core/TN_Log.h"
#include "Game/TN_TctGameState.h"
#include "Game/TN_TctRules.h"

#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace TNTctArenaDetail
{
	/** El mar del mapa procedural (animado). */
	const TCHAR* SeaMaterialPath = TEXT("/Game/ProcMap/Materials/MI_ProcSeaAnim.MI_ProcSeaAnim");
	const TCHAR* PlaneMeshPath = TEXT("/Engine/BasicShapes/Plane.Plane");
	/** Lado del plano básico del motor (uu). */
	constexpr double PlaneSize = 100.0;
	/** Suelo pisable: normal con Z de al menos esto (unos 40°). */
	constexpr double WalkableNormalZ = 0.75;
	/** Una muestra es sitio de salida si sus vecinas a esta fracción del paso están a menos de esto de altura. */
	constexpr double NeighbourStepFraction = 0.6;
	constexpr double NeighbourMaxRise = 60.0;
	/** Suelo a menos de esto por encima del mar no cuenta (orilla que se moja). */
	constexpr double ShoreMargin = 50.0;
}

ATN_TctArena::ATN_TctArena()
{
	using namespace TNTctArenaDetail;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);

	// Movable bajo una raíz Static se puede (al revés no): el mar sube y baja.
	WaterPlane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WaterPlane"));
	WaterPlane->SetupAttachment(RootComponent);
	WaterPlane->SetMobility(EComponentMobility::Movable);
	WaterPlane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WaterPlane->SetCastShadow(false);
	WaterPlane->SetGenerateOverlapEvents(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(PlaneMeshPath);
	if (PlaneMesh.Succeeded())
	{
		WaterPlane->SetStaticMesh(PlaneMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SeaMaterial(SeaMaterialPath);
	if (SeaMaterial.Succeeded())
	{
		WaterPlane->SetMaterial(0, SeaMaterial.Object);
	}
}

void ATN_TctArena::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_TctArena, ArenaVariant);
}

ATN_TctArena* ATN_TctArena::Find(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ATN_TctArena> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

bool ATN_TctArena::VariantExists(FName VariantName)
{
	return !VariantName.IsNone() && FPaths::FileExists(VariantsDir() / VariantName.ToString() / TEXT("manifest.json"));
}

void ATN_TctArena::BeginPlay()
{
	if (HasAuthority() && ArenaVariant.IsNone())
	{
		ArenaVariant = Variant;
	}
	// La base construye la malla si hace falta y, en el servidor, pone las zonas de muerte del manifest.
	Super::BeginPlay();
	FitWaterPlane();
}

void ATN_TctArena::ServerSetArenaVariant(FName NewVariant)
{
	if (!HasAuthority() || NewVariant.IsNone())
	{
		return;
	}
	ArenaVariant = NewVariant;
	if (Variant != NewVariant)
	{
		Variant = NewVariant;
		Recargar();
	}
	FitWaterPlane();
	ForceNetUpdate();
	UE_LOG(LogTortunabo, Log, TEXT("[TcT] Arena: %s"), *ArenaVariant.ToString());
}

void ATN_TctArena::OnRep_ArenaVariant()
{
	if (ArenaVariant.IsNone() || Variant == ArenaVariant)
	{
		return;
	}
	Variant = ArenaVariant;
	// Antes de BeginPlay no hace falta: BeginPlay la construye con la variante ya puesta.
	if (HasActorBegunPlay())
	{
		Recargar();
		FitWaterPlane();
	}
}

void ATN_TctArena::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GetNetMode() == NM_DedicatedServer || !WaterPlane)
	{
		return;
	}
	const UWorld* World = GetWorld();
	const ATN_TctGameState* State = World ? World->GetGameState<ATN_TctGameState>() : nullptr;
	const float WaterZ = State ? State->GetWaterZ() : BaseWaterZ;
	FVector Location = WaterPlane->GetComponentLocation();
	if (!FMath::IsNearlyEqual(Location.Z, static_cast<double>(WaterZ), 0.5))
	{
		Location.Z = WaterZ;
		WaterPlane->SetWorldLocation(Location);
	}
}

void ATN_TctArena::FitWaterPlane()
{
	using namespace TNTctArenaDetail;
	const TSharedPtr<FJsonObject> Manifest = ReadManifest();
	double WaterUu = 0.0;
	if (Manifest.IsValid() && Manifest->TryGetNumberField(TEXT("water_uu"), WaterUu))
	{
		BaseWaterZ = static_cast<float>(GetActorTransform().TransformPosition(FVector(0.0, 0.0, WaterUu)).Z);
	}
	else
	{
		BaseWaterZ = static_cast<float>(GetActorLocation().Z);
	}

	FBox Box(ForceInit);
	TArray<UProceduralMeshComponent*> Meshes;
	GetComponents(Meshes);
	for (const UProceduralMeshComponent* Mesh : Meshes)
	{
		if (Mesh && Mesh->IsCollisionEnabled())
		{
			Box += Mesh->Bounds.GetBox();
		}
	}
	GroundBox = Box;
	if (!WaterPlane || !Box.IsValid)
	{
		return;
	}
	const FVector Center = Box.GetCenter();
	const FVector Size = Box.GetSize();
	WaterPlane->SetWorldLocation(FVector(Center.X, Center.Y, BaseWaterZ));
	WaterPlane->SetWorldScale3D(FVector((Size.X + WaterPlaneMargin * 2.0) / PlaneSize, (Size.Y + WaterPlaneMargin * 2.0) / PlaneSize, 1.0));
}

bool ATN_TctArena::TraceGround(double X, double Y, double TopZ, double BottomZ, FVector& OutPoint) const
{
	using namespace TNTctArenaDetail;
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FHitResult Hit;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(TNTctArenaGround), true);
	if (!World->LineTraceSingleByChannel(Hit, FVector(X, Y, TopZ), FVector(X, Y, BottomZ), ECC_WorldStatic, Params))
	{
		return false;
	}
	// Solo el terreno de la arena (lo primero desde arriba) y pisable.
	if (Hit.GetActor() != this || Hit.ImpactNormal.Z < WalkableNormalZ)
	{
		return false;
	}
	OutPoint = Hit.ImpactPoint;
	return true;
}

bool ATN_TctArena::Survey(float SampleSpacing)
{
	using namespace TNTctArenaDetail;
	FitWaterPlane();
	SurveyHeights.Reset();
	SpawnCandidates.Reset();
	if (!GroundBox.IsValid || SampleSpacing <= 1.f)
	{
		UE_LOG(LogTortunabo, Error, TEXT("[TcT] La arena «%s» no tiene terreno con colisión: no se puede medir."), *ArenaVariant.ToString());
		return false;
	}

	const double TopZ = GroundBox.Max.Z + 500.0;
	const double BottomZ = GroundBox.Min.Z - 500.0;
	const double Step = SampleSpacing;
	const double Near = Step * NeighbourStepFraction;
	const double MinGroundZ = BaseWaterZ + ShoreMargin;
	for (double X = GroundBox.Min.X + Step * 0.5; X < GroundBox.Max.X; X += Step)
	{
		for (double Y = GroundBox.Min.Y + Step * 0.5; Y < GroundBox.Max.Y; Y += Step)
		{
			FVector Point;
			if (!TraceGround(X, Y, TopZ, BottomZ, Point) || Point.Z < MinGroundZ)
			{
				continue;
			}
			SurveyHeights.Add(static_cast<float>(Point.Z));

			// Sitio de salida: las cuatro vecinas también son suelo pisable a la misma altura (no es un borde ni una rampa).
			bool bInterior = true;
			const FVector2D Offsets[] = { { Near, 0.0 }, { -Near, 0.0 }, { 0.0, Near }, { 0.0, -Near } };
			for (const FVector2D& Offset : Offsets)
			{
				FVector Neighbour;
				if (!TraceGround(X + Offset.X, Y + Offset.Y, TopZ, BottomZ, Neighbour) || FMath::Abs(Neighbour.Z - Point.Z) > NeighbourMaxRise)
				{
					bInterior = false;
					break;
				}
			}
			if (bInterior)
			{
				SpawnCandidates.Add(Point);
			}
		}
	}
	UE_LOG(LogTortunabo, Log, TEXT("[TcT] Arena «%s» medida: %d muestras de suelo, %d sitios de salida posibles, mar a %.0f uu."),
		*ArenaVariant.ToString(), SurveyHeights.Num(), SpawnCandidates.Num(), BaseWaterZ);
	return SurveyHeights.Num() > 0;
}

TArray<FTransform> ATN_TctArena::PickSpawnTransforms(int32 Count, float CapsuleLift) const
{
	TArray<FTransform> Spawns;
	if (!GroundBox.IsValid)
	{
		return Spawns;
	}
	const FVector Center = GroundBox.GetCenter();
	for (const int32 Index : TNTctRules::PickSpreadPoints(SpawnCandidates, Count, Center))
	{
		const FVector Location = SpawnCandidates[Index] + FVector(0.0, 0.0, CapsuleLift);
		const FVector ToCenter = Center - Location;
		const FRotator Facing(0.0, FMath::RadiansToDegrees(FMath::Atan2(ToCenter.Y, ToCenter.X)), 0.0);
		Spawns.Add(FTransform(Facing, Location));
	}
	return Spawns;
}
