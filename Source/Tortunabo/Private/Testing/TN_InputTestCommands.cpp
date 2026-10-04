// Comandos para probar sin tocar el teclado ni el mando (Docs/Comandos_Prueba.md, «Mando y avisos de botones»):
//   TN.Later <segundos> <comando>  ejecuta el comando pasado ese tiempo (para encadenar pruebas en -ExecCmds).
//   TN.Input.Press <tecla> [s]     pulsa (y mantiene esos segundos) una tecla o un botón (Gamepad_FaceButton_Bottom, E...) como si viniera
//                                  del aparato: pasa por Slate y por los preprocesadores de entrada, como una pulsación real.

#include "Core/TN_Log.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"

#if !UE_BUILD_SHIPPING

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto del bloque.
namespace TNInputTestCommands
{
	void Later(const TArray<FString>& Args, UWorld* World)
	{
		float Seconds = 0.f;
		if (Args.Num() < 2 || !LexTryParseString(Seconds, *Args[0]) || Seconds < 0.f)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("TN.Later <segundos> <comando>"));
			return;
		}
		const FString Command = FString::Join(TArrayView<const FString>(Args).RightChop(1), TEXT(" "));
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, Command](float)
		{
			UE_LOG(LogTortunabo, Log, TEXT("[TN.Later] %s"), *Command);
			// Por el jugador, como escrito en su consola: así llegan también los del visor (shot, HighResShot...).
			UWorld* Target = (GEngine && GEngine->GameViewport) ? GEngine->GameViewport->GetWorld() : WeakWorld.Get();
			if (APlayerController* PC = Target ? Target->GetFirstPlayerController() : nullptr)
			{
				PC->ConsoleCommand(Command, true);
			}
			else if (GEngine)
			{
				GEngine->Exec(Target, *Command);
			}
			return false;
		}), Seconds);
	}

	void Press(const TArray<FString>& Args, UWorld* World)
	{
		const FKey Key = Args.Num() > 0 ? FKey(FName(*Args[0])) : FKey();
		if (!Key.IsValid() || !FSlateApplication::IsInitialized())
		{
			UE_LOG(LogTortunabo, Warning, TEXT("TN.Input.Press <tecla> (por ejemplo Gamepad_FaceButton_Bottom o E)"));
			return;
		}
		float HoldSeconds = 0.f;
		if (Args.Num() > 1) { LexTryParseString(HoldSeconds, *Args[1]); }
		const FKeyEvent Event(Key, FModifierKeysState(), 0u, false, 0u, 0u);
		FSlateApplication::Get().ProcessKeyDownEvent(Event);
		UE_LOG(LogTortunabo, Log, TEXT("[TN.Input.Press] %s (%.1f s)"), *Key.ToString(), HoldSeconds);
		// Mantenida: se suelta pasado ese tiempo (para andar o para las interacciones de mantener).
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Event](float)
		{
			if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().ProcessKeyUpEvent(Event); }
			return false;
		}), FMath::Max(0.f, HoldSeconds));
	}

	FAutoConsoleCommandWithWorldAndArgs CmdLater(TEXT("TN.Later"),
		TEXT("TN.Later <segundos> <comando>: ejecuta el comando pasado ese tiempo."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Later));
	FAutoConsoleCommandWithWorldAndArgs CmdPress(TEXT("TN.Input.Press"),
		TEXT("TN.Input.Press <tecla> [segundos]: pulsa la tecla o el botón como si viniera del aparato (y la mantiene esos segundos)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Press));
}

#endif
