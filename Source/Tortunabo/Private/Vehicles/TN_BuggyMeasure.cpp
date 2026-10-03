// TN.Rally.Measure (fuera de Shipping, servidor): mide el buggy en la pista del Rally (#100).
//   TN.Rally.Measure [segundos de aceleración = 25] [cerrar al acabar 0|1]
// Espera a que la carrera esté en marcha (o, sin carrera, a que haya un buggy y una pista construida), toma el buggy del
// primer jugador o el primero del mundo, aparta a su piloto IA y:
//   1. lo para;
//   2. acelera a fondo siguiendo la spline de la pista: tiempos de 0 a 60 y de 0 a 100 km/h y velocidad máxima;
//   3. frena a fondo: distancia desde que baja de 60 km/h hasta que se para.
// Resultado en LogTNBuggy: "[Medida] ...". Con un buggy de piloto IA: LVL_Rally?Bots=1?AutoStart en un servidor sin
// jugadoras.

#include "Vehicles/TN_Buggy.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Rally/TN_RallyGameState.h"
#include "Rally/TN_RallyLogic.h"
#include "Rally/TN_RallyTrack.h"
#include "TimerManager.h"

#if !UE_BUILD_SHIPPING

namespace TNBuggyMeasure
{
	constexpr float TickSeconds = 1.f / 60.f;
	constexpr float WaitTimeoutSeconds = 90.f;
	constexpr float StopKmh = 0.5f;
	constexpr float SettleTimeoutSeconds = 6.f;
	constexpr float BrakeTimeoutSeconds = 10.f;
	constexpr float BrakeFromKmh = 60.f;
	constexpr float SprintToKmh = 100.f;
	/** Marca intermedia de la salida (#294: 0-60 km/h en ~2 s). */
	constexpr float SprintMarkKmh = 60.f;
	/** Mirada adelantada para seguir la spline (como el piloto IA). */
	constexpr double LookAheadBaseCm = 1200.0;
	constexpr double LookAheadSeconds = 0.6;
	constexpr float SteerSaturationDeg = 35.f;

	enum class EStage : uint8 { Wait, Settle, Sprint, Brake, Done };

	struct FRun
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<ATN_Buggy> Buggy;
		TWeakObjectPtr<ATN_RallyTrack> Track;
		TWeakObjectPtr<AController> SuspendedPilot;
		FTimerHandle Timer;
		EStage Stage = EStage::Wait;
		float SprintSeconds = 25.f;
		bool bQuitWhenDone = false;
		double StageStart = 0.0;
		double Arc = 0.0;
		float MaxKmh = 0.f;
		float ZeroToSixty = -1.f;
		float ZeroToHundred = -1.f;
		FVector BrakeFrom = FVector::ZeroVector;
		bool bBrakeArmed = false;
		float BrakeMeters = -1.f;
		int32 Flips = 0;
		bool bWasFlipped = false;
	};

	ATN_Buggy* PickBuggy(UWorld& World)
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

	ATN_RallyTrack* PickTrack(UWorld& World)
	{
		if (const ATN_RallyGameState* RallyState = World.GetGameState<ATN_RallyGameState>())
		{
			return RallyState->GetTrack();
		}
		for (TActorIterator<ATN_RallyTrack> It(&World); It; ++It)
		{
			return It->IsBuilt() ? *It : nullptr;
		}
		return nullptr;
	}

	bool IsReady(UWorld& World)
	{
		const ATN_RallyGameState* RallyState = World.GetGameState<ATN_RallyGameState>();
		return !RallyState || RallyState->Phase == ETNRallyPhase::Racing || RallyState->Phase == ETNRallyPhase::Finishing;
	}

	/** Sigue la spline: devuelve el giro hacia el punto adelantado. */
	float SteerAlongTrack(FRun& Run, ATN_Buggy& Buggy, ATN_RallyTrack& Track)
	{
		const FVector Location = Buggy.GetActorLocation();
		Run.Arc = Track.FindArcNear(Location, Run.Arc);
		const double Ahead = LookAheadBaseCm + FMath::Abs(Buggy.GetForwardSpeedCms()) * LookAheadSeconds;
		return TNRally::SteerToward(Buggy.GetActorForwardVector(), Track.GetLocationAtArc(Run.Arc + Ahead) - Location, SteerSaturationDeg);
	}

	/** Recibe el TSharedRef por valor: ClearTimer destruye la lambda que era su única dueña (uso tras liberar, #100). */
	void Finish(TSharedRef<FRun> Run, const TCHAR* Why)
	{
		Run->Stage = EStage::Done;
		if (UWorld* World = Run->World.Get())
		{
			World->GetTimerManager().ClearTimer(Run->Timer);
		}
		if (AController* Pilot = Run->SuspendedPilot.Get())
		{
			Pilot->SetActorTickEnabled(true);
		}
		const auto SprintTime = [&Run](float Seconds)
		{
			return Seconds >= 0.f ? FString::Printf(TEXT("%.2f s"), Seconds) : FString::Printf(TEXT("no llega en %.0f s"), Run->SprintSeconds);
		};
		const FString Brake = Run->BrakeMeters >= 0.f ? FString::Printf(TEXT("%.1f m"), Run->BrakeMeters) : FString(TEXT("sin medir"));
		UE_LOG(LogTNBuggy, Display,
			TEXT("[Medida] %s: velocidad máxima %.1f km/h, 0-60 km/h %s, 0-100 km/h %s, frenada desde 60 km/h %s, vuelcos %d"),
			Why, Run->MaxKmh, *SprintTime(Run->ZeroToSixty), *SprintTime(Run->ZeroToHundred), *Brake, Run->Flips);
		if (Run->bQuitWhenDone)
		{
			FPlatformMisc::RequestExit(false, TEXT("TN.Rally.Measure"));
		}
	}

	/** Por valor por lo mismo que Finish: la copia mantiene vivo FRun mientras dura el paso. */
	void Step(TSharedRef<FRun> Run)
	{
		UWorld* World = Run->World.Get();
		if (!World || Run->Stage == EStage::Done)
		{
			return;
		}
		const double Now = World->GetTimeSeconds();
		if (Run->Stage == EStage::Wait)
		{
			ATN_Buggy* Candidate = PickBuggy(*World);
			ATN_RallyTrack* Track = PickTrack(*World);
			if (Candidate && Candidate->HasAuthority() && Track && Track->IsBuilt() && IsReady(*World))
			{
				Run->Buggy = Candidate;
				Run->Track = Track;
				Run->Arc = Track->FindArcGlobal(Candidate->GetActorLocation());
				if (AController* Pilot = Candidate->GetController(); Pilot && !Pilot->IsA<APlayerController>())
				{
					Pilot->SetActorTickEnabled(false);
					Run->SuspendedPilot = Pilot;
				}
				Candidate->SetEngineLocked(false);
				Run->Stage = EStage::Settle;
				Run->StageStart = Now;
				UE_LOG(LogTNBuggy, Log, TEXT("[Medida] buggy %s en la pista (%.0f m), parando"), *Candidate->GetName(), Track->GetTrackLengthCm() / 100.0);
			}
			else if (Now - Run->StageStart > WaitTimeoutSeconds)
			{
				Finish(Run, TEXT("sin buggy o sin carrera en marcha"));
			}
			return;
		}

		ATN_Buggy* Buggy = Run->Buggy.Get();
		ATN_RallyTrack* Track = Run->Track.Get();
		if (!Buggy || !Track)
		{
			Finish(Run, TEXT("el buggy o la pista han desaparecido"));
			return;
		}
		const float Kmh = static_cast<float>(TNRally::CmsToKmh(Buggy->GetForwardSpeedCms()));
		const bool bFlipped = Buggy->IsFlipped();
		Run->Flips += (bFlipped && !Run->bWasFlipped) ? 1 : 0;
		Run->bWasFlipped = bFlipped;
		const float Steer = SteerAlongTrack(*Run, *Buggy, *Track);
		const double Elapsed = Now - Run->StageStart;

		switch (Run->Stage)
		{
		case EStage::Settle:
			// Con bReverseAsBrake, frenar parado mete la marcha atrás: hacia delante se frena con el freno, hacia atrás
			// con el acelerador y, casi parado, con el freno de mano.
			Buggy->SetAIDriveInput(Kmh < -1.f ? 0.6f : 0.f, Kmh > 1.f ? 1.f : 0.f, Steer, FMath::Abs(Kmh) <= 1.f);
			if ((FMath::Abs(Kmh) < StopKmh && Elapsed > 0.5) || Elapsed > SettleTimeoutSeconds)
			{
				Run->Stage = EStage::Sprint;
				Run->StageStart = Now;
				UE_LOG(LogTNBuggy, Log, TEXT("[Medida] salida desde %.1f km/h"), Kmh);
			}
			break;
		case EStage::Sprint:
			Buggy->SetAIDriveInput(1.f, 0.f, Steer, false);
			Run->MaxKmh = FMath::Max(Run->MaxKmh, Kmh);
			if (Run->ZeroToSixty < 0.f && Kmh >= SprintMarkKmh)
			{
				Run->ZeroToSixty = static_cast<float>(Elapsed);
			}
			if (Run->ZeroToHundred < 0.f && Kmh >= SprintToKmh)
			{
				Run->ZeroToHundred = static_cast<float>(Elapsed);
			}
			if (Elapsed >= Run->SprintSeconds && Kmh >= BrakeFromKmh)
			{
				Run->Stage = EStage::Brake;
				Run->StageStart = Now;
				UE_LOG(LogTNBuggy, Log, TEXT("[Medida] frenando desde %.1f km/h"), Kmh);
			}
			else if (Elapsed >= Run->SprintSeconds * 2.f)
			{
				Finish(Run, TEXT("no pasa de 60 km/h"));
			}
			break;
		case EStage::Brake:
			Buggy->SetAIDriveInput(0.f, 1.f, Steer, false);
			if (!Run->bBrakeArmed && Kmh <= BrakeFromKmh)
			{
				Run->bBrakeArmed = true;
				Run->BrakeFrom = Buggy->GetActorLocation();
			}
			if (Run->bBrakeArmed && Kmh < StopKmh)
			{
				Run->BrakeMeters = static_cast<float>(FVector::Dist2D(Run->BrakeFrom, Buggy->GetActorLocation()) / 100.0);
				Buggy->SetAIDriveInput(0.f, 0.f, 0.f, true);
				Finish(Run, TEXT("hecho"));
			}
			else if (Elapsed > BrakeTimeoutSeconds)
			{
				Finish(Run, TEXT("no se para"));
			}
			break;
		default:
			break;
		}
	}

	void Start(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogTNBuggy, Warning, TEXT("[Medida] TN.Rally.Measure: solo en el servidor"));
			return;
		}
		TSharedRef<FRun> Run = MakeShared<FRun>();
		Run->World = World;
		Run->SprintSeconds = Args.IsValidIndex(0) ? FMath::Max(5.f, FCString::Atof(*Args[0])) : 25.f;
		Run->bQuitWhenDone = Args.IsValidIndex(1) && FCString::Atoi(*Args[1]) != 0;
		Run->StageStart = World->GetTimeSeconds();
		World->GetTimerManager().SetTimer(Run->Timer, FTimerDelegate::CreateLambda([Run]() { Step(Run); }), TickSeconds, true);
		UE_LOG(LogTNBuggy, Log, TEXT("[Medida] esperando a la carrera (aceleración %.0f s)"), Run->SprintSeconds);
	}

	FAutoConsoleCommandWithWorldAndArgs CmdMeasure(TEXT("TN.Rally.Measure"),
		TEXT("Rally (servidor): TN.Rally.Measure [segundos de aceleración = 25] [cerrar 0|1]: velocidad máxima, 0-60 y 0-100 km/h y frenada desde 60 km/h del buggy en la pista (LogTNBuggy [Medida])."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Start(Args, World); }));
}

#endif // !UE_BUILD_SHIPPING
