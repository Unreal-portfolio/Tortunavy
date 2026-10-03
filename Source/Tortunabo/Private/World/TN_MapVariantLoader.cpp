#include "World/TN_MapVariantLoader.h"
#include "Core/TN_Log.h"
#include "World/TN_DeathZoneVolume.h"
#include "World/TN_TerrainMeshDecisions.h"

#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Mismo material de terreno que usan las herramientas de importacion (Scripts/import_terrain_mesh.py).
	const TCHAR* DefaultTerrainMaterialPath = TEXT("/Game/Blueprints/Gameplay/GridMap/M_GridTerrain.M_GridTerrain");
}

ATN_MapVariantLoader::ATN_MapVariantLoader()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	// Los ProceduralMeshComponent de los trozos se crean Static (como ATN_TerrainMeshTile): la
	// raiz tiene que serlo tambien, si no Unreal rechaza el AttachTo (no deja colgar un
	// componente Static de un padre Movable) y el trozo queda sin seguir al actor.
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;

	ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(DefaultTerrainMaterialPath);
	if (Material.Succeeded())
	{
		TerrainMaterial = Material.Object;
	}
}

void ATN_MapVariantLoader::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (BuiltVariant != Variant)
	{
		LoadVariant();
	}
}

void ATN_MapVariantLoader::BeginPlay()
{
	Super::BeginPlay();
	// PIE/standalone duplican el actor del nivel con sus componentes ya construidos en el
	// editor; si por lo que sea no hay ninguno (actor colocado por codigo, p. ej.), se construye
	// ahora para que la variante tambien funcione lanzando la partida directamente.
	if (ChunkMeshes.Num() == 0)
	{
		LoadVariant();
	}
	if (HasAuthority())
	{
		SpawnKillZones();
	}
}

void ATN_MapVariantLoader::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (const TWeakObjectPtr<ATN_DeathZoneVolume>& Zone : SpawnedKillZones)
	{
		if (Zone.IsValid())
		{
			Zone->Destroy();
		}
	}
	SpawnedKillZones.Reset();
	Super::EndPlay(EndPlayReason);
}

TSharedPtr<FJsonObject> ATN_MapVariantLoader::ReadManifest() const
{
	if (Variant.IsNone())
	{
		return nullptr;
	}
	const FString ManifestPath = VariantsDir() / Variant.ToString() / TEXT("manifest.json");
	FString ManifestText;
	if (!FFileHelper::LoadFileToString(ManifestText, *ManifestPath))
	{
		UE_LOG(LogTortunabo, Error, TEXT("[MapVariantLoader] '%s': no se puede leer '%s'."), *GetName(), *ManifestPath);
		return nullptr;
	}
	TSharedPtr<FJsonObject> Manifest;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ManifestText);
	if (!FJsonSerializer::Deserialize(Reader, Manifest) || !Manifest.IsValid())
	{
		UE_LOG(LogTortunabo, Error, TEXT("[MapVariantLoader] '%s': '%s' no es JSON valido."), *GetName(), *ManifestPath);
		return nullptr;
	}
	return Manifest;
}

void ATN_MapVariantLoader::SpawnKillZones()
{
	const TSharedPtr<FJsonObject> Manifest = ReadManifest();
	const TArray<TSharedPtr<FJsonValue>>* Boxes = nullptr;
	if (!Manifest.IsValid() || !Manifest->TryGetArrayField(TEXT("kill_boxes_uu"), Boxes) || !Boxes)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// Caer al fondo de un barranco es morir: medio segundo dentro basta (el agua no se nada).
	constexpr float SecondsToDie = 0.5f;
	for (const TSharedPtr<FJsonValue>& Value : *Boxes)
	{
		const TSharedPtr<FJsonObject>* Box = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Center = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Extent = nullptr;
		double Yaw = 0.0;
		if (!Value->TryGetObject(Box) || !Box || !(*Box)->TryGetArrayField(TEXT("center"), Center)
			|| !(*Box)->TryGetArrayField(TEXT("extent"), Extent) || !Center || !Extent
			|| Center->Num() < 3 || Extent->Num() < 3 || !(*Box)->TryGetNumberField(TEXT("yaw"), Yaw))
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[MapVariantLoader] '%s': caja de muerte mal formada en '%s'."),
				*GetName(), *Variant.ToString());
			continue;
		}
		const FVector Location((*Center)[0]->AsNumber(), (*Center)[1]->AsNumber(), (*Center)[2]->AsNumber());
		const FVector HalfExtent((*Extent)[0]->AsNumber(), (*Extent)[1]->AsNumber(), (*Extent)[2]->AsNumber());
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ATN_DeathZoneVolume* Zone = World->SpawnActor<ATN_DeathZoneVolume>(
			ATN_DeathZoneVolume::StaticClass(), GetActorTransform().TransformPosition(Location),
			FRotator(0.0, Yaw, 0.0) + GetActorRotation(), Params);
		if (!Zone)
		{
			continue;
		}
		Zone->ConfigureZone(HalfExtent, SecondsToDie);
		SpawnedKillZones.Add(Zone);
	}
	UE_LOG(LogTortunabo, Log, TEXT("[MapVariantLoader] '%s': %d zonas de muerte del barranco."),
		*GetName(), SpawnedKillZones.Num());
}

void ATN_MapVariantLoader::Recargar()
{
	LoadVariant();
}

FString ATN_MapVariantLoader::VariantsDir()
{
	return FPaths::ProjectDir() / TEXT("Scripts/terrain_volumes/Variants");
}

void ATN_MapVariantLoader::ClearMeshes()
{
	// Todos los ProceduralMesh del actor, no solo los de ChunkMeshes: los niveles guardados antes de
	// que fueran transitorios traen los trozos viejos serializados y sin referencia en el array.
	TArray<UProceduralMeshComponent*> Meshes;
	GetComponents(Meshes);
	for (UProceduralMeshComponent* Mesh : Meshes)
	{
		if (Mesh) { Mesh->DestroyComponent(); }
	}
	ChunkMeshes.Reset();
	VariantDescription.Reset();
	BuiltVariant = NAME_None;
}

TArray<FString> ATN_MapVariantLoader::GetVariantNames() const
{
	TArray<FString> Names;
	const FString Dir = VariantsDir();

	FString IndexText;
	if (FFileHelper::LoadFileToString(IndexText, *(Dir / TEXT("index.json"))))
	{
		TSharedPtr<FJsonValue> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(IndexText);
		if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
			if (Root->TryGetArray(Entries) && Entries)
			{
				for (const TSharedPtr<FJsonValue>& Entry : *Entries)
				{
					const TSharedPtr<FJsonObject>* Object = nullptr;
					FString Name;
					if (Entry.IsValid() && Entry->TryGetObject(Object) && Object && Object->IsValid()
						&& (*Object)->TryGetStringField(TEXT("name"), Name))
					{
						Names.Add(Name);
					}
				}
			}
		}
	}

	if (Names.Num() == 0)
	{
		// Sin index.json (todavia no lo ha escrito el generador, o se borro): cualquier carpeta
		// con manifest.json bajo Variants/ cuenta como variante.
		FPlatformFileManager::Get().GetPlatformFile().IterateDirectory(*Dir,
			[&Names](const TCHAR* FilenameOrDirectory, bool bIsDirectory) -> bool
			{
				if (bIsDirectory)
				{
					const FString ManifestPath = FString(FilenameOrDirectory) / TEXT("manifest.json");
					if (FPaths::FileExists(ManifestPath))
					{
						Names.Add(FPaths::GetCleanFilename(FilenameOrDirectory));
					}
				}
				return true;
			});
		Names.Sort();
	}
	return Names;
}

void ATN_MapVariantLoader::MoveStartPlayerStart(const TSharedPtr<FJsonObject>& Manifest) const
{
	const TArray<TSharedPtr<FJsonValue>>* StartUu = nullptr;
	if (!Manifest->TryGetArrayField(TEXT("start_uu"), StartUu) || !StartUu || StartUu->Num() < 3)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[MapVariantLoader] '%s': el manifest de '%s' no trae 'start_uu': el PlayerStart no se mueve."),
			*GetName(), *Variant.ToString());
		return;
	}
	// start_uu es la cota del suelo: el PlayerStart se sube media capsula y un margen, si no la
	// capsula del pawn nace metida en el terreno y el spawn falla (la tortuga no aparecia).
	constexpr double CapsuleLift = 120.0;
	const FVector Start((*StartUu)[0]->AsNumber(), (*StartUu)[1]->AsNumber(), (*StartUu)[2]->AsNumber() + CapsuleLift);

	UWorld* World = GetWorld();
	if (!World) { return; }
	APlayerStart* First = nullptr;
	APlayerStart* Tagged = nullptr;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		if (!First) { First = *It; }
		if (It->ActorHasTag(TEXT("MapVariantStart")))
		{
			Tagged = *It;
			break;
		}
	}
	if (APlayerStart* Target = Tagged ? Tagged : First)
	{
		// La cápsula del PlayerStart es Static: en partida (BeginPlay) no se deja mover sin cambiarla antes a Movable.
		if (USceneComponent* StartRoot = Target->GetRootComponent())
		{
			StartRoot->SetMobility(EComponentMobility::Movable);
		}
		Target->SetActorLocation(Start);
	}
	else
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[MapVariantLoader] '%s': no hay ningun APlayerStart en el nivel que mover al inicio de '%s'."),
			*GetName(), *Variant.ToString());
	}
}

void ATN_MapVariantLoader::LoadVariant()
{
	const double StartSeconds = FPlatformTime::Seconds();
	ClearMeshes();
	if (Variant.IsNone())
	{
		return;
	}

	const FString VolumeDir = VariantsDir() / Variant.ToString();
	const FString ManifestPath = VolumeDir / TEXT("manifest.json");
	FString ManifestText;
	if (!FFileHelper::LoadFileToString(ManifestText, *ManifestPath))
	{
		UE_LOG(LogTortunabo, Error, TEXT("[MapVariantLoader] '%s': no se puede leer '%s'."), *GetName(), *ManifestPath);
		return;
	}

	TSharedPtr<FJsonObject> Manifest;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ManifestText);
	if (!FJsonSerializer::Deserialize(Reader, Manifest) || !Manifest.IsValid())
	{
		UE_LOG(LogTortunabo, Error, TEXT("[MapVariantLoader] '%s': '%s' no es JSON valido."), *GetName(), *ManifestPath);
		return;
	}

	Manifest->TryGetStringField(TEXT("description"), VariantDescription);

	const TArray<TSharedPtr<FJsonValue>>* Cells = nullptr;
	if (!Manifest->TryGetArrayField(TEXT("cells"), Cells) || !Cells)
	{
		UE_LOG(LogTortunabo, Error, TEXT("[MapVariantLoader] '%s': '%s' sin 'cells'."), *GetName(), *ManifestPath);
		return;
	}

	int32 ChunkIndex = 0;
	int32 Triangles = 0;
	int32 VisualOnly = 0;
	for (const TSharedPtr<FJsonValue>& CellValue : *Cells)
	{
		const TSharedPtr<FJsonObject> Cell = CellValue.IsValid() ? CellValue->AsObject() : nullptr;
		FString File;
		if (!Cell.IsValid() || !Cell->TryGetStringField(TEXT("file"), File))
		{
			continue;
		}
		const FString ChunkPath = VolumeDir / File;
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *ChunkPath))
		{
			UE_LOG(LogTortunabo, Error, TEXT("[MapVariantLoader] '%s': no se puede leer '%s'."), *GetName(), *ChunkPath);
			continue;
		}
		TNTerrainMesh::FChunk Chunk;
		FString Error;
		if (!TNTerrainMesh::ParseChunk(Bytes, Chunk, Error))
		{
			UE_LOG(LogTortunabo, Error, TEXT("[MapVariantLoader] '%s': '%s' no es valido: %s."), *GetName(), *ChunkPath, *Error);
			continue;
		}
		const TNGridTerrain::FTileMesh Mesh = TNTerrainMesh::ToTileMesh(Chunk.Vertices, Chunk.Normals, Chunk.Colors, Chunk.Triangles);
		if (Mesh.Vertices.Num() == 0)
		{
			UE_LOG(LogTortunabo, Error, TEXT("[MapVariantLoader] '%s': '%s' con arrays que no casan, se descarta."), *GetName(), *ChunkPath);
			continue;
		}

		double CenterX = 0.0, CenterY = 0.0;
		const TArray<TSharedPtr<FJsonValue>>* Center = nullptr;
		if (Cell->TryGetArrayField(TEXT("center_uu"), Center) && Center && Center->Num() >= 2)
		{
			CenterX = (*Center)[0]->AsNumber();
			CenterY = (*Center)[1]->AsNumber();
		}

		// "collision": false = terreno de fondo (la corona barata de alrededor del mapa): solo se ve.
		bool bCollision = true;
		Cell->TryGetBoolField(TEXT("collision"), bCollision);

		UProceduralMeshComponent* Component = NewObject<UProceduralMeshComponent>(this,
			MakeUniqueObjectName(this, UProceduralMeshComponent::StaticClass(),
				*FString::Printf(TEXT("Chunk_%d"), ChunkIndex++)), RF_Transient);
		// Cocinado sincrono: la colision de la variante esta lista nada mas cargarla.
		Component->bUseAsyncCooking = false;
		Component->SetCollisionProfileName(bCollision ? TEXT("BlockAll") : TEXT("NoCollision"));
		Component->SetMobility(EComponentMobility::Static);
		Component->SetupAttachment(RootComponent);
		Component->SetRelativeLocation(FVector(CenterX, CenterY, 0.0));
		Component->RegisterComponent();
		Component->CreateMeshSection_LinearColor(0, Mesh.Vertices, Mesh.Triangles, Mesh.Normals, TArray<FVector2D>(),
			Mesh.Colors, TArray<FProcMeshTangent>(), bCollision);
		if (TerrainMaterial) { Component->SetMaterial(0, TerrainMaterial); }
		ChunkMeshes.Add(Component);
		Triangles += Mesh.Triangles.Num() / 3;
		VisualOnly += bCollision ? 0 : 1;
	}

	MoveStartPlayerStart(Manifest);
	BuiltVariant = Variant;
	UE_LOG(LogTortunabo, Log, TEXT("[MapVariantLoader] '%s': variante '%s' cargada, %d trozos (%d sin colision), %d triangulos, %.0f ms."),
		*GetName(), *Variant.ToString(), ChunkMeshes.Num(), VisualOnly, Triangles,
		(FPlatformTime::Seconds() - StartSeconds) * 1000.0);
}
