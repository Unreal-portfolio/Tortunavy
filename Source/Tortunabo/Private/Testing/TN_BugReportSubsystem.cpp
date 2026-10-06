#include "Testing/TN_BugReportSubsystem.h"

#include "Core/TN_Log.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameState.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformOutputDevices.h"
#include "ImageUtils.h"
#include "InputCoreTypes.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Testing/TN_BugReport.h"
#include "Testing/TN_MonkeySubsystem.h"
#include "Testing/TN_TestReport.h"
#include "UnrealClient.h"
#include "Widgets/SViewport.h"

namespace TNBugReportSubsystemDetail
{
	/** Bytes del final del fichero de registro que se leen para sacar las últimas 2000 líneas. */
	constexpr int64 LogTailBytes = 4 * 1024 * 1024;
	/** Largo máximo de un valor volcado por reflexión (los TArray grandes se recortan). */
	constexpr int32 MaxValueLen = 400;

	/** F8 antes que el PlayerController y el HUD (que no se tocan): lo pasa al subsistema de su juego. */
	class FInput : public IInputProcessor
	{
	public:
		explicit FInput(TFunction<bool()> InOnF8) : OnF8(MoveTemp(InOnF8)) {}

		virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

		virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
		{
			return InKeyEvent.GetKey() == EKeys::F8 && !InKeyEvent.IsRepeat() && OnF8 && OnF8();
		}

		virtual const TCHAR* GetDebugName() const override { return TEXT("TNBugReport"); }

	private:
		TFunction<bool()> OnF8;
	};

	FString ReadLogTail()
	{
		if (GLog)
		{
			GLog->Flush();
		}
		const FString Path = FPlatformOutputDevices::GetAbsoluteLogFilename();
		const TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Path, FILEREAD_AllowWrite));
		if (!Reader)
		{
			return FString();
		}
		const int64 Size = Reader->TotalSize();
		const int64 Start = FMath::Max<int64>(0, Size - LogTailBytes);
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(static_cast<int32>(Size - Start));
		Reader->Seek(Start);
		Reader->Serialize(Bytes.GetData(), Bytes.Num());
		const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
		FString Text(Converted.Length(), Converted.Get());
		// Al empezar a mitad de fichero la primera línea sale cortada.
		int32 FirstBreak = INDEX_NONE;
		if (Start > 0 && Text.FindChar(TEXT('\n'), FirstBreak))
		{
			Text.RightChopInline(FirstBreak + 1);
		}
		return Text;
	}

	FString Meters(const FVector& Location)
	{
		return FString::Printf(TEXT("%.1f, %.1f, %.1f"), Location.X / 100.0, Location.Y / 100.0, Location.Z / 100.0);
	}

	/** Propiedades declaradas en clases del juego (no del motor): el estado propio del objeto sin el ruido de AActor. */
	TSharedRef<FJsonObject> DumpGameProperties(const UObject* Object)
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		if (!Object)
		{
			return Out;
		}
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			const FProperty* Property = *It;
			const UClass* Owner = Property->GetOwnerClass();
			if (!Owner || !Owner->GetOutermost()->GetName().StartsWith(TEXT("/Script/Tortunabo")) || Property->IsA<FDelegateProperty>()
				|| Property->IsA<FMulticastDelegateProperty>())
			{
				continue;
			}
			FString Value;
			// Delta = el propio objeto: sin él, ExportText omite los valores iguales a cero (salían vacíos).
			Property->ExportText_InContainer(0, Value, Object, Object, nullptr, PPF_None);
			if (Value.Len() > MaxValueLen)
			{
				Value = Value.Left(MaxValueLen) + TEXT("…");
			}
			Out->SetStringField(Property->GetName(), Value);
		}
		return Out;
	}

	/** Propiedades enteras con «Seed» en el nombre (MapSeed, Seed…): la semilla del modo sin conocer cada clase. */
	void CollectSeeds(const UObject* Object, TArray<FString>& Out)
	{
		if (!Object)
		{
			return;
		}
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			const FNumericProperty* Numeric = CastField<FNumericProperty>(*It);
			if (Numeric && Numeric->IsInteger() && Numeric->GetName().Contains(TEXT("Seed")))
			{
				const int64 Value = Numeric->GetSignedIntPropertyValue(Numeric->ContainerPtrToValuePtr<void>(Object));
				Out.Add(FString::Printf(TEXT("%s.%s=%lld"), *Object->GetClass()->GetName(), *Numeric->GetName(), Value));
			}
		}
	}

	FString GameModeName(const UWorld& World)
	{
		if (const AGameModeBase* GameMode = World.GetAuthGameMode())
		{
			return GameMode->GetClass()->GetName();
		}
		const AGameStateBase* GameState = World.GetGameState();
		return GameState && GameState->GameModeClass ? GameState->GameModeClass->GetName() : FString(TEXT("?"));
	}

	TSharedRef<FJsonObject> NetworkJson(const UWorld& World, FString& OutSummary)
	{
		TSharedRef<FJsonObject> Net = MakeShared<FJsonObject>();
		const AGameStateBase* GameState = World.GetGameState();
		const int32 Players = GameState ? GameState->PlayerArray.Num() : 0;
		Net->SetStringField(TEXT("net_mode"), TNTestReport::NetModeName(&World));
		Net->SetNumberField(TEXT("players"), Players);
		FString Emulation;
		if (const UNetDriver* Driver = World.GetNetDriver())
		{
			Net->SetStringField(TEXT("driver"), Driver->GetClass()->GetName());
			Net->SetNumberField(TEXT("client_connections"), Driver->ClientConnections.Num());
			if (Driver->ServerConnection)
			{
				Net->SetStringField(TEXT("server_address"), Driver->ServerConnection->LowLevelGetRemoteAddress(true));
			}
#if DO_ENABLE_NET_TEST
			const FPacketSimulationSettings& Sim = Driver->PacketSimulationSettings;
			Net->SetNumberField(TEXT("pkt_lag_ms"), Sim.PktLag);
			Net->SetNumberField(TEXT("pkt_lag_variance_ms"), Sim.PktLagVariance);
			Net->SetNumberField(TEXT("pkt_loss_pct"), Sim.PktLoss);
			if (Sim.PktLag > 0 || Sim.PktLoss > 0)
			{
				Emulation = FString::Printf(TEXT(" · PktLag %d ± %d ms, PktLoss %d %%"), Sim.PktLag, Sim.PktLagVariance, Sim.PktLoss);
			}
#endif
		}
		FString Ping;
		if (const APlayerController* PC = World.GetFirstPlayerController())
		{
			if (const APlayerState* PlayerState = PC->PlayerState)
			{
				Net->SetNumberField(TEXT("ping_ms"), PlayerState->GetPingInMilliseconds());
				Ping = FString::Printf(TEXT(" · ping %.0f ms"), PlayerState->GetPingInMilliseconds());
			}
		}
		OutSummary = FString::Printf(TEXT("%d jugadores%s%s"), Players, *Ping, *Emulation);
		return Net;
	}

	TSharedRef<FJsonObject> PlayerJson(const APlayerController& PC, int32 Index)
	{
		TSharedRef<FJsonObject> Player = MakeShared<FJsonObject>();
		Player->SetNumberField(TEXT("local_index"), Index);
		Player->SetStringField(TEXT("controller"), PC.GetClass()->GetName());
		if (const APlayerState* PlayerState = PC.PlayerState)
		{
			Player->SetStringField(TEXT("name"), PlayerState->GetPlayerName());
			Player->SetNumberField(TEXT("score"), PlayerState->GetScore());
			Player->SetObjectField(TEXT("player_state"), DumpGameProperties(PlayerState));
		}
		const APawn* Pawn = PC.GetPawn();
		if (!Pawn)
		{
			Player->SetStringField(TEXT("pawn"), TEXT("ninguno"));
			return Player;
		}
		Player->SetStringField(TEXT("pawn"), Pawn->GetClass()->GetName());
		Player->SetStringField(TEXT("location_m"), Meters(Pawn->GetActorLocation()));
		Player->SetStringField(TEXT("rotation"), Pawn->GetActorRotation().ToCompactString());
		Player->SetStringField(TEXT("velocity_m_s"), Meters(Pawn->GetVelocity()));
		Player->SetStringField(TEXT("local_role"), StaticEnum<ENetRole>()->GetNameStringByValue(Pawn->GetLocalRole()));
		if (const ACharacter* Character = Cast<ACharacter>(Pawn))
		{
			if (const UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
			{
				Player->SetStringField(TEXT("movement_mode"), Movement->GetMovementName());
			}
		}
		Player->SetObjectField(TEXT("pawn_state"), DumpGameProperties(Pawn));
		return Player;
	}

	bool SaveText(const FString& Text, const FString& Path)
	{
		return FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	/** PNG del widget del viewport de este juego (escena y UMG), ahora y sin pasar por la petición global de captura. */
	bool CaptureViewport(const UGameViewportClient& ViewportClient, const FString& Path)
	{
		const TSharedPtr<SViewport> Widget = ViewportClient.GetGameViewportWidget();
		if (!Widget.IsValid() || !FSlateApplication::IsInitialized())
		{
			return false;
		}
		TArray<FColor> Pixels;
		FIntVector Size;
		if (!FSlateApplication::Get().TakeScreenshot(Widget.ToSharedRef(), Pixels, Size) || Size.X <= 0 || Size.Y <= 0
			|| Pixels.Num() != Size.X * Size.Y)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Informe] No se ha podido capturar el viewport."));
			return false;
		}
		// Slate devuelve el alfa del búfer de la ventana, no siempre opaco.
		for (FColor& Pixel : Pixels)
		{
			Pixel.A = 255;
		}
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Size.X, Size.Y, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
		return !Png.IsEmpty() && FFileHelper::SaveArrayToFile(Png, *Path);
	}

	void RunCommand(UWorld* World)
	{
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		UTN_BugReportSubsystem* Reports = GameInstance ? GameInstance->GetSubsystem<UTN_BugReportSubsystem>() : nullptr;
		if (!Reports)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Informe] TN.BugReport: solo en un juego que no es Shipping."));
			return;
		}
		Reports->CreateReport(TEXT("consola"));
	}

	static FAutoConsoleCommandWithWorld CmdBugReport(TEXT("TN.BugReport"),
		TEXT("Informe de bug (lo mismo que F8): Saved/BugReports/<fecha>/ con captura, log, partida.json, jugador.json e informe.md."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunCommand));
}

bool UTN_BugReportSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return Super::ShouldCreateSubsystem(Outer);
#endif
}

void UTN_BugReportSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if !UE_BUILD_SHIPPING
	if (FSlateApplication::IsInitialized())
	{
		const TWeakObjectPtr<UTN_BugReportSubsystem> WeakThis(this);
		InputProcessor = MakeShared<TNBugReportSubsystemDetail::FInput>([WeakThis]()
		{
			UTN_BugReportSubsystem* Self = WeakThis.Get();
			return Self && Self->HandleF8();
		});
		FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor);
	}
	// -TNBugReportAfter=<s>: informe automático (pruebas sin teclado, p. ej. un cliente -nullrhi con el monkey).
	const FString After = TNTestReport::CommandLineValue(TEXT("-TNBugReportAfter"));
	if (!After.IsEmpty())
	{
		AutoReportAt = FPlatformTime::Seconds() + FMath::Max(0.0, FCString::Atod(*After));
		AutoReportHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UTN_BugReportSubsystem::TickAutoReport), 0.5f);
	}
#endif
}

void UTN_BugReportSubsystem::Deinitialize()
{
	if (InputProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
	}
	InputProcessor.Reset();
	if (AutoReportHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(AutoReportHandle);
		AutoReportHandle.Reset();
	}
	Super::Deinitialize();
}

bool UTN_BugReportSubsystem::HasViewportFocus() const
{
	// Fuera del editor solo hay un juego por proceso. En PIE hay uno por ventana: responde el que tiene el foco.
	if (!GIsEditor)
	{
		return true;
	}
	const UGameViewportClient* Viewport = GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr;
	const TSharedPtr<SViewport> Widget = Viewport ? Viewport->GetGameViewportWidget() : nullptr;
	return Widget.IsValid() && (Widget->HasKeyboardFocus() || Widget->HasFocusedDescendants());
}

bool UTN_BugReportSubsystem::HandleF8()
{
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World || !World->IsGameWorld() || !HasViewportFocus())
	{
		return false;
	}
	return !CreateReport(TEXT("F8")).IsEmpty();
}

bool UTN_BugReportSubsystem::TickAutoReport(float DeltaTime)
{
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (FPlatformTime::Seconds() < AutoReportAt || !World || !World->HasBegunPlay())
	{
		return true;
	}
	CreateReport(TEXT("línea de órdenes"));
	AutoReportHandle.Reset();
	return false;
}

FString UTN_BugReportSubsystem::MakeFolder() const
{
	const FString Base = FPaths::Combine(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()), TEXT("BugReports"), TNBugReport::FolderName(FDateTime::Now()));
	FString Folder = Base;
	// Dos informes en el mismo segundo (varias ventanas de PIE): sufijo.
	for (int32 Suffix = 2; IFileManager::Get().DirectoryExists(*Folder); ++Suffix)
	{
		Folder = FString::Printf(TEXT("%s_%d"), *Base, Suffix);
	}
	return Folder;
}

FString UTN_BugReportSubsystem::CreateReport(const FString& Trigger)
{
#if UE_BUILD_SHIPPING
	return FString();
#else
	using namespace TNBugReportSubsystemDetail;
	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!World)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Informe] Sin mundo de juego: no hay informe."));
		return FString();
	}
	const FString Folder = MakeFolder();
	if (!IFileManager::Get().MakeDirectory(*Folder, true))
	{
		UE_LOG(LogTortunabo, Error, TEXT("[Informe] No se ha podido crear %s."), *Folder);
		return FString();
	}

	TNBugReport::FContext Context;
	Context.Date = FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S"));
	Context.Commit = TNBugReport::CommitFromGit(FPaths::ProjectDir());
	Context.Build = TNTestReport::BuildConfigName();
	Context.Map = World->GetMapName();
	Context.Mode = GameModeName(*World);
	Context.NetMode = TNTestReport::NetModeName(World);
	Context.Trigger = Trigger;
	FString DisplayFolder = Folder;
	FPaths::MakePathRelativeTo(DisplayFolder, *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
	Context.Folder = DisplayFolder;

	// Semillas: las del GameState y el GameMode (este solo en el servidor) y la del monkey si está en marcha.
	CollectSeeds(World->GetGameState(), Context.Seeds);
	CollectSeeds(World->GetAuthGameMode(), Context.Seeds);
	if (const UTN_MonkeySubsystem* Monkey = World->GetSubsystem<UTN_MonkeySubsystem>(); Monkey && Monkey->IsRunning())
	{
		Context.Seeds.Add(FString::Printf(TEXT("TN.Monkey=%d"), Monkey->GetSeed()));
	}

	// Jugadores locales (en PIE solo los de esta ventana).
	TArray<TSharedPtr<FJsonValue>> Players;
	int32 LocalIndex = 0;
	for (const ULocalPlayer* Local : GameInstance->GetLocalPlayers())
	{
		const APlayerController* PC = Local ? Local->GetPlayerController(World) : nullptr;
		if (!PC)
		{
			continue;
		}
		if (LocalIndex == 0 && PC->GetPawn())
		{
			Context.Position = Meters(PC->GetPawn()->GetActorLocation());
		}
		Players.Add(MakeShared<FJsonValueObject>(PlayerJson(*PC, LocalIndex++)));
	}

	// Registro: últimas 2000 líneas a log.txt y los errores y avisos al Markdown.
	const TArray<FString> LogTail = TNBugReport::LastLines(ReadLogTail(), TNBugReport::LogLines);
	Context.Notable = TNBugReport::NotableLogLines(LogTail, TNBugReport::NotableLines);
	SaveText(FString::Join(LogTail, TEXT("\n")) + TEXT("\n"), FPaths::Combine(Folder, TNBugReport::LogFile()));

	TSharedRef<FJsonObject> Match = MakeShared<FJsonObject>();
	Match->SetStringField(TEXT("date"), Context.Date);
	Match->SetStringField(TEXT("commit"), Context.Commit);
	Match->SetStringField(TEXT("build"), Context.Build);
	Match->SetStringField(TEXT("map"), Context.Map);
	Match->SetStringField(TEXT("map_package"), World->GetOutermost()->GetName());
	Match->SetStringField(TEXT("game_mode"), Context.Mode);
	TArray<TSharedPtr<FJsonValue>> SeedsJson;
	for (const FString& Seed : Context.Seeds)
	{
		SeedsJson.Add(MakeShared<FJsonValueString>(Seed));
	}
	Match->SetArrayField(TEXT("seeds"), SeedsJson);
	Match->SetObjectField(TEXT("network"), NetworkJson(*World, Context.Network));
	if (const AGameStateBase* GameState = World->GetGameState())
	{
		Match->SetStringField(TEXT("game_state_class"), GameState->GetClass()->GetName());
		Match->SetNumberField(TEXT("server_time_s"), GameState->GetServerWorldTimeSeconds());
		if (const AGameState* MatchState = Cast<AGameState>(GameState))
		{
			Match->SetStringField(TEXT("match_state"), MatchState->GetMatchState().ToString());
		}
		TArray<TSharedPtr<FJsonValue>> PlayerStates;
		for (const APlayerState* PlayerState : GameState->PlayerArray)
		{
			if (!PlayerState)
			{
				continue;
			}
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("name"), PlayerState->GetPlayerName());
			Item->SetNumberField(TEXT("ping_ms"), PlayerState->GetPingInMilliseconds());
			Item->SetNumberField(TEXT("score"), PlayerState->GetScore());
			Item->SetBoolField(TEXT("is_bot"), PlayerState->IsABot());
			PlayerStates.Add(MakeShared<FJsonValueObject>(Item));
		}
		Match->SetArrayField(TEXT("player_states"), PlayerStates);
		Match->SetObjectField(TEXT("game_state"), DumpGameProperties(GameState));
	}
	TNTestReport::Save(*Match, FPaths::Combine(Folder, TNBugReport::MatchFile()));

	TSharedRef<FJsonObject> PlayerRoot = MakeShared<FJsonObject>();
	PlayerRoot->SetArrayField(TEXT("local_players"), Players);
	TNTestReport::Save(*PlayerRoot, FPaths::Combine(Folder, TNBugReport::PlayerFile()));

	// Captura del viewport de este juego (sin RHI no hay ninguno). FScreenshotRequest es global: en PIE la atendería el
	// primer viewport que dibujase, que puede ser otra ventana; solo se usa fuera del editor si la captura directa falla.
	const UGameViewportClient* ViewportClient = GameInstance->GetGameViewportClient();
	const FString ScreenshotPath = FPaths::Combine(Folder, TNBugReport::ScreenshotFile());
	Context.bHasScreenshot = FApp::CanEverRender() && ViewportClient && CaptureViewport(*ViewportClient, ScreenshotPath);
	if (!Context.bHasScreenshot && FApp::CanEverRender() && ViewportClient && !GIsEditor)
	{
		FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false);
		Context.bHasScreenshot = true;
	}

	SaveText(TNBugReport::FormatMarkdown(Context), FPaths::Combine(Folder, TNBugReport::MarkdownFile()));
	LastFolder = Folder;
	UE_LOG(LogTortunabo, Display, TEXT("[Informe] %s: informe de bug en %s (%s, %s, %s)."), *Trigger, *Folder, *Context.Map, *Context.NetMode, *Context.Commit);
	return Folder;
#endif
}
