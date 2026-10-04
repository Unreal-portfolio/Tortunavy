// Fotos de prueba de las piezas de arte del lobby (TN.Art.Shots), fuera de Shipping: comprueba sin abrir el editor que los
// sustitutos de DA_Arte_Lobby se ven en su sitio (Docs/Arte_Assets.md, §6). Coloca una cámara en unos cuantos puntos fijos
// alrededor del castillo de arena, saca capturas sin interfaz, lista las piezas (TN.Art.Slots Lobby) y cierra el juego.
//   UnrealEditor-Win64-DebugGame.exe <uproject> <LVL_Lobby> -game -RenderOffScreen -ResX=1600 -ResY=900 -NoSteam
//     -ExecCmds="TN.Art.Shots C:/ruta"
// También en el PIE, desde la consola del juego (el editor se cierra al acabar).

#if !UE_BUILD_SHIPPING

#include "Art/TN_Art.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Containers/Ticker.h"
#include "Core/TN_Log.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Lobby/TN_SandCastleLobby.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace TNArtShotsDetail
{
	struct FShot
	{
		/** Cámara y punto al que mira, en locales del castillo (centro en el origen, +Y hacia la puerta doble). */
		FVector Camera;
		FVector Focus;
		float Fov = 60.f;
		const TCHAR* Name;
	};

	TArray<FShot> Plan()
	{
		return {
			{ FVector(0.0, 5600.0, 1500.0), FVector(0.0, 2300.0, 500.0), 60.f, TEXT("entrada") },
			{ FVector(0.0, 1950.0, 420.0), FVector(0.0, 700.0, 150.0), 70.f, TEXT("plaza_huevos") },
			{ FVector(0.0, 9000.0, 6500.0), FVector(0.0, 0.0, 0.0), 55.f, TEXT("castillo_desde_arriba") },
			{ FVector(1200.0, 300.0, 900.0), FVector(-1500.0, 1500.0, 100.0), 75.f, TEXT("plaza_puestos") },
			{ FVector(0.0, -3900.0, 1900.0), FVector(0.0, -1500.0, 0.0), 70.f, TEXT("patio_pruebas") },
			{ FVector(2700.0, 2700.0, 2200.0), FVector(9000.0, 9000.0, 0.0), 70.f, TEXT("valle") },
			{ FVector(-2700.0, -2700.0, 2200.0), FVector(-9000.0, -9000.0, 0.0), 70.f, TEXT("valle_2") },
		};
	}

	struct FState
	{
		FString Dir;
		int32 Step = -1;
		int32 Wait = 0;
		TWeakObjectPtr<ACameraActor> Camera;
		FTransform Origin = FTransform::Identity;
	};

	UWorld* GameWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && Context.World()) { return Context.World(); }
		}
		return nullptr;
	}
}

static FAutoConsoleCommand GTNArtShotsCommand(
	TEXT("TN.Art.Shots"),
	TEXT("Pruebas: TN.Art.Shots [carpeta] [espera = 10]: fotos sin interfaz del lobby desde puntos fijos alrededor del castillo, para ver los sustitutos de arte; lista las piezas (TN.Art.Slots Lobby) y cierra el juego."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		using namespace TNArtShotsDetail;
		TSharedRef<FState> State = MakeShared<FState>();
		State->Dir = Args.IsValidIndex(0) ? Args[0] : FPaths::ProjectSavedDir() / TEXT("ArtShots");
		const float Wait = Args.IsValidIndex(1) ? FCString::Atof(*Args[1]) : 10.f;
		State->Wait = FMath::CeilToInt(Wait / 0.25f);
		IFileManager::Get().MakeDirectory(*State->Dir, true);
		const TArray<FShot> Shots = Plan();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State, Shots](float) -> bool
		{
			UWorld* World = GameWorld();
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			if (!PC) { return true; }
			if (State->Wait > 0) { --State->Wait; return true; }
			if (State->Step < 0)
			{
				for (TActorIterator<ATN_SandCastleLobby> It(World); It; ++It)
				{
					State->Origin = FTransform(FRotator(0.f, It->GetActorRotation().Yaw, 0.f), It->GetActorLocation());
					break;
				}
				GEngine->Exec(World, TEXT("TN.Art.Slots Lobby"));
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				State->Camera = World->SpawnActor<ACameraActor>(State->Origin.GetLocation(), FRotator::ZeroRotator, Params);
				if (State->Camera.IsValid()) { State->Camera->GetCameraComponent()->bConstrainAspectRatio = false; }
				State->Step = 0;
			}
			ACameraActor* Camera = State->Camera.Get();
			const int32 ShotIndex = State->Step / 2;
			if (!Camera || ShotIndex >= Shots.Num())
			{
				UE_LOG(LogTortunabo, Display, TEXT("[Arte] TN.Art.Shots: fotos en %s"), *State->Dir);
				FPlatformMisc::RequestExit(false, TEXT("TN.Art.Shots"));
				return false;
			}
			const FShot& Shot = Shots[ShotIndex];
			if (State->Step % 2 == 0)
			{
				// Coloca la cámara y deja unos fotogramas para que se asienten la luz, el antialias y la carga de mallas.
				const FVector From = State->Origin.TransformPosition(Shot.Camera);
				const FVector To = State->Origin.TransformPosition(Shot.Focus);
				Camera->SetActorLocationAndRotation(From, (To - From).Rotation());
				Camera->GetCameraComponent()->SetFieldOfView(Shot.Fov);
				PC->SetViewTarget(Camera);
				State->Wait = 8;
			}
			else
			{
				const FString File = State->Dir / (FString(Shot.Name) + TEXT(".png"));
				FScreenshotRequest::RequestScreenshot(File, false, false);
				UE_LOG(LogTortunabo, Display, TEXT("[Arte] TN.Art.Shots: %s"), *File);
				State->Wait = 4;
			}
			++State->Step;
			return true;
		}), 0.25f);
	}));

#endif
