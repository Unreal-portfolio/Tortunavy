// Fotos de prueba de los buggies en el mundo (TN.Buggy.WorldShots), fuera de Shipping: con la luz del mapa, de cerca y de
// lejos, para revisar la carrocería de tortuga sin abrir el editor. Congela el tiempo (slomo) para que la parrilla no
// arranque y saca capturas sin interfaz desde una cámara colocada alrededor del buggy de la jugadora.
//   UnrealEditor-Win64-DebugGame.exe <uproject> <LVL_Rally con ?Variant=I03R_tortuga_magna?Bots=5> -game
//     -RenderOffScreen -ResX=1600 -ResY=900 -NoSteam -ExecCmds="TN.Buggy.WorldShots C:/ruta"

#if !UE_BUILD_SHIPPING

#include "Vehicles/TN_Buggy.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogTNBuggyWorldShots, Log, All);

namespace TNBuggyWorldShotsDetail
{
	struct FShot
	{
		/** Cámara relativa al buggy (cm, ejes del buggy) y punto al que mira (relativo también). */
		FVector Camera;
		FVector Focus;
		float Fov = 50.f;
		FString Name;
	};

	TArray<FShot> Plan()
	{
		return {
			{ FVector(520.0, -420.0, 210.0), FVector(0.0, 0.0, 90.0), 45.f, TEXT("mundo_cerca_frente") },
			{ FVector(0.0, -720.0, 160.0), FVector(0.0, 0.0, 90.0), 45.f, TEXT("mundo_cerca_lado") },
			{ FVector(-560.0, 380.0, 330.0), FVector(0.0, 0.0, 90.0), 45.f, TEXT("mundo_cerca_detras") },
			{ FVector(2200.0, -1500.0, 1100.0), FVector(-600.0, 0.0, 0.0), 40.f, TEXT("mundo_lejos_parrilla") },
			{ FVector(-5200.0, 2600.0, 2600.0), FVector(0.0, 0.0, 0.0), 30.f, TEXT("mundo_muy_lejos") },
		};
	}

	struct FState
	{
		FString Dir;
		int32 Step = -1;
		int32 Wait = 0;
		TWeakObjectPtr<ACameraActor> Camera;
		TWeakObjectPtr<ATN_Buggy> Target;
	};
}

static FAutoConsoleCommandWithWorldAndArgs GTNBuggyWorldShotsCommand(
	TEXT("TN.Buggy.WorldShots"),
	TEXT("Pruebas: TN.Buggy.WorldShots [carpeta] [espera = 8]: congela el tiempo, saca fotos sin interfaz del buggy de la jugadora (de cerca, de lejos y la parrilla) y cierra el juego."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		using namespace TNBuggyWorldShotsDetail;
		TSharedRef<FState> State = MakeShared<FState>();
		State->Dir = Args.IsValidIndex(0) ? Args[0] : FPaths::ProjectSavedDir() / TEXT("BuggyWorldShots");
		const float Wait = Args.IsValidIndex(1) ? FCString::Atof(*Args[1]) : 8.f;
		State->Wait = FMath::CeilToInt(Wait / 0.25f);
		IFileManager::Get().MakeDirectory(*State->Dir, true);
		const TArray<FShot> Shots = Plan();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State, Shots](float) -> bool
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && Context.World()) { World = Context.World(); break; }
			}
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			if (!PC) { return true; }
			if (State->Wait > 0) { --State->Wait; return true; }
			if (State->Step < 0)
			{
				ATN_Buggy* Target = Cast<ATN_Buggy>(PC->GetPawn());
				for (TActorIterator<ATN_Buggy> It(World); It && !Target; ++It) { Target = *It; }
				if (!Target) { return true; }
				State->Target = Target;
				// Tiempo casi parado: la parrilla no arranca mientras se hacen las fotos.
				GEngine->Exec(World, TEXT("slomo 0.01"));
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				State->Camera = World->SpawnActor<ACameraActor>(Target->GetActorLocation(), FRotator::ZeroRotator, Params);
				if (State->Camera.IsValid()) { State->Camera->GetCameraComponent()->bConstrainAspectRatio = false; }
				State->Step = 0;
				State->Wait = 0;
			}
			ATN_Buggy* Target = State->Target.Get();
			ACameraActor* Camera = State->Camera.Get();
			const int32 ShotIndex = State->Step / 2;
			if (!Target || !Camera || ShotIndex >= Shots.Num())
			{
				UE_LOG(LogTNBuggyWorldShots, Display, TEXT("[BuggyWorldShots] listo en %s"), *State->Dir);
				FPlatformMisc::RequestExit(false, TEXT("TN.Buggy.WorldShots"));
				return false;
			}
			const FShot& Shot = Shots[ShotIndex];
			const FTransform Xf(FRotator(0.f, Target->GetActorRotation().Yaw, 0.f), Target->GetActorLocation());
			if (State->Step % 2 == 0)
			{
				// Coloca la cámara y deja unos fotogramas para que se asienten la luz y el antialias.
				const FVector From = Xf.TransformPosition(Shot.Camera);
				const FVector To = Xf.TransformPosition(Shot.Focus);
				Camera->SetActorLocationAndRotation(From, (To - From).Rotation());
				Camera->GetCameraComponent()->SetFieldOfView(Shot.Fov);
				PC->SetViewTarget(Camera);
				State->Wait = 6;
			}
			else
			{
				const FString File = State->Dir / (Shot.Name + TEXT(".png"));
				FScreenshotRequest::RequestScreenshot(File, false, false);
				UE_LOG(LogTNBuggyWorldShots, Display, TEXT("[BuggyWorldShots] %s"), *File);
				State->Wait = 4;
			}
			++State->Step;
			return true;
		}), 0.25f);
	}));

#endif
