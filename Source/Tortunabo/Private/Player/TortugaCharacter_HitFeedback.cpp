// ─────────────────────────────────────────────────────────────────────────────
// TortugaCharacter_HitFeedback.cpp
// Respuesta a los golpes (#350): el servidor decide el golpe y solo la máquina de quien lo recibe nota la sacudida de
// cámara y la vibración del mando (TNHitFeedback, con los ajustes de ese jugador).
// ─────────────────────────────────────────────────────────────────────────────

#include "Player/TortugaCharacter.h"
#include "Player/TN_HitFeedback.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Core/TN_Log.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

void ATortugaCharacter::NotifyHitFeedback(float Strength)
{
	if (!HasAuthority() || LastHitFeedbackFrame == GFrameCounter)
	{
		return;
	}
	LastHitFeedbackFrame = GFrameCounter;
	const float Clamped = FMath::Clamp(Strength, 0.f, 1.f);

	// Anfitrión que juega con esta tortuga: en el acto. Si no, solo a su dueño (las IA y los bots no tienen pantalla).
	if (IsLocallyControlled())
	{
		TNHitFeedback::PlayLocal(Cast<APlayerController>(GetController()), Clamped);
		return;
	}
	const bool bToOwner = IsPlayerControlled();
	UE_LOG(LogTortunabo, Verbose, TEXT("[HitFeedback] %s golpe %.2f %s"), *GetNameSafe(this), Clamped,
		bToOwner ? TEXT("a su dueño") : TEXT("sin jugador: nada"));
	if (bToOwner)
	{
		ClientPlayHitFeedback(Clamped);
	}
}

void ATortugaCharacter::ClientPlayHitFeedback_Implementation(float Strength)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[HitFeedback] %s: golpe sin mando local (aún no ha llegado su Controller)."), *GetNameSafe(this));
		return;
	}
	TNHitFeedback::PlayLocal(PC, Strength);
}

// ─────────────────────────────────────────────────────────────────────────────
// Consola de pruebas: TN.Debug.Knockdown
// ─────────────────────────────────────────────────────────────────────────────

#if !UE_BUILD_SHIPPING
namespace TNHitFeedbackDebug
{
	constexpr float DefaultKnockdownSeconds = 2.f;

	ATortugaCharacter* TurtleOfPlayer(UWorld* World, int32 PlayerIndex)
	{
		int32 Index = 0;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It, ++Index)
		{
			if (Index == PlayerIndex)
			{
				const APlayerController* PC = It->Get();
				return PC ? Cast<ATortugaCharacter>(PC->GetPawn()) : nullptr;
			}
		}
		return nullptr;
	}

	void KnockdownNow(UWorld* World, int32 PlayerIndex, float Seconds, float Impulse)
	{
		ATortugaCharacter* Turtle = World ? TurtleOfPlayer(World, PlayerIndex) : nullptr;
		if (!Turtle)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[HitFeedback] TN.Debug.Knockdown: el jugador %d no tiene tortuga."), PlayerIndex);
			return;
		}
		UE_LOG(LogTortunabo, Log, TEXT("[HitFeedback] TN.Debug.Knockdown %s %.1fs empujón %.0f"), *GetNameSafe(Turtle), Seconds, Impulse);
		Turtle->ApplyKnockdown(Seconds, FVector(0.f, 0.f, Impulse));
	}

	/** TN.Debug.Knockdown [segundos] [jugador] [empujón hacia arriba en cm/s] [retraso en s], en la consola del anfitrión. */
	void Knockdown(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[HitFeedback] TN.Debug.Knockdown solo en la consola del anfitrión."));
			return;
		}
		const float Seconds = Args.Num() > 0 ? FCString::Atof(*Args[0]) : DefaultKnockdownSeconds;
		const int32 PlayerIndex = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 0;
		const float Impulse = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 0.f;
		const float Delay = Args.Num() > 3 ? FCString::Atof(*Args[3]) : 0.f;
		if (Delay <= 0.f)
		{
			KnockdownNow(World, PlayerIndex, Seconds, Impulse);
			return;
		}
		// Con retraso (para lanzarlo con -ExecCmds antes de que haya tortugas).
		FTimerHandle Handle;
		const TWeakObjectPtr<UWorld> WeakWorld(World);
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, PlayerIndex, Seconds, Impulse]()
		{
			KnockdownNow(WeakWorld.Get(), PlayerIndex, Seconds, Impulse);
		}), Delay, false);
	}

	FAutoConsoleCommandWithWorldAndArgs KnockdownCommand(
		TEXT("TN.Debug.Knockdown"),
		TEXT("TN.Debug.Knockdown [segundos=2] [jugador=0] [empujón hacia arriba en cm/s=0] [retraso en s=0]: derriba a esa ")
		TEXT("tortuga para probar el sonido del derribo, el latido y el de levantarse, y la sacudida y la vibración de quien ")
		TEXT("cae. Solo en el anfitrión."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Knockdown), ECVF_Cheat);
}
#endif
