// Prueba de red del panzazo sin editor (#24, #62, #63): un servidor ?listen y un cliente -game con la misma opción
//
//   -TNDiveNetTest=<escena>_<veces>     escena: dive (panzazos en llano), slope (cuesta abajo de 25°) o wall (contra una pared)
//
// Los dos montan en el cielo (lejos del mapa) el mismo escenario con cajas de colisión, sin replicar e iguales en las dos
// máquinas. El servidor coloca allí la tortuga del cliente una vez. El cliente la mueve solo (como el jugador: entrada,
// salto, segundo salto para el panzazo) «veces» veces y deja marcas «[DiveNet]» en el registro: con p.NetShowCorrections 1
// en las dos máquinas y -PktLag=150 en el cliente, las líneas «*** Server: Error» y «*** Client: Error» entre las marcas son
// las correcciones de cada fase. Al acabar, los dos se cierran. Ver Docs/Comandos_Prueba.md, «Panzazo en red sin editor».
//
// Pone p.NetShowCorrections 1 (es de trampa: -dpcvars no la cambia). Para comparar con lo de antes, -TNDiveNetBefore en los
// dos: TN.Net.DivePredict 0, TN.Dive.SlopeFall 0 y TN.Dive.WallBounce 0.

#include "Core/TN_Log.h"
#include "Player/TN_TurtleMovementComponent.h"
#include "Player/TortugaCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#if !UE_BUILD_SHIPPING

namespace TNDiveNetTest
{
	enum class EScene : uint8
	{
		Dive,
		Slope,
		Wall,
	};

	/** El escenario, lejos de todo: 1 km por encima y a un lado del origen. */
	const FVector Origin(100000.0, 100000.0, 60000.0);

	/** Dónde empieza cada vuelta (centro de la cápsula, a ras del suelo) y hacia dónde mira. */
	FVector StartPoint(EScene Scene)
	{
		const double StandHalf = 90.0;
		switch (Scene)
		{
		case EScene::Slope: return Origin + FVector(-150.0, 0.0, 50.0 + StandHalf);
		default: return Origin + FVector(0.0, 0.0, 50.0 + StandHalf);
		}
	}

	/** Una caja con colisión (sin malla: en -nullrhi no hace falta verla), igual en las dos máquinas y sin replicar. */
	AActor* SpawnBlock(UWorld& World, const FVector& Center, const FRotator& Rotation, const FVector& SizeCm)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Block = World.SpawnActor<AActor>(AActor::StaticClass(), FTransform(Rotation, Center), Params);
		if (!Block)
		{
			return nullptr;
		}
		Block->SetReplicates(false);
		UBoxComponent* Box = NewObject<UBoxComponent>(Block, TEXT("Box"));
		Box->SetMobility(EComponentMobility::Static);
		Box->SetBoxExtent(SizeCm * 0.5, false);
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Block->SetRootComponent(Box);
		Box->SetWorldTransform(FTransform(Rotation, Center));
		Box->RegisterComponent();
		UE_LOG(LogTortunabo, Display, TEXT("[DiveNet] bloque %s en %s (%s), física %d"), *Block->GetName(),
			*(Center - Origin).ToCompactString(), *SizeCm.ToCompactString(),
			Box->GetBodyInstance() && Box->GetBodyInstance()->IsValidBodyInstance() ? 1 : 0);
		return Block;
	}

	/** El mismo escenario en el servidor y en el cliente. */
	void BuildScene(UWorld& World, EScene Scene)
	{
		switch (Scene)
		{
		case EScene::Dive:
			SpawnBlock(World, Origin, FRotator::ZeroRotator, FVector(4000.0, 4000.0, 100.0));
			break;
		case EScene::Wall:
			SpawnBlock(World, Origin, FRotator::ZeroRotator, FVector(4000.0, 4000.0, 100.0));
			// Cara de la pared a 2 m del inicio: el panzazo desde lo alto del salto llega en el aire (a unos 420 cm/s).
			SpawnBlock(World, Origin + FVector(225.0, 0.0, 300.0), FRotator::ZeroRotator, FVector(50.0, 1000.0, 500.0));
			break;
		case EScene::Slope:
		{
			// Arriba (x <= 0), rampa de 8 m a 25° bajando hacia +X y llano abajo.
			const double Angle = FMath::DegreesToRadians(25.0);
			const double TopZ = Origin.Z + 50.0;
			SpawnBlock(World, Origin + FVector(-500.0, 0.0, 0.0), FRotator::ZeroRotator, FVector(1000.0, 1000.0, 100.0));
			const FVector Forward(FMath::Cos(Angle), 0.0, -FMath::Sin(Angle));
			const FVector Up(FMath::Sin(Angle), 0.0, FMath::Cos(Angle));
			const FVector RampTopCenter = FVector(Origin.X, Origin.Y, TopZ) + Forward * 400.0;
			SpawnBlock(World, RampTopCenter - Up * 50.0, FRotator(-25.0, 0.0, 0.0), FVector(800.0, 1000.0, 100.0));
			const double EndX = Origin.X + 800.0 * FMath::Cos(Angle);
			const double EndZ = TopZ - 800.0 * FMath::Sin(Angle);
			SpawnBlock(World, FVector(EndX + 1000.0 - 5.0, Origin.Y, EndZ - 50.0), FRotator::ZeroRotator, FVector(2000.0, 1000.0, 100.0));
			break;
		}
		}
	}

	struct FRun
	{
		TWeakObjectPtr<UWorld> World;
		EScene Scene = EScene::Dive;
		int32 Times = 6;
		bool bBuilt = false;
		double StartedAt = -1.0;

		// Servidor
		bool bPlaced = false;
		double ClientSeenAt = -1.0;
		double ClientGoneAt = -1.0;

		// Cliente
		enum class EPhase : uint8 { WaitPawn, Settle, RunUp, Air, Flight, Recover, WalkBack, Done };
		EPhase Phase = EPhase::WaitPawn;
		double PhaseAt = 0.0;
		int32 Cycle = 0;
		bool bStoodSince = false;
		double StoodAt = 0.0;
		double LastStatusAt = -10.0;
	};

	TSharedPtr<FRun> Run;
	FTSTicker::FDelegateHandle TickHandle;

	ATortugaCharacter* RemoteTurtle(UWorld& World)
	{
		const AGameStateBase* GameState = World.GetGameState();
		if (!GameState)
		{
			return nullptr;
		}
		for (const APlayerState* PlayerState : GameState->PlayerArray)
		{
			ATortugaCharacter* Turtle = PlayerState ? Cast<ATortugaCharacter>(PlayerState->GetPawn()) : nullptr;
			if (Turtle && !Turtle->IsLocallyControlled())
			{
				return Turtle;
			}
		}
		return nullptr;
	}

	void Mark(const FRun& State, const TCHAR* What, const ATortugaCharacter* Turtle)
	{
		const APlayerState* PlayerState = Turtle ? Turtle->GetPlayerState() : nullptr;
		const FVector At = Turtle ? Turtle->GetActorLocation() - Origin : FVector::ZeroVector;
		UE_LOG(LogTortunabo, Display, TEXT("[DiveNet] ciclo %d: %s (ping %.0f ms, en %s, %.0f cm/s)"), State.Cycle, What,
			PlayerState ? PlayerState->GetPingInMilliseconds() : -1.f, *At.ToCompactString(), Turtle ? Turtle->GetVelocity().Size2D() : 0.0);
	}

	void SetPhase(FRun& State, FRun::EPhase Phase, double Now)
	{
		State.Phase = Phase;
		State.PhaseAt = Now;
	}

	void TickServer(FRun& State, UWorld& World, double Now)
	{
		ATortugaCharacter* Turtle = RemoteTurtle(World);
		if (Turtle && State.bPlaced && Now - State.LastStatusAt > 5.0)
		{
			State.LastStatusAt = Now;
			UE_LOG(LogTortunabo, Display, TEXT("[DiveNet] servidor: la del cliente a %s del inicio, modo %d, %.0f cm/s."),
				*(Turtle->GetActorLocation() - StartPoint(State.Scene)).ToCompactString(),
				static_cast<int32>(Turtle->GetCharacterMovement()->MovementMode.GetValue()), Turtle->GetVelocity().Size());
		}
		if (Turtle)
		{
			if (State.ClientSeenAt < 0.0)
			{
				State.ClientSeenAt = Now;
			}
			State.ClientGoneAt = -1.0;
			if (!State.bPlaced && Now - State.ClientSeenAt > 2.0)
			{
				State.bPlaced = true;
				UCharacterMovementComponent* Move = Turtle->GetCharacterMovement();
				Move->StopMovementImmediately();
				Turtle->SetActorLocationAndRotation(StartPoint(State.Scene) + FVector(0.0, 0.0, 30.0), FRotator::ZeroRotator, false, nullptr,
					ETeleportType::TeleportPhysics);
				Move->SetMovementMode(MOVE_Falling);
				UE_LOG(LogTortunabo, Display, TEXT("[DiveNet] servidor: coloca a %s en el escenario."), *Turtle->GetName());
			}
		}
		else if (State.bPlaced)
		{
			if (State.ClientGoneAt < 0.0)
			{
				State.ClientGoneAt = Now;
			}
			if (Now - State.ClientGoneAt > 3.0)
			{
				UE_LOG(LogTortunabo, Display, TEXT("[DiveNet] servidor: el cliente se ha ido; fin."));
				SetPhase(State, FRun::EPhase::Done, Now);
				FPlatformMisc::RequestExit(false, TEXT("TNDiveNetTest"));
			}
		}
	}

	/** Mirando hacia Yaw: el panzazo sin entrada va hacia la cámara. */
	void Face(ATortugaCharacter& Turtle, float Yaw)
	{
		if (AController* Controller = Turtle.GetController())
		{
			Controller->SetControlRotation(FRotator(0.f, Yaw, 0.f));
		}
	}

	float CycleYaw(const FRun& State)
	{
		return (State.Scene == EScene::Dive && State.Cycle % 2 == 1) ? 180.f : 0.f;
	}

	void TickClient(FRun& State, UWorld& World, double Now)
	{
		APlayerController* PC = World.GetFirstPlayerController();
		ATortugaCharacter* Turtle = PC ? Cast<ATortugaCharacter>(PC->GetPawn()) : nullptr;
		if (!Turtle)
		{
			return;
		}
		UCharacterMovementComponent* Move = Turtle->GetCharacterMovement();
		const double Elapsed = Now - State.PhaseAt;
		const bool bGrounded = Move->IsMovingOnGround();
		const bool bStill = bGrounded && !Turtle->IsBellyPoseActive() && Turtle->GetVelocity().Size2D() < 40.0;
		const FVector Start = StartPoint(State.Scene);

		switch (State.Phase)
		{
		case FRun::EPhase::WaitPawn:
			if (Now - State.LastStatusAt > 2.0)
			{
				State.LastStatusAt = Now;
				UE_LOG(LogTortunabo, Display, TEXT("[DiveNet] cliente: esperando (a %s del inicio, modo %d, %.0f cm/s, tumbada %d)"),
					*(Turtle->GetActorLocation() - Start).ToCompactString(), static_cast<int32>(Move->MovementMode.GetValue()), Turtle->GetVelocity().Size(),
					Turtle->IsBellyPoseActive() ? 1 : 0);
			}
			// Hasta que el servidor la coloque en el escenario y esté quieta.
			if (FVector::Dist2D(Turtle->GetActorLocation(), Start) < 300.0 && bStill && FMath::Abs(Turtle->GetActorLocation().Z - Start.Z) < 60.0)
			{
				Mark(State, TEXT("en el escenario"), Turtle);
				SetPhase(State, FRun::EPhase::Settle, Now);
			}
			break;

		case FRun::EPhase::Settle:
			Face(*Turtle, CycleYaw(State));
			if (Elapsed > 0.8 && bStill)
			{
				++State.Cycle;
				if (State.Cycle > State.Times)
				{
					Mark(State, TEXT("fin"), Turtle);
					SetPhase(State, FRun::EPhase::Done, Now);
					break;
				}
				Mark(State, TEXT("empieza"), Turtle);
				SetPhase(State, FRun::EPhase::RunUp, Now);
			}
			break;

		case FRun::EPhase::RunUp:
		{
			// Carrerilla (en llano y en la cuesta) y salto.
			const float Yaw = CycleYaw(State);
			Face(*Turtle, Yaw);
			const double RunSeconds = State.Scene == EScene::Wall ? 0.0 : 0.35;
			if (Elapsed < RunSeconds)
			{
				Turtle->AddMovementInput(FRotator(0.f, Yaw, 0.f).Vector(), 1.f);
				break;
			}
			if (RunSeconds > 0.0)
			{
				Turtle->AddMovementInput(FRotator(0.f, Yaw, 0.f).Vector(), 1.f);
			}
			// Como la tecla de saltar (ATortugaCharacter::Jump, protegida: por ACharacter).
			static_cast<ACharacter*>(Turtle)->Jump();
			Mark(State, TEXT("salta"), Turtle);
			SetPhase(State, FRun::EPhase::Air, Now);
			break;
		}

		case FRun::EPhase::Air:
			Turtle->StopJumping();
			// En lo alto del salto (o a los 0,25 s), el segundo salto: el panzazo hacia donde mira.
			if (Move->IsFalling() && (Turtle->GetVelocity().Z <= 60.0 || Elapsed > 0.25))
			{
				static_cast<ACharacter*>(Turtle)->Jump();
				Turtle->StopJumping();
				Mark(State, TEXT("panzazo pedido"), Turtle);
				SetPhase(State, FRun::EPhase::Flight, Now);
			}
			else if (Elapsed > 1.5)
			{
				Mark(State, TEXT("sin salto"), Turtle);
				SetPhase(State, FRun::EPhase::Settle, Now);
			}
			break;

		case FRun::EPhase::Flight:
			if (bGrounded)
			{
				Mark(State, TEXT("en el suelo"), Turtle);
				State.bStoodSince = false;
				SetPhase(State, FRun::EPhase::Recover, Now);
			}
			else if (Elapsed > 4.0)
			{
				Mark(State, TEXT("sin caer"), Turtle);
				SetPhase(State, FRun::EPhase::Recover, Now);
			}
			break;

		case FRun::EPhase::Recover:
			// De pie y quieta un rato (o tope).
			if (bStill && !Turtle->IsDiving())
			{
				if (!State.bStoodSince)
				{
					State.bStoodSince = true;
					State.StoodAt = Now;
					Mark(State, TEXT("de pie"), Turtle);
				}
				if (Now - State.StoodAt > 0.4)
				{
					SetPhase(State, State.Scene == EScene::Dive ? FRun::EPhase::Settle : FRun::EPhase::WalkBack, Now);
					if (State.Phase == FRun::EPhase::WalkBack)
					{
						Mark(State, TEXT("vuelve"), Turtle);
					}
				}
			}
			else if (Elapsed > 8.0)
			{
				Mark(State, TEXT("sin levantarse"), Turtle);
				SetPhase(State, FRun::EPhase::Settle, Now);
			}
			break;

		case FRun::EPhase::WalkBack:
		{
			const FVector ToStart = FVector(Start.X, Start.Y, 0.0) - FVector(Turtle->GetActorLocation().X, Turtle->GetActorLocation().Y, 0.0);
			if (ToStart.Size() < 35.0 || Elapsed > 10.0)
			{
				Mark(State, TEXT("en el inicio"), Turtle);
				SetPhase(State, FRun::EPhase::Settle, Now);
				break;
			}
			Face(*Turtle, ToStart.Rotation().Yaw);
			Turtle->AddMovementInput(ToStart.GetSafeNormal(), ToStart.Size() < 120.0 ? 0.4f : 1.f);
			break;
		}

		case FRun::EPhase::Done:
			if (Elapsed > 1.5)
			{
				FPlatformMisc::RequestExit(false, TEXT("TNDiveNetTest"));
			}
			break;
		}
	}

	bool Tick(float)
	{
		if (!Run.IsValid())
		{
			return false;
		}
		FRun& State = *Run;
		UWorld* World = State.World.Get();
		// Solo el mapa de juego en red (no el menú del cliente antes de conectar ni el mapa de paso).
		if (!World || !World->HasBegunPlay() || World->GetNetMode() == NM_Standalone)
		{
			return true;
		}
		const double Now = World->GetTimeSeconds();
		if (!State.bBuilt)
		{
			State.bBuilt = true;
			// Las correcciones al registro («*** Client: Error» / «*** Server: Error») y, para comparar, lo de antes.
			auto SetCVar = [](const TCHAR* Name, int32 Value)
			{
				if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
				{
					CVar->Set(Value, ECVF_SetByCode);
				}
			};
			SetCVar(TEXT("p.NetShowCorrections"), 1);
			// Otras variables para aislar algo: -TNDiveNetSet=TN.Dive.Body:0+TN.Dive.WallBounce:0
			FString Extra;
			if (FParse::Value(FCommandLine::Get(), TEXT("TNDiveNetSet="), Extra))
			{
				TArray<FString> Pairs;
				Extra.ParseIntoArray(Pairs, TEXT("+"));
				for (const FString& Pair : Pairs)
				{
					FString Name;
					FString Value;
					if (Pair.Split(TEXT(":"), &Name, &Value))
					{
						if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Name))
						{
							CVar->Set(*Value, ECVF_SetByCode);
						}
						UE_LOG(LogTortunabo, Display, TEXT("[DiveNet] %s = %s"), *Name, *Value);
					}
				}
			}
			if (FParse::Param(FCommandLine::Get(), TEXT("TNDiveNetBefore")))
			{
				SetCVar(TEXT("TN.Net.DivePredict"), 0);
				SetCVar(TEXT("TN.Dive.SlopeFall"), 0);
				SetCVar(TEXT("TN.Dive.WallBounce"), 0);
				UE_LOG(LogTortunabo, Display, TEXT("[DiveNet] como antes: TN.Net.DivePredict 0, TN.Dive.SlopeFall 0, TN.Dive.WallBounce 0."));
			}
			State.StartedAt = Now;
			BuildScene(*World, State.Scene);
			FHitResult Hit;
			const FVector Probe = StartPoint(State.Scene);
			const bool bHit = World->LineTraceSingleByChannel(Hit, Probe + FVector(0.0, 0.0, 200.0), Probe - FVector(0.0, 0.0, 500.0), ECC_Pawn);
			UE_LOG(LogTortunabo, Display, TEXT("[DiveNet] suelo bajo el inicio: %s (%s)"), bHit ? *(Hit.ImpactPoint - Origin).ToCompactString() : TEXT("nada"),
				bHit ? *GetNameSafe(Hit.GetActor()) : TEXT("-"));
			UE_LOG(LogTortunabo, Display, TEXT("[DiveNet] %s: escenario montado (%s, %d veces)."),
				World->GetNetMode() == NM_Client ? TEXT("cliente") : TEXT("servidor"),
				State.Scene == EScene::Dive ? TEXT("dive") : State.Scene == EScene::Slope ? TEXT("slope") : TEXT("wall"), State.Times);
		}
		if (World->GetNetMode() == NM_Client)
		{
			TickClient(State, *World, Now);
		}
		else
		{
			TickServer(State, *World, Now);
			// Tope por si el cliente no llega.
			if (Now - State.StartedAt > 120.0 + State.Times * 12.0)
			{
				UE_LOG(LogTortunabo, Warning, TEXT("[DiveNet] servidor: tope de tiempo; fin."));
				FPlatformMisc::RequestExit(false, TEXT("TNDiveNetTest"));
				return false;
			}
		}
		return true;
	}

	void OnWorldInit(UWorld* World, const UWorld::InitializationValues)
	{
		FString Value;
		if (!World || !World->IsGameWorld() || !FParse::Value(FCommandLine::Get(), TEXT("TNDiveNetTest="), Value))
		{
			return;
		}
		TArray<FString> Parts;
		Value.ParseIntoArray(Parts, TEXT("_"));
		TSharedPtr<FRun> NewRun = MakeShared<FRun>();
		NewRun->World = World;
		const FString SceneName = Parts.Num() > 0 ? Parts[0] : FString(TEXT("dive"));
		NewRun->Scene = SceneName.Equals(TEXT("slope"), ESearchCase::IgnoreCase) ? EScene::Slope
			: SceneName.Equals(TEXT("wall"), ESearchCase::IgnoreCase) ? EScene::Wall : EScene::Dive;
		NewRun->Times = Parts.Num() > 1 ? FMath::Clamp(FCString::Atoi(*Parts[1]), 1, 50) : 6;
		Run = NewRun;
		if (!TickHandle.IsValid())
		{
			TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick));
		}
	}

	struct FHook
	{
		FHook()
		{
			FWorldDelegates::OnPostWorldInitialization.AddStatic(&OnWorldInit);
		}
	};

	FHook Hook;
}

#endif // !UE_BUILD_SHIPPING
