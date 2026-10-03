// Pruebas de la cabeza que sigue a la cámara en tercera persona (#623, UTN_TurtleAnimInstance), fuera de Shipping:
//  - TN.HeadLook.Shots [carpeta] [espera = 8]: fotos sin interfaz de la tortuga del jugador desde una cámara fija, con la
//    vista del mando al frente, 45° a la derecha, 60° a la izquierda, en el tope, mirando arriba, abajo y detrás. Deja en el
//    registro el giro de la cabeza medido en el hueso y cierra el juego.
//      UnrealEditor-Win64-DebugGame.exe <uproject> /Game/Maps/Lobby/LVL_Lobby -game -RenderOffScreen -ResX=1600 -ResY=900
//        -NoSteam -UseFixedTimeStep -FPS=30 -ExecCmds="TN.HeadLook.Shots C:/ruta"
//  - TN.HeadLook.Sweep [segundos = 20]: mueve sola la vista del jugador local (guiñada ±60° y cabeceo de -25 a 25°), para
//    ver con TN.HeadLook.Log 1 en otra máquina el giro que le llega.

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/TN_Log.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Player/TN_TurtleAnimInstance.h"
#include "Player/TortugaCharacter.h"
#include "UnrealClient.h"

namespace TNHeadLookDebug
{
	UWorld* GameWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && Context.World()) { return Context.World(); }
		}
		return nullptr;
	}

	APlayerController* LocalController(UWorld* World)
	{
		return World ? World->GetFirstPlayerController() : nullptr;
	}

	ATortugaCharacter* LocalTurtle(UWorld* World)
	{
		const APlayerController* PC = LocalController(World);
		return PC ? Cast<ATortugaCharacter>(PC->GetPawn()) : nullptr;
	}

	/** La vista del mando a Yaw y Pitch grados respecto del cuerpo. */
	void SetView(APlayerController* PC, const AActor* Body, float Yaw, float Pitch)
	{
		if (PC && Body) { PC->SetControlRotation(FRotator(Pitch, Body->GetActorRotation().Yaw + Yaw, 0.f)); }
	}

	/** Giro del hueso de la cabeza en el mundo. */
	FQuat HeadRotation(const ATortugaCharacter* Turtle)
	{
		const USkeletalMeshComponent* Body = Turtle ? Turtle->GetMesh() : nullptr;
		return Body && Body->GetBoneIndex(TEXT("Head")) != INDEX_NONE ? Body->GetBoneQuaternion(TEXT("Head")) : FQuat::Identity;
	}

	/** Cámara de las fotos: delante de la tortuga (Yaw = 0 de frente, 90 a su derecha), a la altura del pecho y la cabeza. */
	void Frame(ACameraActor* Camera, const ATortugaCharacter* Turtle, float Yaw, float Pitch)
	{
		const USkeletalMeshComponent* Body = Turtle ? Turtle->GetMesh() : nullptr;
		if (!Camera || !Body) { return; }
		const FVector Head = Body->GetBoneIndex(TEXT("Head")) != INDEX_NONE ? Body->GetBoneLocation(TEXT("Head")) : Body->Bounds.Origin;
		const FVector Focus = FMath::Lerp(Body->Bounds.Origin, Head, 0.6);
		const FVector From = Focus + FRotator(Pitch, Turtle->GetActorRotation().Yaw + Yaw, 0.f).Vector() * 320.0;
		Camera->SetActorLocationAndRotation(From, (Focus - From).Rotation());
		Camera->GetCameraComponent()->SetFieldOfView(45.f);
	}

	struct FView
	{
		const TCHAR* Name;
		float Yaw;
		float Pitch;
		/** También una foto de lado (para ver el cabeceo). */
		bool bSide;
	};

	struct FShotsState
	{
		FString Dir;
		int32 Wait = 0;
		int32 Index = 0;
		int32 Phase = 0;
		FQuat Reference = FQuat::Identity;
		TWeakObjectPtr<ACameraActor> Camera;
	};
}

static FAutoConsoleCommand GTNHeadLookShotsCommand(
	TEXT("TN.HeadLook.Shots"),
	TEXT("Pruebas: TN.HeadLook.Shots [carpeta] [espera = 8]: fotos sin interfaz de la cabeza de la tortuga del jugador siguiendo a la vista (al frente, 45° a la derecha, 60° a la izquierda, en el tope, arriba, abajo y detrás), el giro medido en el registro, y cierra el juego."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		using namespace TNHeadLookDebug;
		static const FView Views[] = {
			{ TEXT("0_frente"),       0.f,   0.f, false },
			{ TEXT("1_derecha_45"),   45.f,  0.f, false },
			{ TEXT("2_izquierda_60"), -60.f, 0.f, false },
			{ TEXT("3_tope_90"),      90.f,  0.f, false },
			{ TEXT("4_arriba_40"),    0.f,  40.f, true },
			{ TEXT("5_abajo_30"),     0.f, -30.f, true },
			{ TEXT("6_detras_170"),   170.f, 0.f, false },
		};
		TSharedRef<FShotsState> State = MakeShared<FShotsState>();
		State->Dir = Args.IsValidIndex(0) ? Args[0] : FPaths::ProjectSavedDir() / TEXT("HeadLookShots");
		// Fotogramas de espera (a 30 por segundo con -UseFixedTimeStep -FPS=30): el lobby termina de montarse.
		State->Wait = FMath::CeilToInt((Args.IsValidIndex(1) ? FCString::Atof(*Args[1]) : 8.f) * 30.f);
		IFileManager::Get().MakeDirectory(*State->Dir, true);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float) -> bool
		{
			UWorld* World = GameWorld();
			APlayerController* PC = LocalController(World);
			ATortugaCharacter* Turtle = LocalTurtle(World);
			if (!PC || !Turtle) { return true; }
			if (State->Wait > 0) { --State->Wait; return true; }
			if (!State->Camera.IsValid())
			{
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				State->Camera = World->SpawnActor<ACameraActor>(Turtle->GetActorLocation(), FRotator::ZeroRotator, Params);
				if (!State->Camera.IsValid()) { return true; }
				State->Camera->GetCameraComponent()->bConstrainAspectRatio = false;
				PC->SetViewTarget(State->Camera.Get());
				// Referencia: la cabeza con la vista al frente (lo que se mide después es el giro desde ahí).
				SetView(PC, Turtle, 0.f, 0.f);
				State->Wait = 45;
				State->Phase = -1;
				return true;
			}
			if (State->Phase == -1)
			{
				State->Reference = HeadRotation(Turtle);
				State->Phase = 0;
			}
			if (State->Index >= static_cast<int32>(UE_ARRAY_COUNT(Views)))
			{
				UE_LOG(LogTortunabo, Display, TEXT("[HeadLook] TN.HeadLook.Shots: fotos en %s"), *State->Dir);
				FPlatformMisc::RequestExit(false, TEXT("TN.HeadLook.Shots"));
				return false;
			}
			const FView& View = Views[State->Index];
			const TCHAR* Suffixes[] = { TEXT("frente"), TEXT("lado") };
			switch (State->Phase)
			{
			case 0:
				// La vista, y un segundo para que la cabeza llegue (el muelle tarda unas décimas).
				SetView(PC, Turtle, View.Yaw, View.Pitch);
				Frame(State->Camera.Get(), Turtle, 25.f, 8.f);
				State->Phase = 1;
				State->Wait = 30;
				return true;
			case 1:
			{
				// Giro medido en el hueso respecto de la referencia, en los ejes del cuerpo.
				const FQuat Delta = HeadRotation(Turtle) * State->Reference.Inverse();
				const FVector Forward = Turtle->GetActorForwardVector();
				const FVector Turned = Turtle->GetActorRotation().UnrotateVector(Delta.RotateVector(Forward));
				const float MeasuredYaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Turned.Y, Turned.X)));
				const float MeasuredPitch = static_cast<float>(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Turned.Z, -1.0, 1.0))));
				const UTN_TurtleAnimInstance* Anim = Cast<UTN_TurtleAnimInstance>(Turtle->GetMesh()->GetAnimInstance());
				UE_LOG(LogTortunabo, Display, TEXT("[HeadLook] Foto %s: vista %.0f/%.0f -> cabeza pedida %.1f/%.1f (peso %.2f), medida en el hueso %.1f/%.1f"),
					View.Name, View.Yaw, View.Pitch, Anim ? Anim->GetHeadLookYaw() : 0.f, Anim ? Anim->GetHeadLookPitch() : 0.f,
					Anim ? Anim->GetHeadLookWeight() : 0.f, MeasuredYaw, MeasuredPitch);
				FScreenshotRequest::RequestScreenshot(State->Dir / FString::Printf(TEXT("%s_%s.png"), View.Name, Suffixes[0]), false, false);
				State->Phase = View.bSide ? 2 : 4;
				State->Wait = 4;
				return true;
			}
			case 2:
				Frame(State->Camera.Get(), Turtle, 90.f, 5.f);
				State->Phase = 3;
				State->Wait = 6;
				return true;
			case 3:
				FScreenshotRequest::RequestScreenshot(State->Dir / FString::Printf(TEXT("%s_%s.png"), View.Name, Suffixes[1]), false, false);
				State->Phase = 4;
				State->Wait = 4;
				return true;
			default:
				++State->Index;
				State->Phase = 0;
				return true;
			}
		}), 0.f);
	}));

static FAutoConsoleCommand GTNHeadLookSweepCommand(
	TEXT("TN.HeadLook.Sweep"),
	TEXT("Pruebas: TN.HeadLook.Sweep [segundos = 20]: mueve sola la vista del jugador local (guiñada ±60° y cabeceo de -25 a 25°) para ver el giro de la cabeza en las demás máquinas (TN.HeadLook.Log 1)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		using namespace TNHeadLookDebug;
		const float Seconds = Args.IsValidIndex(0) ? FCString::Atof(*Args[0]) : 20.f;
		TSharedRef<float> Elapsed = MakeShared<float>(-1.f);
		TSharedRef<float> LogIn = MakeShared<float>(0.f);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Seconds, Elapsed, LogIn](float Dt) -> bool
		{
			UWorld* World = GameWorld();
			APlayerController* PC = LocalController(World);
			ATortugaCharacter* Turtle = LocalTurtle(World);
			if (!PC || !Turtle) { return true; }
			if (*Elapsed < 0.f)
			{
				*Elapsed = 0.f;
				UE_LOG(LogTortunabo, Display, TEXT("[HeadLook] Barrido de la vista de %s durante %.0f s"), *Turtle->GetName(), Seconds);
			}
			*Elapsed += Dt;
			const float Yaw = 60.f * FMath::Sin(*Elapsed * 2.f * UE_PI / 6.f);
			const float Pitch = 25.f * FMath::Sin(*Elapsed * 2.f * UE_PI / 4.3f);
			SetView(PC, Turtle, Yaw, Pitch);
			*LogIn -= Dt;
			if (*LogIn <= 0.f)
			{
				*LogIn = 1.f;
				UE_LOG(LogTortunabo, Display, TEXT("[HeadLook] Barrido: vista %.1f/%.1f"), Yaw, Pitch);
			}
			if (*Elapsed >= Seconds)
			{
				SetView(PC, Turtle, 0.f, 0.f);
				UE_LOG(LogTortunabo, Display, TEXT("[HeadLook] Barrido acabado"));
				return false;
			}
			return true;
		}), 0.f);
	}));

#endif
