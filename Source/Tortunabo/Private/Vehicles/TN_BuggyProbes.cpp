// Comprobaciones del buggy en un mundo de juego (fuera de Shipping, servidor o standalone), para PIE o -game:
//   TN.Rally.MeasureTurn [km/h = 20] [cerrar 0|1]
//     Radio de giro (#606): toma el buggy del primer jugador (o el primero del mundo), aparta a su piloto IA, acelera
//     siguiendo la pista hasta la velocidad pedida y gira a tope a la derecha manteniéndola; el radio sale de la velocidad y
//     la guiñada (TNBuggy::TurnRadiusFromYawRate), de media entre 0,5 y 1,5 s de giro. Resultado: LogTNBuggy "[Giro] ...".
//   TN.Rally.RampHold [grados = 15] [cerrar 0|1]
//     Parrilla en cuesta (#611): crea una rampa y un buggy apoyado en ella con el motor cortado y el freno de carrera (como
//     en la espera y la cuenta atrás), mide cuánto se desplaza por la rampa en 5 s (objetivo: < 5 cm; aparte, lo que sube y
//     baja la suspensión) y, de control, cuánto rueda en 2 s al soltarlo. Resultado: LogTNBuggy "[Rampa] ...". Lejos de la
//     pista (a 2 km de altura) para no tocar nada. Sin ventana: Tortunabo.Rally.Measure.RampHold.

#include "Vehicles/TN_Buggy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Rally/TN_RallyGameState.h"
#include "Rally/TN_RallyLogic.h"
#include "Rally/TN_RallyTrack.h"
#include "TimerManager.h"
#include "Vehicles/TN_BuggyMath.h"

#if !UE_BUILD_SHIPPING

namespace TNBuggyProbes
{
	constexpr float ProbeTickSeconds = 1.f / 60.f;
	constexpr float ProbeWaitTimeoutSeconds = 90.f;
	constexpr double ProbeLookAheadCm = 1200.0;
	constexpr float ProbeSteerSaturationDeg = 35.f;

	ATN_Buggy* PickProbeBuggy(UWorld& World)
	{
		if (const APlayerController* PC = World.GetFirstPlayerController())
		{
			if (ATN_Buggy* Mine = Cast<ATN_Buggy>(PC->GetPawn()))
			{
				return Mine;
			}
		}
		for (TActorIterator<ATN_Buggy> It(&World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	// ── TN.Rally.MeasureTurn ────────────────────────────────────────────────────

	enum class ETurnStage : uint8 { Wait, Accelerate, Turn, Done };

	struct FTurnRun
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<ATN_Buggy> Buggy;
		TWeakObjectPtr<ATN_RallyTrack> Track;
		TWeakObjectPtr<AController> SuspendedPilot;
		FTimerHandle Timer;
		ETurnStage Stage = ETurnStage::Wait;
		float TargetKmh = 20.f;
		bool bQuitWhenDone = false;
		double StageStart = 0.0;
		double Arc = 0.0;
		double RadiusSum = 0.0;
		double SpeedSum = 0.0;
		int32 Samples = 0;
	};

	/** Por valor: ClearTimer destruye la lambda que era la única dueña de Run. */
	void FinishTurn(TSharedRef<FTurnRun> Run, const TCHAR* Why)
	{
		Run->Stage = ETurnStage::Done;
		if (UWorld* World = Run->World.Get())
		{
			World->GetTimerManager().ClearTimer(Run->Timer);
		}
		if (ATN_Buggy* Buggy = Run->Buggy.Get())
		{
			Buggy->SetAIDriveInput(0.f, 1.f, 0.f, false);
		}
		if (AController* Pilot = Run->SuspendedPilot.Get())
		{
			Pilot->SetActorTickEnabled(true);
		}
		const double Radius = Run->Samples > 0 ? Run->RadiusSum / Run->Samples : 0.0;
		const double Kmh = Run->Samples > 0 ? TNRally::CmsToKmh(Run->SpeedSum / Run->Samples) : 0.0;
		UE_LOG(LogTNBuggy, Display, TEXT("[Giro] %s: radio %.2f m a %.1f km/h (%d muestras; objetivo < 6 m a 20 km/h)"), Why,
			Radius / 100.0, Kmh, Run->Samples);
		if (Run->bQuitWhenDone)
		{
			FPlatformMisc::RequestExit(false, TEXT("TN.Rally.MeasureTurn"));
		}
	}

	void StartTurnRun(TSharedRef<FTurnRun> Run, UWorld& World, double Now)
	{
		ATN_Buggy* Candidate = PickProbeBuggy(World);
		const ATN_RallyGameState* RallyState = World.GetGameState<ATN_RallyGameState>();
		ATN_RallyTrack* Track = RallyState ? RallyState->GetTrack() : nullptr;
		const bool bRacing = !RallyState || RallyState->Phase == ETNRallyPhase::Racing || RallyState->Phase == ETNRallyPhase::Finishing;
		if (!Candidate || !Candidate->HasAuthority() || !Track || !Track->IsBuilt() || !bRacing)
		{
			if (Now - Run->StageStart > ProbeWaitTimeoutSeconds)
			{
				FinishTurn(Run, TEXT("sin buggy o sin carrera en marcha"));
			}
			return;
		}
		Run->Buggy = Candidate;
		Run->Track = Track;
		Run->Arc = Track->FindArcGlobal(Candidate->GetActorLocation());
		if (AController* Pilot = Candidate->GetController(); Pilot && !Pilot->IsA<APlayerController>())
		{
			Pilot->SetActorTickEnabled(false);
			Run->SuspendedPilot = Pilot;
		}
		Candidate->SetEngineLocked(false);
		Run->Stage = ETurnStage::Accelerate;
		Run->StageStart = Now;
	}

	void StepTurn(TSharedRef<FTurnRun> Run)
	{
		UWorld* World = Run->World.Get();
		if (!World || Run->Stage == ETurnStage::Done)
		{
			return;
		}
		const double Now = World->GetTimeSeconds();
		if (Run->Stage == ETurnStage::Wait)
		{
			StartTurnRun(Run, *World, Now);
			return;
		}
		ATN_Buggy* Buggy = Run->Buggy.Get();
		ATN_RallyTrack* Track = Run->Track.Get();
		if (!Buggy || !Track)
		{
			FinishTurn(Run, TEXT("el buggy o la pista han desaparecido"));
			return;
		}
		const float Kmh = static_cast<float>(TNRally::CmsToKmh(Buggy->GetForwardSpeedCms()));
		const double Elapsed = Now - Run->StageStart;
		if (Run->Stage == ETurnStage::Accelerate)
		{
			const FVector Location = Buggy->GetActorLocation();
			Run->Arc = Track->FindArcNear(Location, Run->Arc);
			const float Steer = TNRally::SteerToward(Buggy->GetActorForwardVector(),
				Track->GetLocationAtArc(Run->Arc + ProbeLookAheadCm) - Location, ProbeSteerSaturationDeg);
			Buggy->SetAIDriveInput(Kmh < Run->TargetKmh ? 1.f : 0.f, 0.f, Steer, false);
			if (Kmh >= Run->TargetKmh)
			{
				Run->Stage = ETurnStage::Turn;
				Run->StageStart = Now;
			}
			else if (Elapsed > 20.0)
			{
				FinishTurn(Run, TEXT("no llega a la velocidad pedida"));
			}
			return;
		}
		// Volante a tope a la derecha y la velocidad sostenida con el acelerador.
		Buggy->SetAIDriveInput(Kmh < Run->TargetKmh ? 0.7f : 0.f, Kmh > Run->TargetKmh + 3.f ? 0.3f : 0.f, 1.f, false);
		if (Elapsed >= 0.5 && Elapsed <= 1.5)
		{
			const USkeletalMeshComponent* Chassis = Buggy->GetMesh();
			const FVector Velocity = Buggy->GetVelocity();
			const float Flat = static_cast<float>(FVector(Velocity.X, Velocity.Y, 0.0).Size());
			const float YawRate = static_cast<float>(Chassis->GetPhysicsAngularVelocityInRadians() | Buggy->GetActorUpVector());
			Run->RadiusSum += TNBuggy::TurnRadiusFromYawRate(Flat, YawRate);
			Run->SpeedSum += Flat;
			++Run->Samples;
		}
		if (Elapsed > 1.5 || Buggy->IsFlipped())
		{
			FinishTurn(Run, Buggy->IsFlipped() ? TEXT("volcado girando") : TEXT("hecho"));
		}
	}

	void StartMeasureTurn(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogTNBuggy, Warning, TEXT("[Giro] TN.Rally.MeasureTurn: solo en el servidor"));
			return;
		}
		TSharedRef<FTurnRun> Run = MakeShared<FTurnRun>();
		Run->World = World;
		Run->TargetKmh = Args.IsValidIndex(0) ? FMath::Clamp(FCString::Atof(*Args[0]), 5.f, 120.f) : 20.f;
		Run->bQuitWhenDone = Args.IsValidIndex(1) && FCString::Atoi(*Args[1]) != 0;
		Run->StageStart = World->GetTimeSeconds();
		World->GetTimerManager().SetTimer(Run->Timer, FTimerDelegate::CreateLambda([Run]() { StepTurn(Run); }), ProbeTickSeconds, true);
		UE_LOG(LogTNBuggy, Log, TEXT("[Giro] esperando a la carrera (giro a %.0f km/h)"), Run->TargetKmh);
	}

	FAutoConsoleCommandWithWorldAndArgs CmdMeasureTurn(TEXT("TN.Rally.MeasureTurn"),
		TEXT("Rally (servidor): TN.Rally.MeasureTurn [km/h = 20] [cerrar 0|1]: radio de giro del buggy con el volante a tope (LogTNBuggy [Giro])."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { StartMeasureTurn(Args, World); }));

	// ── TN.Rally.RampHold ───────────────────────────────────────────────────────

	constexpr double RampLengthCm = 3000.0;
	constexpr double RampWidthCm = 1200.0;
	constexpr double RampThicknessCm = 100.0;
	constexpr double RampHeightCm = 200000.0;
	/** La suspensión (amortiguación 0,25) tarda unos 3 s en asentarse tras la caída: con 1,5 s su rebote contaba como deriva. */
	constexpr double RampSettleSeconds = 3.0;
	constexpr double RampHoldSeconds = 5.0;
	constexpr double RampReleaseSeconds = 2.0;
	constexpr double RampMaxDriftCm = 5.0;

	struct FRampRun
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<ATN_Buggy> Buggy;
		TWeakObjectPtr<AStaticMeshActor> Ramp;
		FTimerHandle Timer;
		double Start = 0.0;
		bool bQuitWhenDone = false;
		bool bMeasuring = false;
		bool bReleased = false;
		FVector HoldFrom = FVector::ZeroVector;
		/** Normal de la rampa: la deriva se mide en su plano (rodar), sin el sube y baja de la suspensión. */
		FVector RampUp = FVector::UpVector;
		double HeldDriftCm = -1.0;
		double HeldNormalCm = 0.0;
	};

	/** Desplazamiento en el plano de la rampa (cm). */
	double InPlaneCm(const FVector& Delta, const FVector& Up)
	{
		return (Delta - Up * (Delta | Up)).Size();
	}

	void FinishRamp(TSharedRef<FRampRun> Run, double RolledCm)
	{
		if (UWorld* World = Run->World.Get())
		{
			World->GetTimerManager().ClearTimer(Run->Timer);
		}
		const bool bPass = Run->HeldDriftCm >= 0.0 && Run->HeldDriftCm < RampMaxDriftCm;
		UE_LOG(LogTNBuggy, Display,
			TEXT("[Rampa] %s: frenado se desplaza %.2f cm por la rampa en %.0f s (objetivo < %.0f; la suspensión, %.2f cm en la normal); suelto rueda %.0f cm en %.0f s"),
			bPass ? TEXT("BIEN") : TEXT("FALLA"), Run->HeldDriftCm, RampHoldSeconds, RampMaxDriftCm, Run->HeldNormalCm, RolledCm, RampReleaseSeconds);
		if (ATN_Buggy* Buggy = Run->Buggy.Get())
		{
			Buggy->Destroy();
		}
		if (AStaticMeshActor* Ramp = Run->Ramp.Get())
		{
			Ramp->Destroy();
		}
		if (Run->bQuitWhenDone)
		{
			FPlatformMisc::RequestExit(false, TEXT("TN.Rally.RampHold"));
		}
	}

	void StepRamp(TSharedRef<FRampRun> Run)
	{
		UWorld* World = Run->World.Get();
		ATN_Buggy* Buggy = Run->Buggy.Get();
		if (!World || !Buggy)
		{
			UE_LOG(LogTNBuggy, Warning, TEXT("[Rampa] el buggy o el mundo han desaparecido"));
			if (World)
			{
				World->GetTimerManager().ClearTimer(Run->Timer);
			}
			return;
		}
		const double Elapsed = World->GetTimeSeconds() - Run->Start;
		const FVector Location = Buggy->GetActorLocation();
		if (!Run->bMeasuring && Elapsed >= RampSettleSeconds)
		{
			Run->bMeasuring = true;
			Run->HoldFrom = Location;
		}
		if (Run->bMeasuring && !Run->bReleased && Elapsed >= RampSettleSeconds + RampHoldSeconds)
		{
			Run->HeldDriftCm = InPlaneCm(Location - Run->HoldFrom, Run->RampUp);
			Run->HeldNormalCm = FMath::Abs((Location - Run->HoldFrom) | Run->RampUp);
			Run->bReleased = true;
			Run->HoldFrom = Location;
			// Como el verde de StartRacing: fuera el freno de carrera (el motor sigue cortado: solo rueda por la cuesta). Parado y sin
			// acelerador en una cuesta de menos de 30°, el reposo de Chaos (SleepSlopeLimit) lo duerme y no rueda: en carrera lo
			// despierta el acelerador.
			Buggy->SetRaceBrakeHeld(false);
		}
		if (Run->bReleased && Elapsed >= RampSettleSeconds + RampHoldSeconds + RampReleaseSeconds)
		{
			FinishRamp(Run, InPlaneCm(Location - Run->HoldFrom, Run->RampUp));
		}
	}

	void StartRampHold(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogTNBuggy, Warning, TEXT("[Rampa] TN.Rally.RampHold: solo en el servidor"));
			return;
		}
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Cube)
		{
			UE_LOG(LogTNBuggy, Warning, TEXT("[Rampa] no carga /Engine/BasicShapes/Cube"));
			return;
		}
		const double SlopeDeg = Args.IsValidIndex(0) ? FMath::Clamp(FCString::Atod(*Args[0]), 0.0, 35.0) : 15.0;
		// La rampa sube hacia +X; el buggy, apoyado en ella y mirando cuesta arriba, rodaría hacia atrás.
		const FRotator Slope(SlopeDeg, 0.0, 0.0);
		const FVector Center(0.0, 0.0, RampHeightCm);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Ramp = World->SpawnActor<AStaticMeshActor>(Center, Slope, Params);
		if (!Ramp)
		{
			return;
		}
		UStaticMeshComponent* RampMesh = Ramp->GetStaticMeshComponent();
		RampMesh->SetMobility(EComponentMobility::Movable);
		RampMesh->SetStaticMesh(Cube);
		// El cubo básico mide 100 cm de lado.
		RampMesh->SetWorldScale3D(FVector(RampLengthCm, RampWidthCm, RampThicknessCm) / 100.0);
		RampMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);

		const FVector Up = Slope.RotateVector(FVector::UpVector);
		const FVector BuggyAt = Center + Up * (0.5 * RampThicknessCm + 120.0);
		ATN_Buggy* Buggy = World->SpawnActor<ATN_Buggy>(ATN_Buggy::StaticClass(), BuggyAt, Slope, Params);
		if (!Buggy)
		{
			Ramp->Destroy();
			return;
		}
		// La espera y la cuenta atrás: motor cortado y freno de carrera (ATN_RallyGameMode::HoldBuggiesOnGrid).
		Buggy->SetEngineLocked(true);
		Buggy->SetRaceBrakeHeld(true);

		TSharedRef<FRampRun> Run = MakeShared<FRampRun>();
		Run->World = World;
		Run->Buggy = Buggy;
		Run->Ramp = Ramp;
		Run->RampUp = Up;
		Run->Start = World->GetTimeSeconds();
		Run->bQuitWhenDone = Args.IsValidIndex(1) && FCString::Atoi(*Args[1]) != 0;
		World->GetTimerManager().SetTimer(Run->Timer, FTimerDelegate::CreateLambda([Run]() { StepRamp(Run); }), ProbeTickSeconds, true);
		UE_LOG(LogTNBuggy, Log, TEXT("[Rampa] buggy frenado en una rampa de %.0f grados"), SlopeDeg);
	}

	FAutoConsoleCommandWithWorldAndArgs CmdRampHold(TEXT("TN.Rally.RampHold"),
		TEXT("Rally (servidor): TN.Rally.RampHold [grados = 15] [cerrar 0|1]: buggy con el freno de la parrilla en una rampa; cuánto se desplaza en 5 s (LogTNBuggy [Rampa])."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { StartRampHold(Args, World); }));
}

#endif // !UE_BUILD_SHIPPING
