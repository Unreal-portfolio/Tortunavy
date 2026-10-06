#include "World/TN_TctArena.h"
#include "Core/TN_Log.h"
#include "Game/TN_TctGameState.h"
#include "Game/TN_TctItems.h"
#include "Game/TN_TctRules.h"
#include "World/TN_TctScenery.h"

#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Misc/Crc.h"
#include "World/ProcMap/TN_ProcMapMath.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace TNTctArenaDetail
{
	/** El mar del mapa procedural (animado). */
	const TCHAR* SeaMaterialPath = TEXT("/Game/ProcMap/Materials/MI_ProcSeaAnim.MI_ProcSeaAnim");
	/** La arena de la playa del Rally y del Coop (grano triplanar, rizos y arena mojada sobre el color de vértice). */
	const TCHAR* SandMaterialPath = TEXT("/Game/Blueprints/Gameplay/GridMap/M_GridTerrainWet.M_GridTerrainWet");
	const TCHAR* PlaneMeshPath = TEXT("/Engine/BasicShapes/Plane.Plane");
	/** El mar plano y translúcido del mapa procedural (Color y Opacity): la marca del nivel que alcanzará el agua. */
	const TCHAR* MarkerMaterialPath = TEXT("/Game/ProcMap/Materials/MI_ProcSea.MI_ProcSea");
	/** Colores tóxicos del mar animado (ShallowColor, DeepColor y FoamColor de M_ProcWaterAnim). */
	const FLinearColor ToxicShallow(0.42f, 0.82f, 0.12f);
	const FLinearColor ToxicDeep(0.12f, 0.38f, 0.06f);
	const FLinearColor ToxicFoam(0.86f, 1.f, 0.42f);
	const FLinearColor MarkerColor(0.55f, 1.f, 0.1f);
	/** Lado del plano básico del motor (uu). */
	constexpr double PlaneSize = 100.0;
	/** Suelo pisable: normal con Z de al menos esto (unos 40°). */
	constexpr double WalkableNormalZ = 0.75;
	/** Una muestra es sitio de salida si sus vecinas a esta fracción del paso están a menos de esto de altura. */
	constexpr double NeighbourStepFraction = 0.6;
	constexpr double NeighbourMaxRise = 60.0;
	/** Suelo a menos de esto por encima del mar no cuenta (orilla que se moja). */
	constexpr double ShoreMargin = 50.0;
	/** Exposición: cuántas muestras hacia fuera se busca un desnivel y cuánto de bajada cuenta como vacío (uu). */
	constexpr int32 ExposureRings = 6;
	constexpr double ExposureDrop = 150.0;
}

ATN_TctArena::ATN_TctArena()
{
	using namespace TNTctArenaDetail;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	// El agua es veneno (#831): sin las zonas de muerte del fondo del manifest, que matarían al tocar el agua.
	bSpawnKillZones = false;
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

	MarkerPlane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MarkerPlane"));
	MarkerPlane->SetupAttachment(RootComponent);
	MarkerPlane->SetMobility(EComponentMobility::Movable);
	MarkerPlane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MarkerPlane->SetCastShadow(false);
	MarkerPlane->SetGenerateOverlapEvents(false);
	MarkerPlane->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(PlaneMeshPath);
	if (PlaneMesh.Succeeded())
	{
		WaterPlane->SetStaticMesh(PlaneMesh.Object);
		MarkerPlane->SetStaticMesh(PlaneMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MarkerMat(MarkerMaterialPath);
	if (MarkerMat.Succeeded())
	{
		MarkerPlane->SetMaterial(0, MarkerMat.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SeaMaterial(SeaMaterialPath);
	if (SeaMaterial.Succeeded())
	{
		WaterPlane->SetMaterial(0, SeaMaterial.Object);
	}
	// Arena de playa en vez del material genérico del cargador (M_GridTerrain).
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SandMaterial(TNTctArenaDetail::SandMaterialPath);
	if (SandMaterial.Succeeded())
	{
		TerrainMaterial = SandMaterial.Object;
	}
}

const TCHAR* ATN_TctArena::SandMaterialPath()
{
	return TNTctArenaDetail::SandMaterialPath;
}

void ATN_TctArena::ApplySandMaterial()
{
	// Una arena colocada a mano en un nivel podría traer otro material guardado: manda la arena de playa.
	UMaterialInterface* Sand = TerrainMaterial && TerrainMaterial->GetPathName() == SandMaterialPath()
		? TerrainMaterial.Get() : LoadObject<UMaterialInterface>(nullptr, SandMaterialPath());
	if (!Sand)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[TcT] No está %s: la arena se queda con el material del cargador."), SandMaterialPath());
		return;
	}
	TerrainMaterial = Sand;
	TArray<UProceduralMeshComponent*> Meshes;
	GetComponents(Meshes);
	for (UProceduralMeshComponent* Mesh : Meshes)
	{
		if (Mesh && Mesh->GetMaterial(0) != Sand)
		{
			Mesh->SetMaterial(0, Sand);
		}
	}
}

void ATN_TctArena::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_TctArena, ArenaVariant);
	DOREPLIFETIME(ATN_TctArena, SceneryNet);
}

void ATN_TctArena::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearScenery();
	Super::EndPlay(EndPlayReason);
}

void ATN_TctArena::ClearScenery()
{
	if (IsValid(Scenery))
	{
		Scenery->Destroy();
	}
	Scenery = nullptr;
	SceneryKey = 0;
}

void ATN_TctArena::ServerSetScenery(uint32 MatchSeed, const TArray<FIntVector>& KeepOut)
{
	if (!HasAuthority())
	{
		return;
	}
	SceneryNet.Variant = ArenaVariant;
	SceneryNet.Seed = static_cast<int32>(MatchSeed);
	SceneryNet.KeepOut = KeepOut;
	SceneryNet.bReady = true;
	ForceNetUpdate();
	TryBuildScenery();
}

void ATN_TctArena::OnRep_Scenery()
{
	TryBuildScenery();
}

bool ATN_TctArena::TraceTerrainHeight(double X, double Y, float& OutZ) const
{
	if (!GroundBox.IsValid)
	{
		return false;
	}
	// Contra los trozos del terreno de la arena y nada más (no contra el mundo): una tortuga, una pieza de decorado ya montada o un
	// objeto no cambian lo medido, así que el decorado sale igual aunque se rehaga con la partida en marcha.
	const FVector Start(X, Y, GroundBox.Max.Z + 500.0);
	const FVector End(X, Y, GroundBox.Min.Z - 500.0);
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(TNTctArenaTerrain), true);
	bool bFound = false;
	float Highest = 0.f;
	TArray<UProceduralMeshComponent*> Meshes;
	GetComponents(Meshes);
	for (UProceduralMeshComponent* Mesh : Meshes)
	{
		FHitResult Hit;
		if (Mesh && Mesh->IsCollisionEnabled() && Mesh->LineTraceComponent(Hit, Start, End, Params))
		{
			Highest = bFound ? FMath::Max(Highest, static_cast<float>(Hit.ImpactPoint.Z)) : static_cast<float>(Hit.ImpactPoint.Z);
			bFound = true;
		}
	}
	OutZ = Highest;
	return bFound;
}

void ATN_TctArena::TryBuildScenery()
{
	UWorld* World = GetWorld();
	if (!World || !SceneryNet.bReady || SceneryNet.Variant != ArenaVariant || Variant != ArenaVariant || !GroundBox.IsValid)
	{
		return;
	}
	TArray<UProceduralMeshComponent*> Meshes;
	GetComponents(Meshes);
	if (Meshes.Num() == 0)
	{
		return;
	}
	uint32 Key = TNProcMap::HashCell(FCrc::StrCrc32(*ArenaVariant.ToString()), SceneryNet.Seed, SceneryNet.KeepOut.Num());
	for (const FIntVector& Zone : SceneryNet.KeepOut)
	{
		Key = TNProcMap::HashCell(Key, Zone.X ^ (Zone.Z << 16), Zone.Y);
	}
	if (Scenery && SceneryKey == Key)
	{
		return;
	}
	ClearScenery();
	double ManifestSeed = 0.0;
	if (const TSharedPtr<FJsonObject> Manifest = ReadManifest())
	{
		Manifest->TryGetNumberField(TEXT("seed"), ManifestSeed);
	}
	TArray<TNTctScenery::FKeepOut> KeepOuts;
	for (const FIntVector& Zone : SceneryNet.KeepOut)
	{
		TNTctScenery::FKeepOut& Out = KeepOuts.AddDefaulted_GetRef();
		Out.Center = FVector2D(Zone.X, Zone.Y);
		Out.Radius = static_cast<float>(Zone.Z);
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient | RF_DuplicateTransient;
	Scenery = World->SpawnActor<ATN_TctScenery>(ATN_TctScenery::StaticClass(), FTransform::Identity, Params);
	if (!Scenery)
	{
		return;
	}
	SceneryKey = Key;
	const uint32 Seed = TNTctScenery::MakeSeed(static_cast<uint32>(static_cast<int64>(ManifestSeed)), ArenaVariant, static_cast<uint32>(SceneryNet.Seed));
	if (!Scenery->Build(this, Seed, KeepOuts))
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[TcT] La arena «%s» no tiene suelo medible: sin decorado."), *ArenaVariant.ToString());
	}
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
	ApplySandMaterial();
	// La base construye la malla si hace falta y, en el servidor, pone las zonas de muerte del manifest.
	Super::BeginPlay();
	FitWaterPlane();
	SetUpToxicLook();
	TryBuildScenery();
	// Las mallas de los objetos, ya al cargar la arena (en cada máquina): sin tirones al salir el primero de cada uno.
	TNTctItems::PreloadMeshes();
}

void ATN_TctArena::SetUpToxicLook()
{
	using namespace TNTctArenaDetail;
	if (GetNetMode() == NM_DedicatedServer || !WaterPlane || MarkerMaterial)
	{
		return;
	}
	// El mar de la arena, tóxico: el mismo mar animado con otros colores (sin tocar el asset: lo comparten el resto de modos).
	if (UMaterialInterface* Sea = WaterPlane->GetMaterial(0))
	{
		if (UMaterialInstanceDynamic* Toxic = UMaterialInstanceDynamic::Create(Sea, this))
		{
			Toxic->SetVectorParameterValue(TEXT("ShallowColor"), ToxicShallow);
			Toxic->SetVectorParameterValue(TEXT("DeepColor"), ToxicDeep);
			Toxic->SetVectorParameterValue(TEXT("FoamColor"), ToxicFoam);
			WaterPlane->SetMaterial(0, Toxic);
		}
	}
	if (MarkerPlane && MarkerPlane->GetMaterial(0))
	{
		MarkerMaterial = UMaterialInstanceDynamic::Create(MarkerPlane->GetMaterial(0), this);
		if (MarkerMaterial)
		{
			MarkerMaterial->SetVectorParameterValue(TEXT("Color"), MarkerColor);
			MarkerMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.f);
			MarkerPlane->SetMaterial(0, MarkerMaterial);
		}
	}
}

void ATN_TctArena::TickMarker(const ATN_TctGameState* State)
{
	if (!MarkerPlane)
	{
		return;
	}
	// La marca se ve en los segundos de aviso y mientras sube el agua; el resto del tiempo, apagada.
	FTNTctNextRise Next;
	bool bShow = false;
	float TargetZ = 0.f;
	float Pulse = 0.f;
	if (State && State->GetNextRise(Next))
	{
		if (Next.bRising)
		{
			bShow = true;
			TargetZ = Next.RisingTargetZ;
			Pulse = 0.18f;
		}
		else if (Next.bUpcoming && Next.SecondsLeft <= TNTctPoisonDefaults::WarnSeconds)
		{
			bShow = true;
			TargetZ = Next.TargetZ;
			// Late más deprisa cuanto más cerca: un parpadeo de 1 s que va a 4 Hz al final.
			const float Hurry = 1.f - FMath::Clamp(Next.SecondsLeft / TNTctPoisonDefaults::WarnSeconds, 0.f, 1.f);
			Pulse = 0.22f + 0.2f * (0.5f + 0.5f * FMath::Sin(GetWorld()->GetTimeSeconds() * (6.f + 18.f * Hurry)));
		}
	}
	if (MarkerPlane->IsVisible() != bShow)
	{
		MarkerPlane->SetVisibility(bShow);
	}
	if (!bShow)
	{
		return;
	}
	FVector Location = MarkerPlane->GetComponentLocation();
	const FVector WaterLocation = WaterPlane->GetComponentLocation();
	Location.X = WaterLocation.X;
	Location.Y = WaterLocation.Y;
	Location.Z = TargetZ;
	MarkerPlane->SetWorldLocation(Location);
	MarkerPlane->SetWorldScale3D(WaterPlane->GetComponentScale());
	if (MarkerMaterial)
	{
		MarkerMaterial->SetScalarParameterValue(TEXT("Opacity"), Pulse);
	}
}

void ATN_TctArena::ServerSetArenaVariant(FName NewVariant)
{
	if (!HasAuthority() || NewVariant.IsNone())
	{
		return;
	}
	if (ArenaVariant != NewVariant)
	{
		// Otra variante: el decorado de la anterior se va y el servidor fija el de la nueva.
		ClearScenery();
		SceneryNet = FTNTctSceneryNet();
	}
	ArenaVariant = NewVariant;
	// La malla es transitoria: el nivel cargado en partida llega sin ella aunque la variante sea la misma. Se construye ya
	// (el BeginPlay de la base ya no la repite) para que el GameMode pueda medir la arena en StartPlay.
	TArray<UProceduralMeshComponent*> Meshes;
	GetComponents(Meshes);
	ApplySandMaterial();
	if (Variant != NewVariant || Meshes.Num() == 0)
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
	ClearScenery();
	// Antes de BeginPlay no hace falta: BeginPlay la construye con la variante ya puesta.
	if (HasActorBegunPlay())
	{
		ApplySandMaterial();
		Recargar();
		FitWaterPlane();
		TryBuildScenery();
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
	// Nunca por debajo del mar de la variante (antes de que llegue el estado del servidor, el GameState lo da muy abajo).
	const float WaterZ = FMath::Max(State ? State->GetWaterZ() : BaseWaterZ, BaseWaterZ);
	TickMarker(State);
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
	SpawnExposure.Reset();
	HighestZ = 0.f;
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
	// Cota del suelo pisable de cada muestra por su casilla (para medir lo cerca que está cada sitio del vacío).
	TMap<FIntPoint, float> Grid;
	TArray<FIntPoint> CandidateCells;
	int32 IndexX = 0;
	for (double X = GroundBox.Min.X + Step * 0.5; X < GroundBox.Max.X; X += Step, ++IndexX)
	{
		int32 IndexY = 0;
		for (double Y = GroundBox.Min.Y + Step * 0.5; Y < GroundBox.Max.Y; Y += Step, ++IndexY)
		{
			FVector Point;
			if (!TraceGround(X, Y, TopZ, BottomZ, Point) || Point.Z < MinGroundZ)
			{
				continue;
			}
			SurveyHeights.Add(static_cast<float>(Point.Z));
			Grid.Add(FIntPoint(IndexX, IndexY), static_cast<float>(Point.Z));
			HighestZ = FMath::Max(HighestZ, static_cast<float>(Point.Z));

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
				CandidateCells.Add(FIntPoint(IndexX, IndexY));
			}
		}
	}
	// Exposición: el primer anillo de muestras (1 = pegado) donde falta suelo o baja más de 1,5 m; sin ninguno, 0.
	for (int32 Candidate = 0; Candidate < SpawnCandidates.Num(); ++Candidate)
	{
		const FIntPoint Cell = CandidateCells[Candidate];
		const float Z = static_cast<float>(SpawnCandidates[Candidate].Z);
		float Exposure = 0.f;
		for (int32 Ring = 1; Ring <= ExposureRings && Exposure == 0.f; ++Ring)
		{
			for (int32 Dx = -Ring; Dx <= Ring && Exposure == 0.f; ++Dx)
			{
				for (int32 Dy = -Ring; Dy <= Ring; ++Dy)
				{
					if (FMath::Max(FMath::Abs(Dx), FMath::Abs(Dy)) != Ring)
					{
						continue;
					}
					const float* Neighbour = Grid.Find(FIntPoint(Cell.X + Dx, Cell.Y + Dy));
					if (!Neighbour || *Neighbour < Z - ExposureDrop)
					{
						Exposure = 1.f - static_cast<float>(Ring - 1) / static_cast<float>(ExposureRings);
						break;
					}
				}
			}
		}
		SpawnExposure.Add(Exposure);
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
