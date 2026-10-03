// Fotos de prueba de la tortuga (TN.Art.TurtleShots), fuera de Shipping: comprueba sin abrir el editor que el jugador y
// todas sus copias (tendero, general, escaparate de cosméticos y podio) llevan la misma malla y las piezas de arte de la
// tortuga (Docs/Arte_Assets.md, «La tortuga»). Saca fotos sin interfaz del jugador (de frente, de espaldas y con la
// pataleta del podio en dos momentos, para ver que las piezas siguen la animación), del tendero y del general, guarda lo
// que dibujan el escaparate y el podio, lista las piezas (TN.Art.Slots Turtle) y cierra el juego.
//   UnrealEditor-Win64-DebugGame.exe <uproject> /Game/Maps/Lobby/LVL_Lobby -game -RenderOffScreen -ResX=1600 -ResY=900
//     -NoSteam -UseFixedTimeStep -FPS=30 -ExecCmds="TN.Art.TurtleShots C:/ruta"
// También en el PIE, desde la consola del juego (el editor se cierra al acabar).

#if !UE_BUILD_SHIPPING

#include "Art/TN_TurtleArt.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/TN_CosmeticsTypes.h"
#include "Core/TN_Log.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Lobby/TN_CosmeticPreview.h"
#include "Lobby/TN_GeneralBriefing.h"
#include "Lobby/TN_ShopKeeper.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TextureResource.h"
#include "Player/TN_TurtleAnimInstance.h"
#include "Player/TortugaCharacter.h"
#include "UI/Race/TN_RacePodiumStage.h"
#include "UnrealClient.h"

namespace TNTurtleShotsDetail
{
	/** Un paso: hace algo y dice cuántos fotogramas esperar antes del siguiente. */
	using FStep = TFunction<int32()>;

	struct FState
	{
		FString Dir;
		int32 Wait = 0;
		int32 Next = 0;
		TArray<FStep> Steps;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<ACameraActor> Camera;
	};

	UWorld* GameWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && Context.World()) { return Context.World(); }
		}
		return nullptr;
	}

	template <class T>
	T* FirstActor(UWorld* World)
	{
		for (TActorIterator<T> It(World); It; ++It) { return *It; }
		return nullptr;
	}

	/**
	 * Cámara mirando a la malla de una tortuga: al centro de su caja, desde Yaw grados respecto a hacia donde mira el actor
	 * (0 = de frente) y Pitch por encima, a la distancia a la que la tortuga entera ocupa unos cuatro quintos del alto.
	 */
	void Frame(ACameraActor* Camera, const AActor* Owner, const USkeletalMeshComponent* Body, float Yaw, float Pitch)
	{
		if (!Camera || !Owner || !Body) { return; }
		constexpr float Fov = 50.f;
		const FBoxSphereBounds Bounds = Body->Bounds;
		const double TanHalfV = FMath::Tan(FMath::DegreesToRadians(Fov * 0.5)) * 9.0 / 16.0;
		const double Distance = FMath::Max(Bounds.BoxExtent.Z, 30.0) * 1.25 / TanHalfV;
		const FVector Focus = Bounds.Origin;
		const FVector From = Focus + FRotator(Pitch, Owner->GetActorRotation().Yaw + Yaw, 0.f).Vector() * Distance;
		Camera->SetActorLocationAndRotation(From, (Focus - From).Rotation());
		Camera->GetCameraComponent()->SetFieldOfView(Fov);
	}

	/**
	 * Esconde (o vuelve a enseñar) lo que rodea a la tortuga de un actor (el puesto del tendero, la tienda del general):
	 * todo lo que dibuja el actor salvo la malla de la tortuga y lo que lleva enganchado (sombrero, piezas de arte).
	 */
	void Isolate(AActor* Owner, const USkeletalMeshComponent* Body, bool bHide)
	{
		if (!Owner || !Body) { return; }
		TInlineComponentArray<UPrimitiveComponent*> Prims(Owner);
		for (UPrimitiveComponent* Prim : Prims)
		{
			if (Prim != Body && !Prim->IsAttachedTo(Body)) { Prim->SetHiddenInGame(bHide); }
		}
		// Sus luces, pegadas al techo del puesto, queman la foto sin él delante.
		TInlineComponentArray<ULightComponent*> Lights(Owner);
		for (ULightComponent* Light : Lights) { Light->SetVisibility(!bHide); }
	}

	/** Lo que dibuja un render target, en PNG (sRGB de 8 bits). */
	void SaveTarget(UTextureRenderTarget2D* Target, const FString& File)
	{
		FTextureRenderTargetResource* Resource = Target ? Target->GameThread_GetRenderTargetResource() : nullptr;
		TArray<FColor> Pixels;
		FReadSurfaceDataFlags Flags(RCM_UNorm, CubeFace_MAX);
		Flags.SetLinearToGamma(true);
		if (!Resource || !Resource->ReadPixels(Pixels, Flags) || Pixels.Num() != Target->SizeX * Target->SizeY)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Arte] TN.Art.TurtleShots: no se ha podido leer %s"), *File);
			return;
		}
		for (FColor& Pixel : Pixels) { Pixel.A = 255; }
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Target->SizeX, Target->SizeY, Pixels, Png);
		FFileHelper::SaveArrayToFile(Png, *File);
		UE_LOG(LogTortunabo, Display, TEXT("[Arte] TN.Art.TurtleShots: %s"), *File);
	}

	void Shot(const FString& File)
	{
		FScreenshotRequest::RequestScreenshot(File, false, false);
		UE_LOG(LogTortunabo, Display, TEXT("[Arte] TN.Art.TurtleShots: %s"), *File);
	}

	UTN_TurtleAnimInstance* AnimOf(const ACharacter* Character)
	{
		const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
		return Mesh ? Cast<UTN_TurtleAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
	}

	TArray<FStep> Plan(const TSharedRef<FState>& State)
	{
		TArray<FStep> Steps;
		TWeakPtr<FState> Weak = State;
		auto Ctx = [Weak]() { return Weak.Pin(); };
		auto Path = [Ctx](const TCHAR* Name) { return Ctx()->Dir / (FString(Name) + TEXT(".png")); };
		auto Pawn = [Ctx]() -> ATortugaCharacter*
		{
			UWorld* World = Ctx()->World.Get();
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			return PC ? Cast<ATortugaCharacter>(PC->GetPawn()) : nullptr;
		};
		// Una foto del mundo: coloca la cámara, deja que se asienten la luz y el antialias y saca la foto.
		auto WorldShot = [&Steps, Ctx, Path](const TCHAR* Name, TFunction<void(ACameraActor*)> Place)
		{
			Steps.Add([Ctx, Place]()
			{
				Place(Ctx()->Camera.Get());
				return 10;
			});
			const FString File = Path(Name);
			Steps.Add([File]()
			{
				Shot(File);
				return 4;
			});
		};

		WorldShot(TEXT("jugador_frente"), [Pawn](ACameraActor* Cam)
		{
			const ATortugaCharacter* P = Pawn();
			Frame(Cam, P, P ? P->GetMesh() : nullptr, 25.f, 12.f);
		});
		WorldShot(TEXT("jugador_espalda"), [Pawn](ACameraActor* Cam)
		{
			const ATortugaCharacter* P = Pawn();
			Frame(Cam, P, P ? P->GetMesh() : nullptr, 150.f, 20.f);
		});
		// El tendero y el general, sin su puesto ni su tienda delante.
		auto NpcShot = [&Steps, &WorldShot, Ctx](const TCHAR* Name, TFunction<AActor*(UWorld*)> Find)
		{
			auto BodyOf = [](AActor* Npc) { return Npc ? Npc->FindComponentByClass<USkeletalMeshComponent>() : nullptr; };
			WorldShot(Name, [Ctx, Find, BodyOf](ACameraActor* Cam)
			{
				AActor* Npc = Find(Ctx()->World.Get());
				Isolate(Npc, BodyOf(Npc), true);
				Frame(Cam, Npc, BodyOf(Npc), 30.f, 12.f);
			});
			Steps.Add([Ctx, Find, BodyOf]()
			{
				AActor* Npc = Find(Ctx()->World.Get());
				Isolate(Npc, BodyOf(Npc), false);
				return 1;
			});
		};
		NpcShot(TEXT("tendero"), [](UWorld* World) -> AActor* { return FirstActor<ATN_ShopKeeper>(World); });
		NpcShot(TEXT("general"), [](UWorld* World) -> AActor* { return FirstActor<ATN_GeneralBriefing>(World); });

		// La pataleta del podio en el jugador, en dos momentos de su bucle: las piezas tienen que ir con los huesos.
		Steps.Add([Pawn]()
		{
			if (UTN_TurtleAnimInstance* Anim = AnimOf(Pawn())) { Anim->SetCelebration(ETNTurtleCelebration::Tantrum); }
			return 45;
		});
		WorldShot(TEXT("jugador_pataleta_1"), [Pawn](ACameraActor* Cam)
		{
			const ATortugaCharacter* P = Pawn();
			Frame(Cam, P, P ? P->GetMesh() : nullptr, 40.f, 15.f);
		});
		Steps.Add([]() { return 7; });
		WorldShot(TEXT("jugador_pataleta_2"), [Pawn](ACameraActor* Cam)
		{
			const ATortugaCharacter* P = Pawn();
			Frame(Cam, P, P ? P->GetMesh() : nullptr, 40.f, 15.f);
		});
		Steps.Add([Pawn]()
		{
			if (UTN_TurtleAnimInstance* Anim = AnimOf(Pawn())) { Anim->SetCelebration(ETNTurtleCelebration::None); }
			return 1;
		});

		// Escaparate de cosméticos (tienda y probador): la vista en directo y dos miniaturas.
		Steps.Add([Ctx]()
		{
			if (ATN_CosmeticPreview* Preview = ATN_CosmeticPreview::Get(Ctx()->World.Get()))
			{
				Preview->SetLook(FTN_TurtleLook());
				Preview->SetLiveCapture(true);
				Preview->GetThumbnail(ETNCosmeticCategory::Shell, NAME_None);
				Preview->GetThumbnail(ETNCosmeticCategory::Body, NAME_None);
			}
			return 20;
		});
		Steps.Add([Ctx, Path]()
		{
			if (ATN_CosmeticPreview* Preview = ATN_CosmeticPreview::Get(Ctx()->World.Get()))
			{
				SaveTarget(Preview->GetRenderTarget(), Path(TEXT("escaparate")));
				SaveTarget(Preview->GetThumbnail(ETNCosmeticCategory::Shell, NAME_None), Path(TEXT("escaparate_miniatura_caparazon")));
				SaveTarget(Preview->GetThumbnail(ETNCosmeticCategory::Body, NAME_None), Path(TEXT("escaparate_miniatura_cuerpo")));
				Preview->SetLiveCapture(false);
			}
			return 1;
		});

		// Podio de la carrera: tres tortugas de serie con sus poses.
		Steps.Add([Ctx]()
		{
			if (ATN_RacePodiumStage* Podium = ATN_RacePodiumStage::Get(Ctx()->World.Get()))
			{
				Podium->SetPodium({ FTN_TurtleLook(), FTN_TurtleLook(), FTN_TurtleLook() });
				Podium->SetLive(true);
			}
			return 40;
		});
		Steps.Add([Ctx, Path]()
		{
			if (ATN_RacePodiumStage* Podium = ATN_RacePodiumStage::Get(Ctx()->World.Get()))
			{
				SaveTarget(Podium->GetRenderTarget(), Path(TEXT("podio")));
				Podium->SetLive(false);
			}
			return 1;
		});

		// Qué malla lleva cada tortuga, a qué escala y con cuántas piezas de Arte: todas deberían llevar la del personaje.
		Steps.Add([Ctx, Pawn]()
		{
			UWorld* World = Ctx()->World.Get();
			const AActor* Owners[] = { Pawn(), FirstActor<ATN_ShopKeeper>(World), FirstActor<ATN_GeneralBriefing>(World),
				FirstActor<ATN_CosmeticPreview>(World), FirstActor<ATN_RacePodiumStage>(World) };
			UE_LOG(LogTortunabo, Display, TEXT("[Arte] TN.Art.TurtleShots: malla del personaje %s"), *GetNameSafe(TNTurtleArt::GetMesh()));
			for (const AActor* Owner : Owners)
			{
				if (!Owner) { continue; }
				TInlineComponentArray<USkeletalMeshComponent*> Bodies(Owner);
				for (const USkeletalMeshComponent* Body : Bodies)
				{
					TArray<UPrimitiveComponent*> Pieces;
					TNTurtleArt::GetPieceComponents(Body, Pieces);
					UE_LOG(LogTortunabo, Display, TEXT("[Arte] TN.Art.TurtleShots:   %s.%s: %s, escala %.2f, %d piezas de Arte"),
						*Owner->GetName(), *Body->GetName(), *GetNameSafe(Body->GetSkeletalMeshAsset()), Body->GetRelativeScale3D().Z, Pieces.Num());
				}
			}
			return 1;
		});
		return Steps;
	}
}

static FAutoConsoleCommand GTNArtTurtleShotsCommand(
	TEXT("TN.Art.TurtleShots"),
	TEXT("Pruebas: TN.Art.TurtleShots [carpeta] [espera = 10]: fotos sin interfaz del jugador, el tendero, el general, el escaparate de cosméticos y el podio, para ver la malla y las piezas de arte de la tortuga; lista las piezas (TN.Art.Slots Turtle) y cierra el juego."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		using namespace TNTurtleShotsDetail;
		TSharedRef<FState> State = MakeShared<FState>();
		State->Dir = Args.IsValidIndex(0) ? Args[0] : FPaths::ProjectSavedDir() / TEXT("TurtleShots");
		// Fotogramas de espera (a 30 por segundo con -UseFixedTimeStep -FPS=30): el lobby termina de montarse.
		State->Wait = FMath::CeilToInt((Args.IsValidIndex(1) ? FCString::Atof(*Args[1]) : 10.f) * 30.f);
		State->Steps = Plan(State);
		IFileManager::Get().MakeDirectory(*State->Dir, true);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float) -> bool
		{
			UWorld* World = GameWorld();
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			if (!PC || !PC->GetPawn()) { return true; }
			if (State->Wait > 0) { --State->Wait; return true; }
			if (!State->Camera.IsValid())
			{
				State->World = World;
				GEngine->Exec(World, TEXT("TN.Art.Slots Turtle"));
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				State->Camera = World->SpawnActor<ACameraActor>(PC->GetPawn()->GetActorLocation(), FRotator::ZeroRotator, Params);
				if (!State->Camera.IsValid()) { return true; }
				State->Camera->GetCameraComponent()->bConstrainAspectRatio = false;
				PC->SetViewTarget(State->Camera.Get());
			}
			if (!State->Steps.IsValidIndex(State->Next))
			{
				UE_LOG(LogTortunabo, Display, TEXT("[Arte] TN.Art.TurtleShots: fotos en %s"), *State->Dir);
				FPlatformMisc::RequestExit(false, TEXT("TN.Art.TurtleShots"));
				return false;
			}
			State->Wait = State->Steps[State->Next++]();
			return true;
		}), 0.f);
	}));

#endif
