#include "VR/TN_VRSubsystem.h"
#include "Multiplayer/TN_LocalPlayRules.h"
#include "Multiplayer/TN_LocalPlaySubsystem.h"
#include "VR/TN_VRRig.h"
#include "TN_VRInputProcessor.h"
#include "Core/TN_Log.h"
#include "Settings/TN_GameSettingsSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "IXRTrackingSystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "StereoRendering.h"
#include "XRLoadingScreenFunctionLibrary.h"

// ─────────────────────────────────────────────────────────────────────────────
// Consola
// ─────────────────────────────────────────────────────────────────────────────

static TAutoConsoleVariable<int32> CVarTNVRMode(
	TEXT("TN.VR"),
	-1,
	TEXT("Modo VR de Tortunavy: -1 = el del ajuste «Modo VR» (Automático de serie: gafas si el motor pinta en estéreo), ")
	TEXT("0 = apagado, 1 = gafas (enciende el HMD si hace falta), 2 = simulado sin gafas (primera persona, aletas e interfaz en el mundo con el ratón)."),
	ECVF_Default);

namespace TNVRSubsystemDetail
{
	/** Ajuste «Modo VR» del menú (FTNGameSettings::VRMode). */
	constexpr uint8 SettingAuto = 0;
	constexpr uint8 SettingOff = 1;
	constexpr uint8 SettingSimulated = 2;

	UTN_VRSubsystem* Find(UWorld* World)
	{
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UTN_VRSubsystem>() : nullptr;
	}

	void Status(UWorld* World)
	{
		if (const UTN_VRSubsystem* VR = Find(World))
		{
			UE_LOG(LogTortunabo, Display, TEXT("[VR] %s"), *VR->DescribeStatus());
		}
	}

	void Recenter(UWorld* World)
	{
		if (UTN_VRSubsystem* VR = Find(World))
		{
			VR->Recenter();
		}
	}

	FAutoConsoleCommandWithWorld StatusCommand(TEXT("TN.VR.Status"),
		TEXT("Escribe en el registro el modo VR, si hay gafas y estéreo, el dispositivo y el estado del panel."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&Status));
	FAutoConsoleCommandWithWorld RecenterCommand(TEXT("TN.VR.Recenter"),
		TEXT("Recentra la vista VR (mira al frente desde donde está la cabeza) y vuelve a poner delante el HUD o el menú."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&Recenter));

	bool IsStereoOn()
	{
		return GEngine && GEngine->StereoRenderingDevice.IsValid() && GEngine->StereoRenderingDevice->IsStereoEnabled();
	}

	/** Variables de consola que marean con gafas (desenfoque de movimiento, aberración cromática, profundidad de campo). */
	const TCHAR* const ComfortCVars[][2] = {
		{ TEXT("r.MotionBlurQuality"), TEXT("0") },
		{ TEXT("r.SceneColorFringeQuality"), TEXT("0") },
		{ TEXT("r.DepthOfFieldQuality"), TEXT("0") },
	};
}

// ─────────────────────────────────────────────────────────────────────────────
// Ciclo de vida
// ─────────────────────────────────────────────────────────────────────────────

UTN_VRSubsystem* UTN_VRSubsystem::Get(const UObject* WorldContext)
{
	UWorld* World = GEngine && WorldContext ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return TNVRSubsystemDetail::Find(World);
}

bool UTN_VRSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void UTN_VRSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogTortunabo, Log, TEXT("[VR] Subsistema listo. %s"), *DescribeStatus());
}

void UTN_VRSubsystem::Deinitialize()
{
	HideLoadingSplash();
	if (Mode != ETNVRMode::Off)
	{
		ApplyComfortSettings(false);
	}
	EnsureInputProcessor(false);
	if (ATN_VRRig* ActiveRig = Rig.Get())
	{
		ActiveRig->Destroy();
	}
	Rig.Reset();
	Mode = ETNVRMode::Off;
	TNVR::SetMode(ETNVRMode::Off);
	Super::Deinitialize();
}

ETickableTickType UTN_VRSubsystem::GetTickableTickType() const
{
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Conditional;
}

bool UTN_VRSubsystem::IsTickable() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance && GameInstance->GetGameViewportClient();
}

TStatId UTN_VRSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTN_VRSubsystem, STATGROUP_Tickables);
}

UWorld* UTN_VRSubsystem::GetTickableGameObjectWorld() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetWorld() : nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Modo
// ─────────────────────────────────────────────────────────────────────────────

ETNVRMode UTN_VRSubsystem::ResolveMode()
{
	using namespace TNVRSubsystemDetail;
	// Partida local (#311): con más de un jugador en el PC, la pantalla plana (unas gafas son de uno solo).
	if (const UTN_LocalPlaySubsystem* LocalPlay = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTN_LocalPlaySubsystem>() : nullptr)
	{
		if (!TNLocalPlay::AllowsVR(LocalPlay->IsLocalMode(), LocalPlay->GetNumPlayers()))
		{
			bTriedEnableHMD = false;
			return ETNVRMode::Off;
		}
	}
	int32 Choice = CVarTNVRMode.GetValueOnGameThread();
	if (Choice < 0)
	{
		// Línea de comandos (una vez leída vale para toda la sesión).
		static const int32 CommandLineChoice = FParse::Param(FCommandLine::Get(), TEXT("novr")) ? 0
			: (FParse::Param(FCommandLine::Get(), TEXT("vrsim")) ? 2 : -1);
		Choice = CommandLineChoice;
	}
	if (Choice < 0)
	{
		const UTN_GameSettingsSubsystem* Settings = UTN_GameSettingsSubsystem::Get(GetGameInstance());
		const uint8 Setting = Settings ? Settings->GetSettings().VRMode : SettingAuto;
		Choice = Setting == SettingOff ? 0 : (Setting == SettingSimulated ? 2 : -1);
	}
	switch (Choice)
	{
		case 0:
			bTriedEnableHMD = false;
			return ETNVRMode::Off;
		case 2:
			bTriedEnableHMD = false;
			return ETNVRMode::Simulated;
		case 1:
			// Gafas a la fuerza: se enciende el HMD una vez (si hay dispositivo OpenXR; si no, se queda apagado).
			if (!IsStereoOn() && !bTriedEnableHMD)
			{
				bTriedEnableHMD = true;
				const bool bEnabled = UHeadMountedDisplayFunctionLibrary::EnableHMD(true);
				UE_LOG(LogTortunabo, Log, TEXT("[VR] TN.VR 1: encender las gafas → %s."), bEnabled ? TEXT("hecho") : TEXT("no hay gafas OpenXR"));
			}
			return IsStereoOn() ? ETNVRMode::Headset : ETNVRMode::Off;
		default:
			bTriedEnableHMD = false;
			return IsStereoOn() ? ETNVRMode::Headset : ETNVRMode::Off;
	}
}

void UTN_VRSubsystem::ApplyMode(ETNVRMode NewMode)
{
	const ETNVRMode OldMode = Mode;
	Mode = NewMode;
	TNVR::SetMode(NewMode);

	if (NewMode == ETNVRMode::Headset)
	{
		// Origen a la altura de los ojos (sentado o de pie da igual: la vista va a la altura de la tortuga) y al frente.
		UHeadMountedDisplayFunctionLibrary::SetTrackingOrigin(EHMDTrackingOrigin::Local);
		UHeadMountedDisplayFunctionLibrary::ResetOrientationAndPosition(0.f);
	}
	if ((OldMode == ETNVRMode::Headset) != (NewMode == ETNVRMode::Headset))
	{
		ApplyComfortSettings(NewMode == ETNVRMode::Headset);
	}
	EnsureInputProcessor(NewMode != ETNVRMode::Off);

	if (NewMode == ETNVRMode::Off)
	{
		HideLoadingSplash();
		if (ATN_VRRig* ActiveRig = Rig.Get())
		{
			ActiveRig->ReleaseScreenToViewport();
			ActiveRig->Destroy();
		}
		Rig.Reset();
	}
	else if (ATN_VRRig* ActiveRig = Rig.Get())
	{
		ActiveRig->OnModeChanged(NewMode);
	}
	UE_LOG(LogTortunabo, Log, TEXT("[VR] Modo: %d → %d. %s"), static_cast<int32>(OldMode), static_cast<int32>(NewMode), *DescribeStatus());
}

void UTN_VRSubsystem::ApplyComfortSettings(bool bHeadset)
{
	using namespace TNVRSubsystemDetail;
	for (const auto& Pair : ComfortCVars)
	{
		IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Pair[0]);
		if (!Var)
		{
			continue;
		}
		if (bHeadset)
		{
			if (!SavedCVars.Contains(Pair[0]))
			{
				SavedCVars.Add(Pair[0], Var->GetString());
			}
			Var->Set(Pair[1], ECVF_SetByCode);
		}
		else if (const FString* Old = SavedCVars.Find(Pair[0]))
		{
			Var->Set(**Old, ECVF_SetByCode);
		}
	}
	if (!bHeadset)
	{
		SavedCVars.Reset();
	}
}

void UTN_VRSubsystem::EnsureInputProcessor(bool bWanted)
{
	if (!FSlateApplication::IsInitialized())
	{
		InputProcessor.Reset();
		return;
	}
	if (bWanted && !InputProcessor.IsValid())
	{
		InputProcessor = MakeShared<FTNVRInputProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor);
	}
	else if (!bWanted && InputProcessor.IsValid())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
		InputProcessor.Reset();
	}
}

void UTN_VRSubsystem::Tick(float DeltaTime)
{
	const ETNVRMode Wanted = ResolveMode();
	if (Wanted != Mode)
	{
		ApplyMode(Wanted);
	}
	if (Mode == ETNVRMode::Off)
	{
		return;
	}
	const UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (World && World->IsGameWorld() && !World->bIsTearingDown)
	{
		GetRig(World, true);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Rig y pantalla
// ─────────────────────────────────────────────────────────────────────────────

ATN_VRRig* UTN_VRSubsystem::GetRig(UWorld* World, bool bCreate)
{
	ATN_VRRig* Existing = Rig.Get();
	if (Existing && Existing->GetWorld() == World && !Existing->IsActorBeingDestroyed())
	{
		return Existing;
	}
	if (!bCreate || Mode == ETNVRMode::Off || !World || !World->IsGameWorld() || World->bIsTearingDown || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	if (World->GetGameInstance() != GetGameInstance())
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	ATN_VRRig* NewRig = World->SpawnActor<ATN_VRRig>(ATN_VRRig::StaticClass(), FTransform::Identity, Params);
	Rig = NewRig;
	if (NewRig)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[VR] Rig creado en %s."), *World->GetMapName());
	}
	return NewRig;
}

ATN_VRRig* UTN_VRSubsystem::GetActiveRig() const
{
	ATN_VRRig* Existing = Rig.Get();
	return Existing && !Existing->IsActorBeingDestroyed() ? Existing : nullptr;
}

bool UTN_VRSubsystem::HostWidget(UUserWidget* Widget, int32 ZOrder)
{
	if (!Widget || Mode == ETNVRMode::Off)
	{
		return false;
	}
	UWorld* World = Widget->GetWorld();
	ATN_VRRig* TargetRig = GetRig(World, true);
	return TargetRig && TargetRig->HostWidget(Widget, ZOrder);
}

bool UTN_VRSubsystem::IsHostedWidget(const UUserWidget* Widget) const
{
	const ATN_VRRig* ActiveRig = GetActiveRig();
	return ActiveRig && ActiveRig->IsHosting(Widget);
}

bool UTN_VRSubsystem::HostSlate(UGameViewportClient* Viewport, const TSharedRef<SWidget>& Widget, int32 ZOrder)
{
	if (Mode == ETNVRMode::Off)
	{
		return false;
	}
	UWorld* World = Viewport ? Viewport->GetWorld() : nullptr;
	ATN_VRRig* TargetRig = GetRig(World, true);
	return TargetRig && TargetRig->HostSlate(Widget, ZOrder);
}

bool UTN_VRSubsystem::UnhostSlate(const TSharedRef<SWidget>& Widget)
{
	ATN_VRRig* ActiveRig = GetActiveRig();
	return ActiveRig && ActiveRig->UnhostSlate(Widget);
}

bool UTN_VRSubsystem::IsMenuMode() const
{
	const ATN_VRRig* ActiveRig = GetActiveRig();
	return ActiveRig && ActiveRig->IsMenuMode();
}

void UTN_VRSubsystem::Recenter()
{
	if (Mode == ETNVRMode::Headset)
	{
		UHeadMountedDisplayFunctionLibrary::ResetOrientationAndPosition(0.f);
	}
	if (ATN_VRRig* ActiveRig = GetActiveRig())
	{
		ActiveRig->RecenterPanel();
	}
	UE_LOG(LogTortunabo, Log, TEXT("[VR] Recentrado."));
}

// ─────────────────────────────────────────────────────────────────────────────
// Pantalla de carga de las gafas
// ─────────────────────────────────────────────────────────────────────────────

namespace TNVRSplashDetail
{
	/** Textura de 4 × N con una columna de colores de arriba abajo (el filtrado bilineal la convierte en degradado). */
	UTexture2D* MakeColumnTexture(const TCHAR* Name, const TArray<FColor>& Column)
	{
		const int32 W = 4;
		const int32 H = Column.Num();
		UTexture2D* Texture = H > 0 ? UTexture2D::CreateTransient(W, H, PF_B8G8R8A8, FName(Name)) : nullptr;
		if (!Texture)
		{
			return nullptr;
		}
		Texture->SRGB = true;
		Texture->Filter = TF_Bilinear;
		Texture->LODGroup = TEXTUREGROUP_UI;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		FColor* Data = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 y = 0; y < H; ++y)
		{
			for (int32 x = 0; x < W; ++x)
			{
				Data[y * W + x] = Column[y];
			}
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}
}

void UTN_VRSubsystem::EnsureSplashEnvironment()
{
	if (SplashEnvironment.Num() == 3)
	{
		return;
	}
	SplashEnvironment.Reset();
	// Lados: cielo arriba, bruma cálida en el horizonte (a la altura de los ojos) y mar abajo.
	const TArray<FColor> Sides = {
		FColor(58, 142, 219), FColor(74, 158, 226), FColor(96, 176, 234), FColor(126, 200, 242), FColor(170, 220, 245),
		FColor(214, 234, 240), FColor(245, 238, 222), FColor(255, 241, 214), FColor(120, 196, 206), FColor(64, 172, 196),
		FColor(42, 157, 181), FColor(36, 140, 168), FColor(31, 124, 155), FColor(27, 110, 143) };
	const TArray<FColor> Top = { FColor(52, 132, 212) };
	const TArray<FColor> Bottom = { FColor(233, 211, 161) };
	SplashEnvironment.Add(TNVRSplashDetail::MakeColumnTexture(TEXT("TN_VRSplashSides"), Sides));
	SplashEnvironment.Add(TNVRSplashDetail::MakeColumnTexture(TEXT("TN_VRSplashTop"), Top));
	SplashEnvironment.Add(TNVRSplashDetail::MakeColumnTexture(TEXT("TN_VRSplashBottom"), Bottom));
}

void UTN_VRSubsystem::ShowLoadingSplash(UTexture* Texture)
{
	if (Mode != ETNVRMode::Headset || !Texture)
	{
		return;
	}
	SplashTexture = Texture;
	UXRLoadingScreenFunctionLibrary::ClearLoadingScreenSplashes();
	// La playa en 360: un cubo de 10 m con la cara de delante de cada capa hacia dentro (sin girar, una capa mira a -X).
	EnsureSplashEnvironment();
	if (SplashEnvironment.Num() == 3 && SplashEnvironment[0] && SplashEnvironment[1] && SplashEnvironment[2])
	{
		constexpr double Half = 500.0;
		const FVector2D Face(2.0 * Half, 2.0 * Half);
		for (int32 Side = 0; Side < 4; ++Side)
		{
			const FRotator Yaw(0.0, 90.0 * Side, 0.0);
			UXRLoadingScreenFunctionLibrary::AddLoadingScreenSplash(SplashEnvironment[0], Yaw.RotateVector(FVector(Half, 0.0, 0.0)), Yaw,
				Face, FRotator::ZeroRotator, false);
		}
		UXRLoadingScreenFunctionLibrary::AddLoadingScreenSplash(SplashEnvironment[1], FVector(0.0, 0.0, Half), FRotator(90.0, 0.0, 0.0),
			Face, FRotator::ZeroRotator, false);
		UXRLoadingScreenFunctionLibrary::AddLoadingScreenSplash(SplashEnvironment[2], FVector(0.0, 0.0, -Half), FRotator(-90.0, 0.0, 0.0),
			Face, FRotator::ZeroRotator, false);
	}
	// El huevo delante, a la altura de los ojos: ~56° de ancho.
	UXRLoadingScreenFunctionLibrary::AddLoadingScreenSplash(Texture, FVector(300.0, 0.0, 0.0), FRotator::ZeroRotator,
		FVector2D(320.0, 180.0), FRotator::ZeroRotator, false);
	UXRLoadingScreenFunctionLibrary::ShowLoadingScreen();
	bSplashShown = true;
}

void UTN_VRSubsystem::HideLoadingSplash()
{
	if (!bSplashShown)
	{
		return;
	}
	bSplashShown = false;
	UXRLoadingScreenFunctionLibrary::HideLoadingScreen();
	UXRLoadingScreenFunctionLibrary::ClearLoadingScreenSplashes();
	SplashTexture = nullptr;
}

FString UTN_VRSubsystem::DescribeStatus() const
{
	static const TCHAR* const ModeNames[] = { TEXT("apagado"), TEXT("gafas"), TEXT("simulado") };
	const int32 ModeIndex = FMath::Clamp(static_cast<int32>(Mode), 0, 2);
	const bool bXR = GEngine && GEngine->XRSystem.IsValid();
	const FName Device = bXR ? UHeadMountedDisplayFunctionLibrary::GetHMDDeviceName() : NAME_None;
	const ATN_VRRig* ActiveRig = GetActiveRig();
	return FString::Printf(TEXT("Modo VR: %s · TN.VR=%d · OpenXR: %s · gafas conectadas: %s · estéreo: %s · dispositivo: %s · rig: %s · menú delante: %s"),
		ModeNames[ModeIndex], CVarTNVRMode.GetValueOnGameThread(),
		bXR ? TEXT("sí") : TEXT("no"),
		UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayConnected() ? TEXT("sí") : TEXT("no"),
		TNVRSubsystemDetail::IsStereoOn() ? TEXT("sí") : TEXT("no"),
		*Device.ToString(),
		ActiveRig ? *ActiveRig->GetName() : TEXT("ninguno"),
		ActiveRig && ActiveRig->IsMenuMode() ? TEXT("sí") : TEXT("no"))
		+ (ActiveRig ? TEXT(" · ") + ActiveRig->DescribeHands() : FString());
}

