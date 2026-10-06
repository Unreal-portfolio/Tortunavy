// Capturas de prueba de la tienda y del probador (TN.Shop.UIShots), fuera de Shipping: abre la tienda en las pestañas
// de cascos y de ojos y el probador, y saca capturas con la interfaz.
//   UnrealEditor-Win64-DebugGame.exe <uproject> /Game/Maps/Lobby/LVL_Lobby -game -RenderOffScreen -ResX=1600 -ResY=900
//     -NoSteam -ExecCmds="TN.Tutorial.Skip, TN.Shop.UIShots C:/ruta"

#if !UE_BUILD_SHIPPING

#include "UI/Shop/TN_ShopWidgets.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Player/MP_GamePlayerController.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogTNShopUIShots, Log, All);

namespace TNShopUIShotsDetail
{
	template <typename T>
	T* FindOpen(UWorld* World)
	{
		for (TObjectIterator<T> It; It; ++It)
		{
			if (It->GetWorld() == World && It->IsInViewport()) { return *It; }
		}
		return nullptr;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTNShopUIShotsCommand(
	TEXT("TN.Shop.UIShots"),
	TEXT("Pruebas: TN.Shop.UIShots [carpeta] [espera = 6]: saca capturas con interfaz de la tienda (pestañas de cascos y de ojos) y del probador y cierra el juego."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		const FString Dir = Args.IsValidIndex(0) ? Args[0] : FPaths::ProjectSavedDir() / TEXT("ShopUIShots");
		const float Wait = Args.IsValidIndex(1) ? FCString::Atof(*Args[1]) : 6.f;
		IFileManager::Get().MakeDirectory(*Dir, true);
		TSharedRef<int32> Step = MakeShared<int32>(0);
		TSharedRef<int32> Pause = MakeShared<int32>(FMath::CeilToInt(Wait / 0.25f));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Dir, Step, Pause](float) -> bool
		{
			using namespace TNShopUIShotsDetail;
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && Context.World()) { World = Context.World(); break; }
			}
			AMP_GamePlayerController* PC = World ? Cast<AMP_GamePlayerController>(World->GetFirstPlayerController()) : nullptr;
			if (!PC || !PC->GetPawn()) { return true; }
			if (*Pause > 0) { --*Pause; return true; }
			const auto Shot = [&Dir](const TCHAR* Name)
			{
				const FString File = Dir / FString(Name) + TEXT(".png");
				FScreenshotRequest::RequestScreenshot(File, true, false);
				UE_LOG(LogTNShopUIShots, Display, TEXT("[ShopUIShots] %s"), *File);
			};
			switch ((*Step)++)
			{
			case 0:
				PC->TNShop();
				*Pause = 8;
				return true;
			case 1:
				if (UTN_ShopWidget* Shop = FindOpen<UTN_ShopWidget>(World)) { Shop->DebugShowTab(0, 2); }
				*Pause = 12;
				return true;
			case 2:
				Shot(TEXT("tienda_cascos"));
				*Pause = 4;
				return true;
			case 3:
				if (UTN_ShopWidget* Shop = FindOpen<UTN_ShopWidget>(World)) { Shop->DebugShowTab(3, 2); }
				*Pause = 8;
				return true;
			case 4:
				Shot(TEXT("tienda_ojos"));
				*Pause = 4;
				return true;
			case 5:
				PC->CloseShopUI();
				PC->TNBooth();
				*Pause = 12;
				return true;
			case 6:
				if (UTN_BoothWidget* Booth = FindOpen<UTN_BoothWidget>(World)) { Booth->DebugShowRow(0, 1); }
				*Pause = 10;
				return true;
			case 7:
				Shot(TEXT("probador"));
				*Pause = 4;
				return true;
			default:
				UE_LOG(LogTNShopUIShots, Display, TEXT("[ShopUIShots] listo en %s"), *Dir);
				FPlatformMisc::RequestExit(false, TEXT("TN.Shop.UIShots"));
				return false;
			}
		}), 0.25f);
	}));

#endif
