#include "Testing/TN_ArtShotSubsystem.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/MeshComponent.h"
#include "Core/TN_Log.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/TortugaCharacter.h"
#include "Testing/TN_TestReport.h"
#include "UnrealClient.h"
#include "World/TN_EnemySeagull.h"
#include "World/TN_PlaceholderArt.h"

namespace TNArtShot
{
	/** Segundos por defecto antes de la primera captura (carga de texturas y BeginPlay de todo el mapa). */
	constexpr float DefaultWarmup = 4.f;
	/** Del encuadre a la captura (el arte de código se monta en BeginPlay y las texturas suben de mip). */
	constexpr float SettleSeconds = 1.5f;
	/** De la captura al siguiente objeto (la petición se atiende en el siguiente fotograma dibujado). */
	constexpr float AfterShotSeconds = 0.6f;
	/** Distancia delante del jugador a la que nace lo que no está en el mapa (cm). */
	constexpr float SpawnAhead = 1200.f;
	/** La cámara se aleja este múltiplo del radio del objeto, girada y por encima de su frente. */
	constexpr float DistanceRadii = 2.6f;
	constexpr float ViewYawDegrees = 30.f;
	constexpr float ViewPitchDegrees = -18.f;
	constexpr float MinRadius = 60.f;
	constexpr float TraceHalfHeight = 5000.f;

	/** Caja de las mallas que se ven (los marcadores ocultos y los avisos no cuentan). */
	FBox VisibleBounds(const AActor* Actor)
	{
		FBox Box(ForceInit);
		TInlineComponentArray<UMeshComponent*> Meshes(Actor);
		for (const UMeshComponent* Mesh : Meshes)
		{
			if (Mesh->IsRegistered() && Mesh->IsVisible() && !Mesh->bHiddenInGame)
			{
				Box += Mesh->Bounds.GetBox();
			}
		}
		return Box.IsValid ? Box : FBox(Actor->GetActorLocation() - FVector(MinRadius), Actor->GetActorLocation() + FVector(MinRadius));
	}

	FString ShortName(const FString& Path)
	{
		FString Name = FPaths::GetExtension(Path);
		return Name.IsEmpty() ? FPaths::GetBaseFilename(Path) : Name;
	}
}

bool UTN_ArtShotSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && !TNTestReport::CommandLineValue(TEXT("-TNArtShots")).IsEmpty() && Super::ShouldCreateSubsystem(Outer);
#endif
}

void UTN_ArtShotSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	TNTestReport::CommandLineValue(TEXT("-TNArtShots")).ParseIntoArray(ClassPaths, TEXT(";"), true);
	OutDir = TNTestReport::CommandLineValue(TEXT("-TNArtShotsOut"));
	if (OutDir.IsEmpty())
	{
		OutDir = FPaths::ProjectSavedDir() / TEXT("ArtShots");
	}
	IFileManager::Get().MakeDirectory(*OutDir, true);
	const FString Warmup = TNTestReport::CommandLineValue(TEXT("-TNArtShotsWarmup"));
	Countdown = Warmup.IsEmpty() ? TNArtShot::DefaultWarmup : FCString::Atof(*Warmup);
	bQuitWhenDone = FParse::Param(FCommandLine::Get(), TEXT("TNQuitWhenDone"));
	// X:Y y no X,Y: FParse::Value corta en las comas.
	TArray<FString> At;
	TNTestReport::CommandLineValue(TEXT("-TNArtShotsAt")).ParseIntoArray(At, TEXT(":"), true);
	if (At.Num() >= 2)
	{
		SpawnAt = FVector2D(FCString::Atod(*At[0]), FCString::Atod(*At[1]));
	}
	bActive = ClassPaths.Num() > 0;
	UE_LOG(LogTortunabo, Display, TEXT("[ArtShot] %d clases, capturas en %s"), ClassPaths.Num(), *OutDir);
}

void UTN_ArtShotSubsystem::Tick(float DeltaTime)
{
	Countdown -= DeltaTime;
	if (Countdown > 0.f)
	{
		return;
	}
	if (bFramed)
	{
		TakeShot(Current);
		bFramed = false;
		Countdown = TNArtShot::AfterShotSeconds;
		return;
	}
	++Current;
	if (!ClassPaths.IsValidIndex(Current))
	{
		Finish();
		return;
	}
	bFramed = FrameShot(Current);
	Countdown = bFramed ? TNArtShot::SettleSeconds : 0.f;
}

AActor* UTN_ArtShotSubsystem::FindOrSpawn(UClass* Class)
{
	UWorld* World = GetWorld();
	AActor* Placed = nullptr;
	for (TActorIterator<AActor> It(World, Class); It; ++It)
	{
		const bool bCloser = SpawnAt.IsSet() && Placed
			&& FVector2D::DistSquared(FVector2D(It->GetActorLocation()), SpawnAt.GetValue()) < FVector2D::DistSquared(FVector2D(Placed->GetActorLocation()), SpawnAt.GetValue());
		Placed = (!Placed || bCloser) ? *It : Placed;
	}
	if (Placed)
	{
		return Placed;
	}
	APlayerController* PC = World->GetFirstPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	FVector Origin = Pawn ? Pawn->GetActorLocation() + Pawn->GetActorForwardVector() * TNArtShot::SpawnAhead : FVector::ZeroVector;
	if (SpawnAt.IsSet())
	{
		Origin = FVector(SpawnAt->X, SpawnAt->Y, Origin.Z);
	}
	FVector Ground = Origin;
	FHitResult Hit;
	const FVector Up(0.0, 0.0, TNArtShot::TraceHalfHeight);
	if (World->LineTraceSingleByChannel(Hit, Origin + Up, Origin - Up, ECC_WorldStatic))
	{
		Ground = Hit.ImpactPoint;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FRotator Facing = Pawn ? FRotator(0.f, Pawn->GetActorRotation().Yaw + 180.f, 0.f) : FRotator::ZeroRotator;
	AActor* Spawned = World->SpawnActor<AActor>(Class, FTransform(Facing, Ground), Params);
	// La gaviota sin objetivo pica al momento y se va: se le da el jugador para que se quede encima.
	if (ATN_EnemySeagull* Gull = Cast<ATN_EnemySeagull>(Spawned))
	{
		Gull->InitializeWithTarget(Cast<ATortugaCharacter>(Pawn));
	}
	return Spawned;
}

bool UTN_ArtShotSubsystem::FrameShot(int32 Index)
{
	UClass* Class = LoadClass<AActor>(nullptr, *ClassPaths[Index]);
	Subject = Class ? FindOrSpawn(Class) : nullptr;
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!Subject || !PC)
	{
		UE_LOG(LogTortunabo, Error, TEXT("[ArtShot] %s: no se ha podido cargar ni crear"), *ClassPaths[Index]);
		return false;
	}
	if (!Camera)
	{
		Camera = GetWorld()->SpawnActor<ACameraActor>();
	}
	const FBox Box = TNArtShot::VisibleBounds(Subject);
	const float Radius = FMath::Max(static_cast<float>(Box.GetExtent().Size()), TNArtShot::MinRadius);
	// De frente y algo girada; si algo del mapa tapa ese lado, prueba otros y, si no, se acerca hasta lo que tapa.
	const float YawOffsets[] = { TNArtShot::ViewYawDegrees, -TNArtShot::ViewYawDegrees, 0.f, 75.f, -75.f, 120.f, -120.f, 180.f };
	FCollisionQueryParams Query(SCENE_QUERY_STAT(TN_ArtShotView), false, Subject);
	FRotator BestView = FRotator::ZeroRotator;
	FVector BestEye = Box.GetCenter();
	double BestClear = -1.0;
	for (const float Offset : YawOffsets)
	{
		const FRotator View(TNArtShot::ViewPitchDegrees, Subject->GetActorRotation().Yaw + 180.f + Offset, 0.f);
		const FVector Eye = Box.GetCenter() - View.Vector() * Radius * TNArtShot::DistanceRadii;
		FHitResult Hit;
		const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, Box.GetCenter(), Eye, ECC_Visibility, Query);
		const double Clear = bBlocked ? Hit.Distance : FVector::Dist(Box.GetCenter(), Eye);
		if (Clear > BestClear)
		{
			BestClear = Clear;
			BestView = View;
			BestEye = bBlocked ? Hit.Location + View.Vector() * TNArtShot::MinRadius : Eye;
		}
		if (!bBlocked)
		{
			break;
		}
	}
	Camera->SetActorLocationAndRotation(BestEye, BestView);
	PC->SetViewTarget(Camera);
	return true;
}

void UTN_ArtShotSubsystem::TakeShot(int32 Index)
{
	if (!IsValid(Subject))
	{
		UE_LOG(LogTortunabo, Error, TEXT("[ArtShot] %s: ha desaparecido antes de la captura"), *ClassPaths[Index]);
		return;
	}
	const FString Name = TNArtShot::ShortName(ClassPaths[Index]);
	const FString File = OutDir / (Name + TEXT(".png"));
	FScreenshotRequest::RequestScreenshot(File, false, false);
	const FBox Box = TNArtShot::VisibleBounds(Subject);
	UE_LOG(LogTortunabo, Display, TEXT("[ArtShot] %s: marcadores visibles=%d, medidas=%s cm -> %s"), *Name,
		TNPlaceholderArt::CountVisiblePlaceholders(Subject), *Box.GetSize().ToCompactString(), *File);
}

void UTN_ArtShotSubsystem::Finish()
{
	bActive = false;
	UE_LOG(LogTortunabo, Display, TEXT("[ArtShot] Terminado: %d capturas"), ClassPaths.Num());
	if (bQuitWhenDone)
	{
		FPlatformMisc::RequestExit(false, TEXT("TNArtShots"));
	}
}
