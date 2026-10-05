#include "World/ProcMap/TN_SandStorm.h"

#include "World/ProcMap/TN_SandStormRules.h"
#include "World/ProcMap/TN_PathStorm.h"
#include "World/ProcMap/TN_ProcMapActorUtils.h"
#include "World/Beach/TN_BeachShelterVolume.h"
#include "Player/TN_StaminaComponent.h"
#include "Player/TortugaCharacter.h"
#include "Settings/TN_GameSettingsSubsystem.h"
#include "Core/TN_Log.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

namespace TNSandStormDetail
{
	/** Nombre del freno de la tormenta en UTN_StaminaComponent (no pisa a los demás topes). */
	const FName SpeedCapSource(TEXT("SandStorm"));

	/** Color de la niebla y tinte de la imagen con la tormenta en su punto (los del desierto de la tormenta de bañistas). */
	const FLinearColor FogColor(0.62f, 0.44f, 0.22f);
	const FLinearColor Tint(1.1f, 0.92f, 0.7f);
	constexpr float Saturation = 0.55f;
	constexpr float Vignette = 0.7f;

	/** Dentro de un búnker se ve menos tormenta (está fuera). */
	constexpr float ShelteredVisual = 0.35f;

	/** La ráfaga «llega» (suena) al pasar de este umbral. */
	constexpr float GustSoundThreshold = 0.5f;
}

ATN_SandStorm::ATN_SandStorm()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(1.f);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->SetupAttachment(Root);
	PostProcess->bUnbound = true;
	PostProcess->bEnabled = false;
	// Por encima del volumen del nivel; la tormenta de bañistas (prioridad 0) se mezcla encima si coinciden.
	PostProcess->Priority = 1.f;
}

void ATN_SandStorm::BeginPlay()
{
	Super::BeginPlay();
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	for (TActorIterator<AExponentialHeightFog> It(GetWorld()); It; ++It)
	{
		if (UExponentialHeightFogComponent* C = It->GetComponent()) { Fog = C; break; }
	}
	if (!Fog.IsValid())
	{
		// Sin niebla en el nivel, una propia (apagada hasta que llega la tormenta).
		UExponentialHeightFogComponent* Own = NewObject<UExponentialHeightFogComponent>(this, NAME_None, RF_Transient);
		Own->SetupAttachment(Root);
		Own->SetFogDensity(0.f);
		Own->RegisterComponent();
		Fog = Own;
	}
}

void ATN_SandStorm::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearSpeedCaps();
	RestoreFog();
	Super::EndPlay(EndPlayReason);
}

void ATN_SandStorm::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_SandStorm, Cycle);
}

void ATN_SandStorm::StartCycle(int32 InSeed)
{
	if (!HasAuthority())
	{
		return;
	}
	Cycle.Seed = InSeed;
	Cycle.StartServerTime = ServerNow();
	Cycle.bRunning = true;
	ForceNetUpdate();
	UE_LOG(LogTortunabo, Log, TEXT("[SandStorm] Ciclo de tormentas de arena con semilla %d: la primera, a los %.0f s."),
		InSeed, TNSandStorm::EventStart(static_cast<uint32>(InSeed), 0));
}

void ATN_SandStorm::StopCycle()
{
	if (!HasAuthority())
	{
		return;
	}
	Cycle.bRunning = false;
	ForceNetUpdate();
	OnRep_Cycle();
}

void ATN_SandStorm::DebugStartNow()
{
	if (!HasAuthority())
	{
		return;
	}
	if (!Cycle.bRunning)
	{
		Cycle.Seed = FMath::Rand();
		Cycle.bRunning = true;
	}
	// La tormenta Index siguiente empieza ya: se mueve el inicio del ciclo.
	int32 Index = 0;
	double Local = 0.0;
	const uint32 Seed = static_cast<uint32>(Cycle.Seed);
	const double Elapsed = ServerNow() - Cycle.StartServerTime;
	if (TNSandStorm::EventAt(Seed, Elapsed, Index, Local))
	{
		return;
	}
	while (TNSandStorm::EventStart(Seed, Index) < Elapsed) { ++Index; }
	Cycle.StartServerTime = ServerNow() - TNSandStorm::EventStart(Seed, Index);
	ForceNetUpdate();
}

void ATN_SandStorm::OnRep_Cycle()
{
	if (!Cycle.bRunning)
	{
		CurrentIntensity = 0.f;
		ClearSpeedCaps();
		ApplyLocalLook(0.f, 0.f);
	}
}

double ATN_SandStorm::ServerNow() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

void ATN_SandStorm::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	float Intensity = 0.f;
	float Gust = 0.f;
	FVector2D Wind = FVector2D::ZeroVector;
	if (Cycle.bRunning)
	{
		int32 Index = 0;
		double Local = 0.0;
		const uint32 Seed = static_cast<uint32>(Cycle.Seed);
		if (TNSandStorm::EventAt(Seed, ServerNow() - Cycle.StartServerTime, Index, Local))
		{
			Intensity = TNSandStorm::Intensity01(Local);
			Gust = TNSandStorm::Gust01(Seed, Index, Local);
			Wind = TNSandStorm::WindDir(Seed, Index);
		}
	}
	if ((Intensity > 0.f) != (CurrentIntensity > 0.f) && HasAuthority())
	{
		UE_LOG(LogTortunabo, Log, TEXT("[SandStorm] %s la tormenta de arena."), Intensity > 0.f ? TEXT("Empieza") : TEXT("Se va"));
	}
	CurrentIntensity = Intensity;
	ApplyToTurtles(DeltaSeconds, Intensity, Gust, Wind);
	ApplyLocalLook(Intensity, Gust);
	LastGust = Gust;
}

void ATN_SandStorm::ApplyToTurtles(float DeltaSeconds, float Intensity, float Gust, const FVector2D& Wind)
{
	if (Intensity <= 0.f)
	{
		ClearSpeedCaps();
		return;
	}
	const FVector WindDir(Wind.X, Wind.Y, 0.0);
	for (TActorIterator<ATortugaCharacter> It(GetWorld()); It; ++It)
	{
		ATortugaCharacter* Turtle = *It;
		UTN_StaminaComponent* Stamina = Turtle ? Turtle->FindComponentByClass<UTN_StaminaComponent>() : nullptr;
		if (!Stamina || !TNProcActors::SimulatesMovement(Turtle))
		{
			continue;
		}
		const bool bSheltered = ATN_BeachShelterVolume::IsSheltered(Turtle);
		// Freno: tope de velocidad mientras dura (dentro del búnker, ninguno).
		const float Cap = bSheltered ? -1.f : Stamina->GetWalkSpeed() * TNSandStorm::SpeedFactor(Intensity);
		float* Applied = AppliedCaps.Find(Turtle);
		if (Cap < 0.f)
		{
			if (Applied) { Stamina->ClearSpeedCap(TNSandStormDetail::SpeedCapSource); AppliedCaps.Remove(Turtle); }
		}
		else if (!Applied || FMath::Abs(*Applied - Cap) > 1.f)
		{
			Stamina->SetSpeedCap(TNSandStormDetail::SpeedCapSource, Cap);
			AppliedCaps.Add(Turtle, Cap);
		}
		// Empuje: solo andando por el suelo (en el aire, nadando o en la bola manda otra cosa) y fuera del búnker.
		UCharacterMovementComponent* Move = Turtle->GetCharacterMovement();
		if (bSheltered || Gust <= 0.f || !Move || Move->MovementMode != MOVE_Walking)
		{
			continue;
		}
		const float Along = static_cast<float>(FVector::DotProduct(Move->Velocity, WindDir));
		const float Accel = TNSandStorm::PushAcceleration(Along, Gust);
		if (Accel > 0.f)
		{
			Move->AddImpulse(WindDir * (Accel * DeltaSeconds), true);
		}
	}
}

void ATN_SandStorm::ClearSpeedCaps()
{
	for (const TPair<TWeakObjectPtr<ACharacter>, float>& Pair : AppliedCaps)
	{
		if (ACharacter* Turtle = Pair.Key.Get())
		{
			if (UTN_StaminaComponent* Stamina = Turtle->FindComponentByClass<UTN_StaminaComponent>())
			{
				Stamina->ClearSpeedCap(TNSandStormDetail::SpeedCapSource);
			}
		}
	}
	AppliedCaps.Reset();
}

bool ATN_SandStorm::PathStormOwnsFog(const FVector& ViewLocation) const
{
	for (TActorIterator<ATN_PathStorm> It(GetWorld()); It; ++It)
	{
		if (It->IsLocationInside(ViewLocation)) { return true; }
	}
	return false;
}

void ATN_SandStorm::ApplyLocalLook(float Intensity, float Gust)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	APlayerController* PC = World->GetFirstPlayerController();
	const FVector View = (PC && PC->PlayerCameraManager) ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
	const UTN_GameSettingsSubsystem* Settings = UTN_GameSettingsSubsystem::Get(this);
	float Weight = TNSandStorm::VisualWeight(Intensity, Settings ? Settings->GetWeatherEffects() : 1.f);
	if (PC && PC->GetPawn() && ATN_BeachShelterVolume::IsSheltered(PC->GetPawn()))
	{
		Weight *= TNSandStormDetail::ShelteredVisual;
	}

	// Niebla: se cierra con la tormenta y vuelve exactamente a como estaba. Si el jugador está dentro de la tormenta de
	// bañistas, la niebla es suya (la restaura ella a su estado original): esta no la toca.
	UExponentialHeightFogComponent* F = Fog.Get();
	if (F && Weight > 0.001f && !PathStormOwnsFog(View))
	{
		if (!bFogCached)
		{
			bFogCached = true;
			FogDensity0 = F->FogDensity;
			FogFalloff0 = F->FogHeightFalloff;
			FogStart0 = F->StartDistance;
			FogOpacity0 = F->FogMaxOpacity;
			FogColor0 = F->FogInscatteringLuminance;
		}
		F->SetFogDensity(FMath::Lerp(FogDensity0, TNSandStorm::FOG_DENSITY, Weight));
		F->SetFogHeightFalloff(FMath::Lerp(FogFalloff0, 0.002f, Weight));
		F->SetStartDistance(FMath::Lerp(FogStart0, 0.f, Weight));
		F->SetFogMaxOpacity(FMath::Lerp(FogOpacity0, 1.f, Weight));
		F->SetFogInscatteringColor(FMath::Lerp(FogColor0, TNSandStormDetail::FogColor, Weight));
		bFogApplied = true;
	}
	else if (F && Weight > 0.001f)
	{
		bFogApplied = false;
	}
	else
	{
		RestoreFog();
	}

	PostProcess->bEnabled = Weight > 0.001f;
	PostProcess->BlendWeight = Weight;
	FPostProcessSettings& S = PostProcess->Settings;
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(TNSandStormDetail::Saturation, TNSandStormDetail::Saturation, TNSandStormDetail::Saturation, 1.f);
	S.bOverride_ColorGain = true;
	S.ColorGain = FVector4(TNSandStormDetail::Tint.R, TNSandStormDetail::Tint.G, TNSandStormDetail::Tint.B, 1.f);
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = TNSandStormDetail::Vignette;

	if (GustSound && Gust >= TNSandStormDetail::GustSoundThreshold && LastGust < TNSandStormDetail::GustSoundThreshold)
	{
		UGameplayStatics::SpawnSound2D(this, GustSound, Gust);
	}
}

void ATN_SandStorm::RestoreFog()
{
	if (!bFogApplied)
	{
		return;
	}
	bFogApplied = false;
	if (UExponentialHeightFogComponent* F = Fog.Get())
	{
		F->SetFogDensity(FogDensity0);
		F->SetFogHeightFalloff(FogFalloff0);
		F->SetStartDistance(FogStart0);
		F->SetFogMaxOpacity(FogOpacity0);
		F->SetFogInscatteringColor(FogColor0);
	}
}

#if !UE_BUILD_SHIPPING
namespace TNSandStormDetail
{
	/** Pruebas (en el anfitrión): «TN.SandStorm now» la trae ya, «off» para el ciclo y «on» lo arranca con otra semilla. */
	void RunDebugCommand(const TArray<FString>& Args, UWorld* World)
	{
		ATN_SandStorm* Storm = nullptr;
		for (TActorIterator<ATN_SandStorm> It(World); It; ++It) { Storm = *It; break; }
		if (!Storm || !Storm->HasAuthority())
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[SandStorm] TN.SandStorm: solo en el anfitrión de una partida coop del mapa procedural."));
			return;
		}
		const FString Arg = Args.Num() > 0 ? Args[0].ToLower() : FString(TEXT("now"));
		if (Arg == TEXT("off"))
		{
			Storm->StopCycle();
		}
		else if (Arg == TEXT("on"))
		{
			Storm->StartCycle(FMath::Rand());
		}
		else
		{
			Storm->DebugStartNow();
		}
	}

	FAutoConsoleCommandWithWorldAndArgs DebugCommand(TEXT("TN.SandStorm"),
		TEXT("Tormenta de arena del coop (#790), en el anfitrión: now (por defecto) la trae ya, off para el ciclo, on lo arranca con otra semilla."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunDebugCommand));
}
#endif
