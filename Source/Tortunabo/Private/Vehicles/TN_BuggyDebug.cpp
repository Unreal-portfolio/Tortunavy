// Comandos de consola del buggy para la prueba de humo (fuera de Shipping). Se pueden encadenar en -ExecCmds en el
// mismo frame: cada uno espera lo que necesita con un temporizador del mundo.
//   TN.Rally.SpawnBuggy                       (servidor) crea un buggy delante del jugador y lo posee
//   TN.Rally.DebugDrive <gas> <giro> <s> [espera]  conduce con SetAIDriveInput y registra el desplazamiento
//   TN.Rally.SpawnTarget [cm] [espera]       (servidor) buggy vacío delante como blanco
//   TN.Rally.DebugFireAll [espera] [atrás]            dispara una vez cada munición (coco, alga, burbuja, mortero, tinta), 1 s entre una y otra
//   TN.Rally.DebugQuitAfter <s>               cierra el juego pasados s segundos
//   TN.Rally.DebugEffects [vida] [espera] [turbo]   (servidor) deja el buggy con esa vida (humo) y el turbo pisado (llama)
//   TN.Rally.LocalFire [especial] [espera] [veces]   (cliente o anfitrión) la jugadora local pide disparos al servidor
//   TN.Rally.StatusLater <espera> [veces] [intervalo]  TN.Rally.Status diferido (en un cliente, lo replicado)
//   TN.Rally.DebugSwapSeats [espera]          (servidor) en cada buggy biplaza, la artillera pasa a conducir y viceversa
//   TN.Rally.DebugPhotos <espera> <carpeta> [quieto] [veces] [intervalo]  fotos del buggy sin interfaz (lámina o persecución)
//   TN.Rally.ViewShot <espera> <fichero> <x> <y> <z> <pitch> <yaw> [fov]  captura sin interfaz desde una cámara fija
// LocalFire, StatusLater y DebugQuitAfter esperan con el ticker del motor, no con el del mundo: en un cliente, -ExecCmds
// corre antes de conectarse y el mundo de entonces se destruye al viajar al mapa del servidor.

#include "Vehicles/TN_Buggy.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Rally/TN_RallyGameState.h"
#include "Rally/TN_RallyLogic.h"
#include "Vehicles/TN_BuggyGunnerPawn.h"
#include "Vehicles/TN_BuggyHealthComponent.h"
#include "Vehicles/TN_BuggyMath.h"
#include "Vehicles/TN_BuggyTurretComponent.h"
#include "Vehicles/TN_RallyTurretLogic.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "TimerManager.h"

#if !UE_BUILD_SHIPPING

namespace TNBuggyDebug
{
	float FloatArg(const TArray<FString>& Args, int32 Index, float Default)
	{
		return Args.IsValidIndex(Index) ? FCString::Atof(*Args[Index]) : Default;
	}

	/** El buggy del primer jugador o, si no conduce ninguno, el primero del mundo. */
	ATN_Buggy* FindBuggy(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (ATN_Buggy* Mine = Cast<ATN_Buggy>(PC->GetPawn()))
			{
				return Mine;
			}
		}
		for (TActorIterator<ATN_Buggy> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	/** Llama a Action pasados Seconds (0 = en el siguiente frame), si el mundo sigue vivo. */
	void After(UWorld* World, float Seconds, TFunction<void(UWorld*)> Action)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, Action]()
		{
			if (UWorld* Alive = WeakWorld.Get())
			{
				Action(Alive);
			}
		}), FMath::Max(Seconds, 0.01f), false);
	}

	/** El mundo de juego actual (el del mapa del servidor una vez conectado). */
	UWorld* CurrentGameWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && Context.World())
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	/** Como After, pero con el ticker del motor: sobrevive al viaje de mapa y actúa en el mundo que haya entonces. */
	void AfterGlobal(float Seconds, TFunction<void(UWorld*)> Action)
	{
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Action](float)
		{
			if (UWorld* World = CurrentGameWorld())
			{
				Action(World);
			}
			return false;
		}), FMath::Max(Seconds, 0.01f));
	}

	/** La jugadora local pide un disparo por el mismo camino que su entrada (RPC al servidor). */
	void LocalFire(UWorld* World, bool bSpecial)
	{
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (ATN_BuggyGunnerPawn* Gunner = Cast<ATN_BuggyGunnerPawn>(Pawn))
		{
			Gunner->RequestFire(bSpecial);
			UE_LOG(LogTNBuggy, Log, TEXT("[Humo] la artillera local (%s) pide disparo %s al servidor"), *PC->GetName(),
				bSpecial ? TEXT("especial") : TEXT("de coco"));
		}
		else if (ATN_Buggy* Driver = Cast<ATN_Buggy>(Pawn))
		{
			Driver->RequestDriverFire(bSpecial, false);
			UE_LOG(LogTNBuggy, Log, TEXT("[Humo] la conductora local (%s) pide disparo %s al servidor"), *PC->GetName(),
				bSpecial ? TEXT("especial") : TEXT("de coco"));
		}
		else
		{
			UE_LOG(LogTNBuggy, Warning, TEXT("[Humo] TN.Rally.LocalFire: la jugadora local no va en un buggy (peón %s)"), *GetNameSafe(Pawn));
		}
	}

	void SpawnBuggy(UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (!PC || World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogTNBuggy, Warning, TEXT("[Humo] TN.Rally.SpawnBuggy: solo en el servidor y con un jugador"));
			return;
		}
		FVector ViewLocation;
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		const APawn* Pawn = PC->GetPawn();
		const FVector Base = Pawn ? Pawn->GetActorLocation() : ViewLocation;
		const FRotator Yaw(0.f, Pawn ? Pawn->GetActorRotation().Yaw : ViewRotation.Yaw, 0.f);
		const FVector Location = Base + Yaw.Vector() * 800.f + FVector(0.f, 0.f, 150.f);

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		ATN_Buggy* Buggy = World->SpawnActor<ATN_Buggy>(ATN_Buggy::StaticClass(), Location, Yaw, Params);
		if (!Buggy)
		{
			UE_LOG(LogTNBuggy, Error, TEXT("[Humo] no se ha podido crear el buggy"));
			return;
		}
		Buggy->SetRallyTeamIndex(0);
		const bool bSeated = Buggy->SeatController(PC, ETNRallySeat::Driver);
		UE_LOG(LogTNBuggy, Log, TEXT("[Humo] buggy %s creado en (%.0f, %.0f, %.0f); conductora sentada=%d"), *Buggy->GetName(),
			Location.X, Location.Y, Location.Z, bSeated ? 1 : 0);
	}

	/** Buggy sin nadie (equipo 1) DistanceCm delante del buggy del jugador: blanco para los disparos. */
	void SpawnTarget(UWorld* World, float DistanceCm)
	{
		ATN_Buggy* Mine = FindBuggy(World);
		if (!Mine || !Mine->HasAuthority())
		{
			UE_LOG(LogTNBuggy, Warning, TEXT("[Humo] TN.Rally.SpawnTarget: no hay buggy en el servidor"));
			return;
		}
		// Delante si hay suelo; si no (borde del mapa), detrás o a un lado.
		const FRotator Yaw(0.f, Mine->GetActorRotation().Yaw, 0.f);
		const FVector Directions[] = { Yaw.Vector(), -Yaw.Vector(), FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), -FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y) };
		FVector Location = Mine->GetActorLocation() + Directions[0] * DistanceCm;
		for (const FVector& Dir : Directions)
		{
			const FVector Probe = Mine->GetActorLocation() + Dir * DistanceCm;
			FHitResult Ground;
			FCollisionQueryParams Query(FName(TEXT("TNRallySpawnTarget")), false, Mine);
			if (World->LineTraceSingleByChannel(Ground, Probe + FVector(0.f, 0.f, 500.f), Probe - FVector(0.f, 0.f, 2000.f), ECC_WorldStatic, Query))
			{
				Location = Ground.ImpactPoint;
				break;
			}
		}
		Location.Z += 150.f;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		ATN_Buggy* Target = World->SpawnActor<ATN_Buggy>(ATN_Buggy::StaticClass(), Location, Yaw, Params);
		if (Target)
		{
			Target->SetRallyTeamIndex(1);
		}
		UE_LOG(LogTNBuggy, Log, TEXT("[Humo] blanco %s a %.0f cm"), *GetNameSafe(Target), DistanceCm);
	}

	void Drive(UWorld* World, float Throttle, float Steer, float Seconds)
	{
		ATN_Buggy* Buggy = FindBuggy(World);
		if (!Buggy || !Buggy->HasAuthority())
		{
			UE_LOG(LogTNBuggy, Warning, TEXT("[Humo] TN.Rally.DebugDrive: no hay buggy en el servidor"));
			return;
		}
		const FVector Start = Buggy->GetActorLocation();
		UE_LOG(LogTNBuggy, Log, TEXT("[Humo] conduce %s gas=%.2f giro=%.2f durante %.1f s desde (%.0f, %.0f, %.0f)"), *Buggy->GetName(),
			Throttle, Steer, Seconds, Start.X, Start.Y, Start.Z);
		// Las entradas se mantienen cada frame (a 30 Hz) hasta el final, como haría el piloto IA.
		TWeakObjectPtr<ATN_Buggy> WeakBuggy(Buggy);
		const int32 Steps = FMath::Max(1, FMath::RoundToInt(Seconds * 30.f));
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			After(World, Step / 30.f, [WeakBuggy, Throttle, Steer](UWorld*)
			{
				if (ATN_Buggy* Alive = WeakBuggy.Get())
				{
					Alive->SetAIDriveInput(Throttle, 0.f, Steer, false);
				}
			});
		}
		After(World, Seconds, [WeakBuggy, Start, Seconds](UWorld*)
		{
			ATN_Buggy* Alive = WeakBuggy.Get();
			if (!Alive)
			{
				UE_LOG(LogTNBuggy, Error, TEXT("[Humo] el buggy ha desaparecido durante la prueba"));
				return;
			}
			// Frena 2 s y luego suelta todo con el freno de mano: con bReverseAsBrake, seguir frenando parado mete la
			// marcha atrás, y el freno de mano a toda velocidad lo hace trompear.
			Alive->SetAIDriveInput(0.f, 1.f, 0.f, false);
			After(Alive->GetWorld(), 2.f, [WeakBuggy](UWorld*)
			{
				if (ATN_Buggy* Stopped = WeakBuggy.Get())
				{
					Stopped->SetAIDriveInput(0.f, 0.f, 0.f, true);
				}
			});
			const FVector End = Alive->GetActorLocation();
			const float Moved = FVector::Dist2D(Start, End);
			UE_LOG(LogTNBuggy, Log, TEXT("[Humo] desplazamiento=%.0f cm en %.1f s velocidad=%.0f cm/s volcado=%d arribaZ=%.2f"),
				Moved, Seconds, Alive->GetForwardSpeedCms(), Alive->IsFlipped() ? 1 : 0, Alive->GetActorUpVector().Z);
		});
	}

	void FireAll(UWorld* World, bool bBackward)
	{
		const ETNRallyAmmo Order[] = { ETNRallyAmmo::Coco, ETNRallyAmmo::Alga, ETNRallyAmmo::Burbuja, ETNRallyAmmo::Mortero, ETNRallyAmmo::Tinta,
			ETNRallyAmmo::Ancla, ETNRallyAmmo::Concha, ETNRallyAmmo::ConchaGuiada, ETNRallyAmmo::Erizos,
			ETNRallyAmmo::Medusa, ETNRallyAmmo::Arpon, ETNRallyAmmo::PezGlobo };
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Order); ++Index)
		{
			const ETNRallyAmmo Ammo = Order[Index];
			After(World, static_cast<float>(Index), [Ammo, bBackward](UWorld* Alive)
			{
				ATN_Buggy* Buggy = FindBuggy(Alive);
				if (!Buggy || !Buggy->HasAuthority() || !Buggy->GetTurret())
				{
					UE_LOG(LogTNBuggy, Warning, TEXT("[Humo] TN.Rally.DebugFireAll: no hay buggy en el servidor"));
					return;
				}
				const bool bSpecial = TNRallyTurret::IsSpecial(Ammo);
				if (bSpecial)
				{
					Buggy->GiveSpecialAmmo(Ammo, TNRallyTurret::SpecFor(Ammo).BoxCharges);
				}
				for (TActorIterator<ATN_Buggy> It(Alive); It; ++It)
				{
					if (*It != Buggy)
					{
						UE_LOG(LogTNBuggy, Log, TEXT("[Humo] %s a %.0f cm del tirador"), *It->GetName(), FVector::Dist(It->GetActorLocation(), Buggy->GetActorLocation()));
					}
				}
				const int32 ProjectilesBefore = Alive->GetActorCount();
				Buggy->DriverFireAuto(bSpecial, bBackward);
				UE_LOG(LogTNBuggy, Log, TEXT("[Humo] disparo de %s: actores %d -> %d, calor=%.2f, especial=%s x%d"), *UEnum::GetValueAsString(Ammo),
					ProjectilesBefore, Alive->GetActorCount(), Buggy->GetTurret()->GetHeat01(),
					*UEnum::GetValueAsString(Buggy->GetSpecialAmmo()), Buggy->GetTurret()->GetSpecialCharges());
			});
		}
	}

	FAutoConsoleCommandWithWorldAndArgs CmdSpawnBuggy(TEXT("TN.Rally.SpawnBuggy"),
		TEXT("Rally (servidor): crea un buggy 8 m delante del jugador y lo posee como conductora."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			SpawnBuggy(World);
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdSpawnTarget(TEXT("TN.Rally.SpawnTarget"),
		TEXT("Rally (servidor): TN.Rally.SpawnTarget [distancia cm = 2500] [espera]: crea un buggy vacío delante del buggy del jugador como blanco."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const float Distance = FloatArg(Args, 0, 2500.f);
			After(World, FloatArg(Args, 1, 0.f), [Distance](UWorld* Alive) { SpawnTarget(Alive, Distance); });
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdDebugDrive(TEXT("TN.Rally.DebugDrive"),
		TEXT("Rally (servidor): TN.Rally.DebugDrive <gas 0..1> <giro -1..1> <segundos> [espera]: conduce el buggy del jugador con SetAIDriveInput y registra el desplazamiento (LogTNBuggy [Humo])."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const float Throttle = FloatArg(Args, 0, 1.f);
			const float Steer = FloatArg(Args, 1, 0.f);
			const float Seconds = FloatArg(Args, 2, 5.f);
			// Espera por defecto de 1 s: el buggy recién creado cae y se asienta antes de arrancar.
			After(World, FloatArg(Args, 3, 1.f), [Throttle, Steer, Seconds](UWorld* Alive) { Drive(Alive, Throttle, Steer, Seconds); });
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdDebugFireAll(TEXT("TN.Rally.DebugFireAll"),
		TEXT("Rally (servidor): TN.Rally.DebugFireAll [espera] [atrás 0|1]: el buggy del jugador dispara una vez cada munición, 1 s entre una y otra, con el apuntado automático de la conductora sola."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const bool bBackward = FloatArg(Args, 1, 0.f) != 0.f;
			After(World, FloatArg(Args, 0, 0.f), [bBackward](UWorld* Alive) { FireAll(Alive, bBackward); });
		}));

	/** El buggy en el que va el primer jugador (de conductora o de artillera) o, si no va en ninguno, FindBuggy. */
	ATN_Buggy* FindPlayerBuggy(UWorld* World)
	{
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		const ATN_RallyGameState* RallyState = World ? World->GetGameState<ATN_RallyGameState>() : nullptr;
		const FTNRallyStanding* Mine = RallyState && PC ? RallyState->FindStandingForPlayer(PC->PlayerState) : nullptr;
		if (ATN_Buggy* Seated = Mine ? Cast<ATN_Buggy>(Mine->Vehicle) : nullptr)
		{
			return Seated;
		}
		return FindBuggy(World);
	}

	FAutoConsoleCommandWithWorldAndArgs CmdGiveAmmo(TEXT("TN.Rally.GiveAmmo"),
		TEXT("Rally y karts (servidor o partida sola): TN.Rally.GiveAmmo Concha|ConchaGuiada|Alga|Tinta|Burbuja|Mortero|Ancla|Erizos|Medusa|Arpon|PezGlobo [cargas]: munición especial de las cajas «?» para el buggy del jugador (sin cargas, las de una caja)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			ATN_Buggy* Buggy = FindPlayerBuggy(World);
			const int64 Value = Args.Num() > 0 ? StaticEnum<ETNRallyAmmo>()->GetValueByNameString(Args[0]) : INDEX_NONE;
			const ETNRallyAmmo Ammo = Value == INDEX_NONE ? ETNRallyAmmo::None : static_cast<ETNRallyAmmo>(Value);
			if (!Buggy || !Buggy->HasAuthority() || !TNRallyTurret::IsSpecial(Ammo))
			{
				UE_LOG(LogTNRally, Display, TEXT("TN.Rally.GiveAmmo: hace falta un buggy propio en el servidor y una munición especial (Concha, ConchaGuiada, Alga, Tinta, Burbuja, Mortero, Ancla, Erizos, Medusa, Arpon o PezGlobo)."));
				return;
			}
			const int32 Charges = Args.Num() > 1 ? FMath::Max(1, FCString::Atoi(*Args[1])) : TNRally::ChargesFor(Ammo);
			Buggy->GiveSpecialAmmo(Ammo, Charges);
			UE_LOG(LogTNRally, Display, TEXT("TN.Rally.GiveAmmo: %s ×%d para %s."), *UEnum::GetValueAsString(Ammo), Charges, *Buggy->GetName());
		}));

	/** Vida fijada y turbo pisado en el buggy del jugador, para ver el humo (#296) y la llama (#294) sin combate ni mando. */
	void ShowEffects(UWorld* World, float Health, bool bBoost)
	{
		ATN_Buggy* Buggy = FindBuggy(World);
		UTN_BuggyHealthComponent* HealthComponent = Buggy ? Buggy->GetHealthComponent() : nullptr;
		if (!Buggy || !Buggy->HasAuthority() || !HealthComponent)
		{
			UE_LOG(LogTNBuggy, Warning, TEXT("[Humo] TN.Rally.DebugEffects: no hay buggy en el servidor"));
			return;
		}
		HealthComponent->ApplyDamage(HealthComponent->GetHealth() - FMath::Max(1.f, Health));
		Buggy->DebugHoldBoost(bBoost);
		UE_LOG(LogTNBuggy, Log, TEXT("[Humo] efectos de %s: vida=%.0f humo=%d bocanadas=%d turbo=%d"), *Buggy->GetName(),
			HealthComponent->GetHealth(), HealthComponent->IsSmoking() ? 1 : 0, HealthComponent->IsEmittingSmokePuffs() ? 1 : 0,
			bBoost ? 1 : 0);
	}

	FAutoConsoleCommandWithWorldAndArgs CmdDebugEffects(TEXT("TN.Rally.DebugEffects"),
		TEXT("Rally (servidor): TN.Rally.DebugEffects [vida = 30] [espera] [turbo 1|0 = 1]: deja el buggy del jugador con esa vida (humo a media vida) y el turbo pisado con la barra llena (llama)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const float Health = FloatArg(Args, 0, 30.f);
			const bool bBoost = FloatArg(Args, 2, 1.f) != 0.f;
			After(World, FloatArg(Args, 1, 0.f), [Health, bBoost](UWorld* Alive) { ShowEffects(Alive, Health, bBoost); });
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdDebugQuitAfter(TEXT("TN.Rally.DebugQuitAfter"),
		TEXT("Rally: TN.Rally.DebugQuitAfter <segundos>: cierra el juego pasado ese tiempo (para encadenar la prueba de humo en -ExecCmds)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			AfterGlobal(FloatArg(Args, 0, 10.f), [](UWorld* Alive)
			{
				UE_LOG(LogTNBuggy, Log, TEXT("[Humo] fin"));
				UKismetSystemLibrary::QuitGame(Alive, Alive->GetFirstPlayerController(), EQuitPreference::Quit, false);
			});
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdDebugTeleport(TEXT("TN.Rally.DebugTeleport"),
		TEXT("Rally (servidor): TN.Rally.DebugTeleport <dx> <dy> <dz> [espera]: RallyTeleport del buggy del jugador (o el primero) desplazado y su posición 0, 0,1 y 1 s después."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const FVector Offset(FloatArg(Args, 0, 0.f), FloatArg(Args, 1, 0.f), FloatArg(Args, 2, 500.f));
			AfterGlobal(FloatArg(Args, 3, 0.f), [Offset](UWorld* Alive)
			{
				ATN_Buggy* Buggy = FindBuggy(Alive);
				if (!Buggy || !Buggy->HasAuthority())
				{
					UE_LOG(LogTNBuggy, Warning, TEXT("[Humo] TN.Rally.DebugTeleport: no hay buggy en el servidor"));
					return;
				}
				const FVector From = Buggy->GetActorLocation();
				const FVector To = From + Offset;
				Buggy->RallyTeleport(FTransform(Buggy->GetActorRotation(), To), 0.f, 0.f);
				UE_LOG(LogTNBuggy, Log, TEXT("[Humo] teletransporte de %s: de (%.0f, %.0f, %.0f) a (%.0f, %.0f, %.0f); ahora (%.0f, %.0f, %.0f)"),
					*Buggy->GetName(), From.X, From.Y, From.Z, To.X, To.Y, To.Z, Buggy->GetActorLocation().X, Buggy->GetActorLocation().Y,
					Buggy->GetActorLocation().Z);
				TWeakObjectPtr<ATN_Buggy> Weak(Buggy);
				for (const float Delay : { 0.1f, 1.f })
				{
					AfterGlobal(Delay, [Weak, Delay](UWorld*)
					{
						if (const ATN_Buggy* Alive = Weak.Get())
						{
							const FVector Now = Alive->GetActorLocation();
							UE_LOG(LogTNBuggy, Log, TEXT("[Humo] a los %.1f s: (%.0f, %.0f, %.0f)"), Delay, Now.X, Now.Y, Now.Z);
						}
					});
				}
			});
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdLocalFire(TEXT("TN.Rally.LocalFire"),
		TEXT("Rally: TN.Rally.LocalFire [especial 0|1] [espera] [veces = 1]: la jugadora local (artillera o conductora sola) pide disparos al servidor por su RPC, uno cada 0,5 s."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const bool bSpecial = FloatArg(Args, 0, 0.f) != 0.f;
			const float Wait = FloatArg(Args, 1, 0.f);
			const int32 Shots = FMath::Clamp(static_cast<int32>(FloatArg(Args, 2, 1.f)), 1, 20);
			for (int32 Shot = 0; Shot < Shots; ++Shot)
			{
				AfterGlobal(Wait + 0.5f * Shot, [bSpecial](UWorld* Alive) { LocalFire(Alive, bSpecial); });
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdLocalCall(TEXT("TN.Rally.LocalCall"),
		TEXT("Rally: TN.Rally.LocalCall [0 = nota, 1 = ¡Turbo ya!, 2 = ¡Frena!] [espera] [veces = 1]: la artillera local canta a la conductora (#330), una vez por segundo."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const int32 What = static_cast<int32>(FloatArg(Args, 0, 0.f));
			const float Wait = FloatArg(Args, 1, 0.f);
			const int32 Times = FMath::Clamp(static_cast<int32>(FloatArg(Args, 2, 1.f)), 1, 60);
			for (int32 Call = 0; Call < Times; ++Call)
			{
				AfterGlobal(Wait + static_cast<float>(Call), [What](UWorld* Alive)
				{
					const APlayerController* PC = Alive ? Alive->GetFirstPlayerController() : nullptr;
					ATN_BuggyGunnerPawn* Gunner = PC ? Cast<ATN_BuggyGunnerPawn>(PC->GetPawn()) : nullptr;
					if (!Gunner)
					{
						UE_LOG(LogTNBuggy, Warning, TEXT("[Humo] TN.Rally.LocalCall: la jugadora local no es artillera"));
						return;
					}
					const bool bSent = What == 0 ? Gunner->RequestCallNote()
						: Gunner->RequestQuickCall(What == 1 ? ETNRallyQuickCall::Boost : ETNRallyQuickCall::Brake);
					UE_LOG(LogTNBuggy, Log, TEXT("[Humo] la artillera local pide cantar %d: %s"), What, bSent ? TEXT("enviado") : TEXT("sin nota o en espera"));
				});
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdDebugSwapSeats(TEXT("TN.Rally.DebugSwapSeats"),
		TEXT("Rally (servidor): TN.Rally.DebugSwapSeats [espera]: en cada buggy con las dos plazas ocupadas, la artillera pasa a conducir y la conductora a la torreta (para probar la salida de una conductora cliente)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			AfterGlobal(FloatArg(Args, 0, 0.f), [](UWorld* Alive)
			{
				if (Alive->GetNetMode() == NM_Client)
				{
					UE_LOG(LogTNBuggy, Warning, TEXT("[Humo] TN.Rally.DebugSwapSeats: solo en el servidor"));
					return;
				}
				for (TActorIterator<ATN_Buggy> It(Alive); It; ++It)
				{
					AController* Driver = It->GetSeatController(ETNRallySeat::Driver);
					AController* Gunner = It->GetSeatController(ETNRallySeat::Gunner);
					if (!Driver || !Gunner)
					{
						continue;
					}
					// UnseatController pasa la artillera al volante; la antigua conductora ocupa la torreta.
					It->UnseatController(Driver);
					const bool bSeated = It->SeatController(Driver, ETNRallySeat::Gunner);
					UE_LOG(LogTNBuggy, Log, TEXT("[Humo] %s: conduce %s, artillera %s (%s)"), *It->GetName(),
						*GetNameSafe(It->GetSeatController(ETNRallySeat::Driver)), *GetNameSafe(It->GetSeatController(ETNRallySeat::Gunner)),
						bSeated ? TEXT("cambio hecho") : TEXT("la torreta no acepta a la antigua conductora"));
				}
			});
		}));

	/** Un encuadre de las fotos: guiñada respecto al morro (0 = de frente), distancia y altura de la cámara (cm). */
	struct FPhotoAngle
	{
		float YawDeg;
		float DistanceCm;
		float HeightCm;
	};

	/** Encuadres de la lámina del buggy: tres cuartos delante, lado, tres cuartos detrás, cabina desde arriba y frente. */
	const FPhotoAngle StillAngles[] = { { 35.f, 700.f, 260.f }, { 90.f, 650.f, 170.f }, { 145.f, 700.f, 260.f },
		{ 60.f, 380.f, 420.f }, { 0.f, 650.f, 150.f } };
	/** Persecución para los efectos: detrás y a un lado, a la altura del polvo. */
	const FPhotoAngle ChaseAngles[] = { { 150.f, 900.f, 250.f }, { 115.f, 750.f, 180.f } };

	/** Pone la cámara de fotos mirando al buggy (a la altura de los asientos) desde Angle. */
	void AimPhotoCamera(AActor* Camera, const ATN_Buggy* Buggy, const FPhotoAngle& Angle)
	{
		const FVector Center = Buggy->GetActorLocation() + Buggy->GetActorUpVector() * 100.f;
		const FRotator Around(0.f, Buggy->GetActorRotation().Yaw + Angle.YawDeg, 0.f);
		const FVector Eye = Center + Around.Vector() * Angle.DistanceCm + FVector(0.f, 0.f, Angle.HeightCm);
		Camera->SetActorLocationAndRotation(Eye, (Center - Eye).Rotation());
	}

	/**
	 * Fotos del buggy de la jugadora local (o el primero) con una cámara propia, sin interfaz, en Folder/buggy_<n>.png.
	 * Quieto: se frena el buggy (servidor) y se hacen los encuadres de la lámina; si no, persecución cada Interval s.
	 */
	void TakePhotos(UWorld* World, const FString& Folder, bool bStill, int32 Count, float Interval)
	{
		ATN_Buggy* Buggy = FindBuggy(World);
		APlayerController* PC = World->GetFirstPlayerController();
		if (!Buggy || !PC)
		{
			UE_LOG(LogTNBuggy, Warning, TEXT("[Fotos] sin buggy o sin jugadora local"));
			return;
		}
		if (bStill && Buggy->HasAuthority())
		{
			Buggy->SetEngineLocked(true);
			Buggy->SetRaceBrakeHeld(true);
		}
		ACameraActor* Camera = World->SpawnActor<ACameraActor>();
		if (!Camera)
		{
			return;
		}
		Camera->GetCameraComponent()->SetFieldOfView(50.f);
		const int32 Shots = bStill ? UE_ARRAY_COUNT(StillAngles) : FMath::Max(1, Count);
		TWeakObjectPtr<ATN_Buggy> WeakBuggy(Buggy);
		TWeakObjectPtr<ACameraActor> WeakCamera(Camera);
		TWeakObjectPtr<APlayerController> WeakPC(PC);
		for (int32 Shot = 0; Shot < Shots; ++Shot)
		{
			const FPhotoAngle Angle = bStill ? StillAngles[Shot] : ChaseAngles[Shot % UE_ARRAY_COUNT(ChaseAngles)];
			const float At = Interval * Shot;
			After(World, At, [WeakBuggy, WeakCamera, WeakPC, Angle](UWorld*)
			{
				if (WeakBuggy.IsValid() && WeakCamera.IsValid() && WeakPC.IsValid())
				{
					AimPhotoCamera(WeakCamera.Get(), WeakBuggy.Get(), Angle);
					WeakPC->SetViewTargetWithBlend(WeakCamera.Get(), 0.f);
				}
			});
			// La captura, un poco después: el suavizado temporal se asienta con la cámara quieta.
			const FString File = FPaths::Combine(Folder, FString::Printf(TEXT("buggy_%d.png"), Shot));
			After(World, At + FMath::Min(0.4f, Interval * 0.6f), [File, WeakBuggy, WeakCamera, Angle](UWorld*)
			{
				if (WeakBuggy.IsValid() && WeakCamera.IsValid())
				{
					AimPhotoCamera(WeakCamera.Get(), WeakBuggy.Get(), Angle);
				}
				FScreenshotRequest::RequestScreenshot(File, false, false);
				UE_LOG(LogTNBuggy, Log, TEXT("[Fotos] %s"), *File);
			});
		}
	}

	FAutoConsoleCommandWithWorldAndArgs CmdDebugPhotos(TEXT("TN.Rally.DebugPhotos"),
		TEXT("Rally: TN.Rally.DebugPhotos <espera> <carpeta> [quieto 1|0] [veces] [intervalo]: fotos del buggy sin interfaz (lámina con el buggy frenado, o persecución)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const FString Folder = Args.IsValidIndex(1) ? Args[1] : FPaths::ScreenShotDir();
			const bool bStill = FloatArg(Args, 2, 1.f) != 0.f;
			const int32 Count = FMath::Clamp(static_cast<int32>(FloatArg(Args, 3, 6.f)), 1, 60);
			const float Interval = FMath::Max(0.2f, FloatArg(Args, 4, 0.8f));
			AfterGlobal(FloatArg(Args, 0, 1.5f), [Folder, bStill, Count, Interval](UWorld* Alive)
			{
				TakePhotos(Alive, Folder, bStill, Count, Interval);
			});
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdViewShot(TEXT("TN.Rally.ViewShot"),
		TEXT("Rally: TN.Rally.ViewShot <espera> <fichero.png> <x> <y> <z> <pitch> <yaw> [fov = 60]: captura sin interfaz desde una cámara fija (uu y grados)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 7)
			{
				UE_LOG(LogTNBuggy, Warning, TEXT("[Fotos] uso: TN.Rally.ViewShot <espera> <fichero.png> <x> <y> <z> <pitch> <yaw> [fov]"));
				return;
			}
			const FString File = Args[1];
			const FVector Eye(FloatArg(Args, 2, 0.f), FloatArg(Args, 3, 0.f), FloatArg(Args, 4, 0.f));
			const FRotator Look(FloatArg(Args, 5, -90.f), FloatArg(Args, 6, 0.f), 0.f);
			const float Fov = FMath::Clamp(FloatArg(Args, 7, 60.f), 5.f, 170.f);
			AfterGlobal(FloatArg(Args, 0, 3.f), [File, Eye, Look, Fov](UWorld* Alive)
			{
				APlayerController* PC = Alive->GetFirstPlayerController();
				ACameraActor* Camera = PC ? Alive->SpawnActor<ACameraActor>(Eye, Look) : nullptr;
				if (!Camera)
				{
					UE_LOG(LogTNBuggy, Warning, TEXT("[Fotos] sin jugadora local para la cámara fija"));
					return;
				}
				Camera->GetCameraComponent()->SetFieldOfView(Fov);
				PC->SetViewTargetWithBlend(Camera, 0.f);
				// La captura, un segundo después: el suavizado temporal y la exposición se asientan con la cámara quieta.
				After(Alive, 1.f, [File](UWorld*)
				{
					FScreenshotRequest::RequestScreenshot(File, false, false);
					UE_LOG(LogTNBuggy, Log, TEXT("[Fotos] %s"), *File);
				});
			});
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdStatusLater(TEXT("TN.Rally.StatusLater"),
		TEXT("Rally: TN.Rally.StatusLater <espera> [veces = 1] [intervalo = 5]: TN.Rally.Status diferido (en un cliente, el estado replicado)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const float Wait = FloatArg(Args, 0, 10.f);
			const int32 Times = FMath::Clamp(static_cast<int32>(FloatArg(Args, 1, 1.f)), 1, 50);
			const float Interval = FMath::Max(0.5f, FloatArg(Args, 2, 5.f));
			for (int32 Index = 0; Index < Times; ++Index)
			{
				AfterGlobal(Wait + Interval * Index, [](UWorld* Alive)
				{
					const ATN_RallyGameState* RallyState = Alive->GetGameState<ATN_RallyGameState>();
					const APlayerController* PC = Alive->GetFirstPlayerController();
					UE_LOG(LogTNRally, Display, TEXT("[Estado %s] peón local %s\n%s"), Alive->GetNetMode() == NM_Client ? TEXT("cliente") : TEXT("servidor"),
						*GetNameSafe(PC ? PC->GetPawn() : nullptr), RallyState ? *RallyState->DescribeStatus() : TEXT("sin partida de Rally"));
				});
			}
		}));
}

#endif
