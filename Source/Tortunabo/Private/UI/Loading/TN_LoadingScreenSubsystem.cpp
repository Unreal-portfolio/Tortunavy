#include "UI/Loading/TN_LoadingScreenSubsystem.h"
#include "Audio/TN_AudioVoices.h"
#include "Multiplayer/TN_LocalViews.h"

#include "STN_EggLoadingScreen.h"
#include "Core/TN_CoopGameState.h"
#include "Core/TN_LocText.h"
#include "Core/TN_Log.h"
#include "Core/TN_MatchFlowTypes.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Lobby/TN_HQGameMode.h"
#include "Misc/App.h"
#include "MoviePlayer.h"
#include "Sound/SoundGenerator.h"
#include "UObject/UObjectGlobals.h"
#include <atomic>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// Sonido del huevo (hilo de audio: C++ puro, sin UObjects ni asignaciones)
// ─────────────────────────────────────────────────────────────────────────────

namespace TNEggAudio
{
	struct FEggSharedParams
	{
		std::atomic<int32> CrackCount{ 0 };
		std::atomic<float> CrackStrength{ 0.7f };
		std::atomic<int32> PopCount{ 0 };
		std::atomic<int32> KnockCount{ 0 };
		std::atomic<float> KnockStrength{ 1.f };
		std::atomic<int32> WhooshCount{ 0 };
		std::atomic<float> WhooshStrength{ 1.f };
		std::atomic<float> Volume{ 0.85f };
		/**
		 * Cuentas ya oídas cuando el sintetizador se volvió a arrancar (UTN_EggSynthComponent::KeepAwake): un generador
		 * nuevo empieza por ellas y solo suena lo que se pida después, no lo que ya sonó antes de pararse.
		 */
		std::atomic<int32> BaseCrack{ 0 };
		std::atomic<int32> BasePop{ 0 };
		std::atomic<int32> BaseKnock{ 0 };
		std::atomic<int32> BaseWhoosh{ 0 };
	};

	constexpr float TwoPi = 6.2831853f;

	class FEggEngine
	{
	public:
		void Init(float InSampleRate, const FEggSharedParams& Params)
		{
			SampleRate = FMath::Max(8000.f, InSampleRate);
			SetCrackTone(2800.f);
			SeenCrack = Params.BaseCrack.load(std::memory_order_relaxed);
			SeenPop = Params.BasePop.load(std::memory_order_relaxed);
			SeenKnock = Params.BaseKnock.load(std::memory_order_relaxed);
			SeenWhoosh = Params.BaseWhoosh.load(std::memory_order_relaxed);
		}

		void Render(float* Out, int32 Frames, int32 Channels, FEggSharedParams& Params)
		{
			const int32 CrackNow = Params.CrackCount.load(std::memory_order_relaxed);
			if (CrackNow != SeenCrack)
			{
				SeenCrack = CrackNow;
				StartCrack(Params.CrackStrength.load(std::memory_order_relaxed));
			}
			const int32 PopNow = Params.PopCount.load(std::memory_order_relaxed);
			if (PopNow != SeenPop)
			{
				SeenPop = PopNow;
				PopT = 0.f;
				PopPhase = 0.f;
				bPopActive = true;
			}
			const int32 KnockNow = Params.KnockCount.load(std::memory_order_relaxed);
			if (KnockNow != SeenKnock)
			{
				SeenKnock = KnockNow;
				KnockT = 0.f;
				KnockPhase = 0.f;
				KnockAmp = FMath::Clamp(Params.KnockStrength.load(std::memory_order_relaxed), 0.f, 1.f);
			}
			const int32 WhooshNow = Params.WhooshCount.load(std::memory_order_relaxed);
			if (WhooshNow != SeenWhoosh)
			{
				SeenWhoosh = WhooshNow;
				WhooshT = 0.f;
				WhooshAmp = FMath::Clamp(Params.WhooshStrength.load(std::memory_order_relaxed), 0.f, 1.f);
			}
			const float Gain = Params.Volume.load(std::memory_order_relaxed);
			const float Dt = 1.f / SampleRate;

			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				float Sample = 0.f;
				if (CrackT >= 0.f)
				{
					Sample += RenderCrack(Dt);
				}
				if (bPopActive)
				{
					Sample += RenderPop(Dt);
				}
				if (KnockT >= 0.f)
				{
					Sample += RenderKnock(Dt);
				}
				if (WhooshT >= 0.f)
				{
					Sample += RenderWhoosh(Dt);
				}
				const float Shaped = static_cast<float>(std::tanh(Sample * 1.1f)) * Gain * 0.8f;
				for (int32 Ch = 0; Ch < Channels; ++Ch)
				{
					Out[Frame * Channels + Ch] = Shaped;
				}
			}
		}

	private:
		float Noise()
		{
			RandState ^= RandState << 13;
			RandState ^= RandState >> 17;
			RandState ^= RandState << 5;
			return static_cast<float>(RandState & 0xFFFFFF) / 8388607.5f - 1.f;
		}

		void SetCrackTone(float Hz)
		{
			CrackF = 2.f * FMath::Sin(UE_PI * FMath::Min(Hz, SampleRate * 0.2f) / SampleRate);
		}

		void StartCrack(float Strength)
		{
			CrackT = 0.f;
			CrackAmp = FMath::Clamp(Strength, 0.f, 1.f);
			const float R0 = 0.5f + 0.5f * Noise();
			const float R1 = 0.5f + 0.5f * Noise();
			const float R2 = 0.5f + 0.5f * Noise();
			// Cada crujido suena un poco distinto: cáscara más o menos aguda.
			SetCrackTone(2200.f + 1600.f * R2);
			ClickAt[0] = 0.012f + 0.01f * R0;
			ClickAt[1] = 0.03f + 0.02f * R1;
			ClickAt[2] = 0.055f + 0.02f * R2;
			ClickAmp[0] = 0.5f + 0.3f * R1;
			ClickAmp[1] = 0.35f + 0.25f * R2;
			ClickAmp[2] = 0.25f + 0.2f * R0;
		}

		/** Crujido: ruido en banda aguda con un golpe inicial, tres chasquidos cortos detrás y un poco de cuerpo grave. */
		float RenderCrack(float Dt)
		{
			const float Nz = Noise();
			CrackLow += CrackF * CrackBand;
			const float CrackHigh = Nz - CrackLow - 0.8f * CrackBand;
			CrackBand += CrackF * CrackHigh;
			CrunchLow += (Nz - CrunchLow) * 0.08f;
			float Env = FMath::Exp(-CrackT / 0.018f);
			for (int32 k = 0; k < 3; ++k)
			{
				const float Tk = CrackT - ClickAt[k];
				if (Tk >= 0.f)
				{
					Env += ClickAmp[k] * FMath::Exp(-Tk / 0.006f);
				}
			}
			const float Crunch = CrunchLow * FMath::Exp(-CrackT / 0.009f) * 2.2f;
			CrackT += Dt;
			if (CrackT > 0.2f)
			{
				CrackT = -1.f;
			}
			return (CrackBand * Env * 1.7f + Crunch) * CrackAmp;
		}

		/** «¡Pum!»: golpe grave que cae de tono, soplido y chasquido de arranque. */
		float RenderPop(float Dt)
		{
			const float Freq = 52.f + 140.f * FMath::Exp(-PopT / 0.07f);
			PopPhase += TwoPi * Freq * Dt;
			if (PopPhase > TwoPi)
			{
				PopPhase -= TwoPi;
			}
			const float Attack = 1.f - FMath::Exp(-PopT / 0.0025f);
			const float Body = FMath::Sin(PopPhase) * Attack * FMath::Exp(-PopT / 0.24f);
			const float Nz = Noise();
			PopLow += (Nz - PopLow) * 0.12f;
			const float Puff = PopLow * FMath::Exp(-PopT / 0.05f) * 1.4f;
			const float Click = Nz * FMath::Exp(-PopT / 0.004f) * 0.5f;
			PopT += Dt;
			if (PopT > 1.f)
			{
				bPopActive = false;
			}
			return Body * 0.95f + Puff + Click;
		}

		/** «¡Clac!»: las dos mitades chocan (golpe grave que baja de tono y chasquido) y rebotan una vez. */
		float RenderKnock(float Dt)
		{
			const float Rebound = KnockT - 0.055f;
			const float SinceHit = Rebound >= 0.f ? Rebound : KnockT;
			const float Freq = 92.f + 90.f * FMath::Exp(-SinceHit / 0.025f);
			KnockPhase += TwoPi * Freq * Dt;
			if (KnockPhase > TwoPi)
			{
				KnockPhase -= TwoPi;
			}
			float BodyEnv = FMath::Exp(-KnockT / 0.05f);
			float ClickEnv = FMath::Exp(-KnockT / 0.0035f);
			if (Rebound >= 0.f)
			{
				BodyEnv += 0.4f * FMath::Exp(-Rebound / 0.035f);
				ClickEnv += 0.45f * FMath::Exp(-Rebound / 0.003f);
			}
			const float Nz = Noise();
			const float Bright = Nz - KnockLow;
			KnockLow += (Nz - KnockLow) * 0.3f;
			KnockT += Dt;
			if (KnockT > 0.4f)
			{
				KnockT = -1.f;
			}
			return KnockAmp * (FMath::Sin(KnockPhase) * BodyEnv * 0.95f + Bright * ClickEnv * 0.7f);
		}

		/** «Fiuu»: ruido en una banda que sube de tono, con entrada rápida y cola que se apaga. */
		float RenderWhoosh(float Dt)
		{
			const float Progress = WhooshT / 0.5f;
			const float Fc = 260.f + 1500.f * Progress;
			const float Wf = 2.f * FMath::Sin(UE_PI * FMath::Min(Fc, SampleRate * 0.2f) / SampleRate);
			const float Nz = Noise();
			WhooshLow += Wf * WhooshBand;
			const float WhooshHigh = Nz - WhooshLow - 0.7f * WhooshBand;
			WhooshBand += Wf * WhooshHigh;
			const float Env = FMath::Min(1.f, WhooshT / 0.07f) * FMath::Square(FMath::Max(0.f, 1.f - Progress));
			WhooshT += Dt;
			if (WhooshT > 0.5f)
			{
				WhooshT = -1.f;
			}
			return WhooshBand * Env * WhooshAmp * 1.2f;
		}

		float SampleRate = 48000.f;
		// Crujido
		float CrackF = 0.3f;
		float CrackLow = 0.f;
		float CrackBand = 0.f;
		float CrunchLow = 0.f;
		float CrackT = -1.f;
		float CrackAmp = 0.f;
		float ClickAt[3] = { 0.f, 0.f, 0.f };
		float ClickAmp[3] = { 0.f, 0.f, 0.f };
		// «¡Pum!»
		bool bPopActive = false;
		float PopT = 0.f;
		float PopPhase = 0.f;
		float PopLow = 0.f;
		// «¡Clac!»
		float KnockT = -1.f;
		float KnockPhase = 0.f;
		float KnockLow = 0.f;
		float KnockAmp = 1.f;
		// «Fiuu»
		float WhooshT = -1.f;
		float WhooshLow = 0.f;
		float WhooshBand = 0.f;
		float WhooshAmp = 1.f;

		int32 SeenCrack = 0;
		int32 SeenPop = 0;
		int32 SeenKnock = 0;
		int32 SeenWhoosh = 0;
		uint32 RandState = 0x9E3779B9u;
	};

	class FTNEggSynthGenerator final : public ISoundGenerator
	{
	public:
		FTNEggSynthGenerator(float InSampleRate, int32 InNumChannels, const TSharedPtr<FEggSharedParams, ESPMode::ThreadSafe>& InParams)
			: Params(InParams)
			, OutChannels(FMath::Max(1, InNumChannels))
		{
			DspEngine.Init(InSampleRate, *Params);
		}

		virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
		{
			const int32 Frames = NumSamples / OutChannels;
			DspEngine.Render(OutAudio, Frames, OutChannels, *Params);
			for (int32 i = Frames * OutChannels; i < NumSamples; ++i)
			{
				OutAudio[i] = 0.f;
			}
			return NumSamples;
		}

	private:
		TSharedPtr<FEggSharedParams, ESPMode::ThreadSafe> Params;
		int32 OutChannels = 1;
		FEggEngine DspEngine;
	};
}

UTN_EggSynthComponent::UTN_EggSynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// El tick solo cuenta el silencio para parar el sintetizador, dos veces por segundo; también con la partida parada
	// (el huevo de la interfaz suena entre mapas y con el menú de pausa de la partida local).
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.5f;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	bAutoActivate = false;
	NumChannels = 1;
	bAllowSpatialization = false;
	SharedParams = MakeShared<TNEggAudio::FEggSharedParams, ESPMode::ThreadSafe>();
}

void UTN_EggSynthComponent::KeepAwake()
{
	if (!SharedParams.IsValid())
	{
		return;
	}
	IdleLeft = IdleStopSeconds;
	if (!IsActive() && IsRegistered())
	{
		// El generador nuevo empieza por lo que ya se ha pedido: solo suena lo que venga a partir de aquí.
		TNEggAudio::FEggSharedParams& P = *SharedParams;
		P.BaseCrack.store(P.CrackCount.load(std::memory_order_relaxed), std::memory_order_relaxed);
		P.BasePop.store(P.PopCount.load(std::memory_order_relaxed), std::memory_order_relaxed);
		P.BaseKnock.store(P.KnockCount.load(std::memory_order_relaxed), std::memory_order_relaxed);
		P.BaseWhoosh.store(P.WhooshCount.load(std::memory_order_relaxed), std::memory_order_relaxed);
		TNAudioVoices::Apply(*this, bAllowSpatialization ? TNAudioVoices::ERank::World : TNAudioVoices::ERank::Reserved);
		Start();
	}
	SetComponentTickEnabled(true);
}

void UTN_EggSynthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	IdleLeft -= DeltaTime;
	if (IdleLeft <= 0.f)
	{
		// Callado: se libera la voz del mezclador hasta el próximo sonido.
		if (IsActive())
		{
			Stop();
		}
		SetComponentTickEnabled(false);
	}
}

void UTN_EggSynthComponent::PlayCrack(float Strength)
{
	if (SharedParams.IsValid())
	{
		KeepAwake();
		SharedParams->CrackStrength.store(FMath::Clamp(Strength, 0.f, 1.f), std::memory_order_relaxed);
		SharedParams->CrackCount.fetch_add(1, std::memory_order_relaxed);
	}
}

void UTN_EggSynthComponent::PlayPop()
{
	if (SharedParams.IsValid())
	{
		KeepAwake();
		SharedParams->PopCount.fetch_add(1, std::memory_order_relaxed);
	}
}

void UTN_EggSynthComponent::PlayKnock(float Strength)
{
	if (SharedParams.IsValid())
	{
		KeepAwake();
		SharedParams->KnockStrength.store(FMath::Clamp(Strength, 0.f, 1.f), std::memory_order_relaxed);
		SharedParams->KnockCount.fetch_add(1, std::memory_order_relaxed);
	}
}

void UTN_EggSynthComponent::PlayWhoosh(float Strength)
{
	if (SharedParams.IsValid())
	{
		KeepAwake();
		SharedParams->WhooshStrength.store(FMath::Clamp(Strength, 0.f, 1.f), std::memory_order_relaxed);
		SharedParams->WhooshCount.fetch_add(1, std::memory_order_relaxed);
	}
}

bool UTN_EggSynthComponent::Init(int32& /*SampleRate*/)
{
	NumChannels = 1;
	return true;
}

ISoundGeneratorPtr UTN_EggSynthComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	return MakeShared<TNEggAudio::FTNEggSynthGenerator, ESPMode::ThreadSafe>(InParams.SampleRate, InParams.NumChannels, SharedParams);
}

// ─────────────────────────────────────────────────────────────────────────────
// Consola
// ─────────────────────────────────────────────────────────────────────────────

namespace TNLoadingConsole
{
	UTN_LoadingScreenSubsystem* Find(UWorld* World)
	{
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UTN_LoadingScreenSubsystem>() : nullptr;
	}

	void Test(UWorld* World)
	{
		if (UTN_LoadingScreenSubsystem* Loading = Find(World))
		{
			Loading->SetHold(false);
			Loading->BeginLoading(TEXT("Prueba de carga"));
			Loading->BreakAfter(3.f);
		}
	}

	void CloseAndWait(UWorld* World)
	{
		if (UTN_LoadingScreenSubsystem* Loading = Find(World))
		{
			Loading->SetHold(true);
			Loading->BeginLoading(TEXT("Prueba de carga (TN.Loading.Test.Break lo rompe y TN.Loading.Test.Open lo abre)"));
		}
	}

	void BreakEgg(UWorld* World)
	{
		if (UTN_LoadingScreenSubsystem* Loading = Find(World))
		{
			Loading->SetHold(false);
			Loading->BreakNow();
		}
	}

	void OpenEgg(UWorld* World)
	{
		if (UTN_LoadingScreenSubsystem* Loading = Find(World))
		{
			Loading->OpenNow();
		}
	}

	void TestGo(UWorld* World)
	{
		if (UTN_LoadingScreenSubsystem* Loading = Find(World))
		{
			Loading->SetHold(false);
			Loading->BeginLoading(TEXT("Prueba de salida"));
			Loading->BreakAfter(2.f, true);
		}
	}

	void TestGoOnly(UWorld* World)
	{
		if (UTN_LoadingScreenSubsystem* Loading = Find(World))
		{
			Loading->ShowGoBanner();
		}
	}

	FAutoConsoleCommandWithWorld TestCommand(TEXT("TN.Loading.Test"),
		TEXT("Cierra el huevo de la pantalla de carga y lo rompe a los 3 s."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&Test));
	FAutoConsoleCommandWithWorld CloseCommand(TEXT("TN.Loading.Test.Close"),
		TEXT("Cierra el huevo como al pulsar Host y lo deja cerrado hasta TN.Loading.Test.Break o TN.Loading.Test.Open."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&CloseAndWait));
	FAutoConsoleCommandWithWorld HoldCommand(TEXT("TN.Loading.Test.Hold"),
		TEXT("Igual que TN.Loading.Test.Close: cierra el huevo y lo deja cerrado."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&CloseAndWait));
	FAutoConsoleCommandWithWorld BreakCommand(TEXT("TN.Loading.Test.Break"),
		TEXT("Rompe el huevo de la pantalla de carga que esté a la vista."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&BreakEgg));
	FAutoConsoleCommandWithWorld OpenCommand(TEXT("TN.Loading.Test.Open"),
		TEXT("Abre sin romperlo el huevo que esté a la vista (como al cancelarse la cuenta atrás del lobby)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&OpenEgg));
	FAutoConsoleCommandWithWorld GoCommand(TEXT("TN.Loading.Test.Go"),
		TEXT("Cierra el huevo y a los 2 s lo rompe con «¡ADELANTE!» y una frase, como al empezar la ronda del mapa procedural."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&TestGo));
	FAutoConsoleCommandWithWorld GoOnlyCommand(TEXT("TN.Loading.Test.GoOnly"),
		TEXT("Enseña «¡ADELANTE!» solo, sin huevo, como al empezar las rondas siguientes del mapa procedural."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&TestGoOnly));
}

// ─────────────────────────────────────────────────────────────────────────────
// Subsistema
// ─────────────────────────────────────────────────────────────────────────────

namespace TNLoadingTimes
{
	/** Esperas máximas con el huevo cerrado sin que empiece el viaje (luego se abre y avisa en el registro). */
	constexpr double HoldTravelSeconds = 45.0;
	constexpr double HoldLobbySeconds = 60.0;
	constexpr double HoldResultsSeconds = 20.0;
	/** Tras cargar: respiro mínimo para que el primer fotograma del mapa nuevo ya esté pintado, y tope total. */
	constexpr double MinAfterLoadSeconds = 0.6;
	constexpr double MaxAfterLoadSeconds = 45.0;
	/** Cancelación de la cuenta atrás del lobby que tiene que durar esto para abrir el huevo. */
	constexpr double LobbyCancelSeconds = 0.25;
	/** Como mucho, lo que espera un viaje pedido con RunWhenClosed. */
	constexpr double MaxWaitForCloseSeconds = 1.5;
}

namespace TNLoadingLayers
{
	/** Orden en el viewport: el huevo tapa todo (HUD y menús incluidos); «¡ADELANTE!» sin huevo va justo debajo. */
	constexpr int32 EggZOrder = 20000;
	constexpr int32 GoBannerZOrder = EggZOrder - 10;
}

bool UTN_LoadingScreenSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void UTN_LoadingScreenSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PreLoadHandle = FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(this, &UTN_LoadingScreenSubsystem::HandlePreLoadMap);
	PostLoadHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UTN_LoadingScreenSubsystem::HandlePostLoadMap);
	SeamlessStartHandle = FWorldDelegates::OnSeamlessTravelStart.AddUObject(this, &UTN_LoadingScreenSubsystem::HandleSeamlessTravelStart);
	// El arte se genera ya (unas décimas de segundo, una vez por ejecución): al pulsar Host el huevo sale en el acto.
	if (FApp::CanEverRender() && !IsRunningCommandlet())
	{
		TNEggLoadingArt::Warm();
	}
}

void UTN_LoadingScreenSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PreLoadMapWithContext.Remove(PreLoadHandle);
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadHandle);
	FWorldDelegates::OnSeamlessTravelStart.Remove(SeamlessStartHandle);
	PendingWhenClosed.Reset();
	RemoveGoBanner();
	RemoveFromViewport();
	Screen.Reset();
	Super::Deinitialize();
}

ETickableTickType UTN_LoadingScreenSubsystem::GetTickableTickType() const
{
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Conditional;
}

bool UTN_LoadingScreenSubsystem::IsTickable() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance && GameInstance->GetGameViewportClient();
}

TStatId UTN_LoadingScreenSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTN_LoadingScreenSubsystem, STATGROUP_Tickables);
}

UWorld* UTN_LoadingScreenSubsystem::GetTickableGameObjectWorld() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetWorld() : nullptr;
}

FString UTN_LoadingScreenSubsystem::FriendlyStatusForMap(const FString& MapName)
{
	// Textos ya traducidos al idioma de esta máquina: la pantalla de carga los recibe como FString.
	if (MapName.Contains(TEXT("Menu")))
	{
		return NSLOCTEXT("TNLoading", "StatusBackToMenu", "Volviendo al menú").ToString();
	}
	if (MapName.Contains(TEXT("HQ")) || MapName.Contains(TEXT("Lobby")))
	{
		return NSLOCTEXT("TNLoading", "StatusToHeadquarters", "Rumbo al cuartel").ToString();
	}
	if (MapName.Contains(TEXT("LVL_Demo")))
	{
		return NSLOCTEXT("TNLoading", "StatusHatching", "Incubando la partida").ToString();
	}
	return NSLOCTEXT("TNLoading", "StatusLoading", "Cargando").ToString();
}

FString UTN_LoadingScreenSubsystem::CleanStatus(const FString& InStatus)
{
	// Los puntos suspensivos los anima la pantalla («Reconectando... (intento 2)» → «Reconectando (intento 2)»).
	FString Clean = InStatus.Replace(TEXT("..."), TEXT("")).Replace(TEXT("…"), TEXT(""));
	Clean.ReplaceInline(TEXT("  "), TEXT(" "));
	Clean.TrimStartAndEndInline();
	while (Clean.EndsWith(TEXT(".")))
	{
		Clean.LeftChopInline(1);
	}
	return Clean;
}

bool UTN_LoadingScreenSubsystem::IsLobbyWorld(const UWorld* World)
{
	if (!World)
	{
		return false;
	}
	const AGameStateBase* BaseState = World->GetGameState();
	if (BaseState && BaseState->GameModeClass && BaseState->GameModeClass->IsChildOf(ATN_HQGameMode::StaticClass()))
	{
		return true;
	}
	const FString MapName = World->GetMapName();
	return MapName.Contains(TEXT("HQ")) || MapName.Contains(TEXT("Lobby"));
}

void UTN_LoadingScreenSubsystem::HandlePreLoadMap(const FWorldContext& WorldContext, const FString& MapName)
{
	if (WorldContext.OwningGameInstance != GetGameInstance())
	{
		return;
	}
	const UWorld* OldWorld = WorldContext.World();
	if (OldWorld && OldWorld->IsInSeamlessTravel())
	{
		// Los viajes sin cortes van por HandleSeamlessTravelStart.
		return;
	}
	RemoveGoBanner();
	LoadingMapName = MapName;
	bInHardLoad = true;
	HardLoadStartTime = FPlatformTime::Seconds();
	// El viaje que se esperaba (Host, Join, cuenta atrás...) ya ha empezado; las esperas de prueba no lo retienen.
	HoldReason = TNEggLoading::EHold::None;
	LobbyCancelSince = -1.0;
	bHold = false;
	BreakAtTime = -1.0;
	// Primera carga del juego (aún no había partida): «Cargando»; si no, el texto amable del mapa de destino.
	bKeepLoadStatus = !(OldWorld && OldWorld->HasBegunPlay());
	const FString LoadStatus = bKeepLoadStatus ? NSLOCTEXT("TNLoading", "StatusLoading", "Cargando").ToString() : FriendlyStatusForMap(MapName);
	// Un viaje que esperaba a que el huevo se cerrara (RunWhenClosed) ya no toca: está empezando otro.
	PendingWhenClosed.Reset();
	// Cerrado del todo ya: durante el LoadMap no se repinta nada más.
	BeginLoading(LoadStatus, true);
	if (!Screen.IsValid())
	{
		return;
	}

	if (!GIsEditor && IsMoviePlayerEnabled() && GetMoviePlayer())
	{
		// Fuera del editor MoviePlayer pinta el mismo huevo (misma línea de unión, mismas tortugas) en su hilo.
		FLoadingScreenAttributes Attributes;
		Attributes.bAutoCompleteWhenLoadingCompletes = true;
		Attributes.bMoviesAreSkippable = false;
		Attributes.bWaitForManualStop = false;
		Attributes.MinimumLoadingScreenDisplayTime = 0.f;
		Attributes.WidgetLoadingScreen = SNew(STN_EggLoadingScreen)
			.StartClosed(true)
			.Status(Screen->GetStatus())
			.Seed(Screen->GetSeed())
			.TimeOrigin(Screen->GetTimeOrigin());
		GetMoviePlayer()->SetupLoadingScreen(Attributes);
	}
	else if (FSlateApplication::IsInitialized() && !FSlateApplication::Get().IsTicking())
	{
		// Sin MoviePlayer (PIE) el viewport se congela durante el LoadMap con el último fotograma pintado: se pinta uno
		// más ya con el huevo cerrado del todo y el texto nuevo (como la pantalla de carga de Lyra, pero sin procesar
		// la entrada en mitad del LoadMap).
		FSlateApplication::Get().Tick(ESlateTickType::TimeAndWidgets);
	}
}

void UTN_LoadingScreenSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	if (LoadedWorld && LoadedWorld->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	bInHardLoad = false;
	bKeepLoadStatus = false;
	if (Screen.IsValid())
	{
		// LoadMap puede haber vaciado el viewport: se vuelve a poner encima.
		RemoveFromViewport();
		AddToViewport();
	}
}

void UTN_LoadingScreenSubsystem::HandleSeamlessTravelStart(UWorld* World, const FString& MapName)
{
	if (!World || World->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	// Viaje sin cortes (lobby → partida, resultados → cuartel): el viaje que se esperaba ya ha empezado.
	HoldReason = TNEggLoading::EHold::None;
	LobbyCancelSince = -1.0;
	bHold = false;
	BreakAtTime = -1.0;
	PendingWhenClosed.Reset();
	RemoveGoBanner();
	LoadingMapName = MapName;
	BeginLoading(FriendlyStatusForMap(MapName));
	LoadDoneTime = -1.0;
}

void UTN_LoadingScreenSubsystem::BeginLoading(const FString& InStatus, bool bStartClosed)
{
	if (IsRunningDedicatedServer() || !FSlateApplication::IsInitialized())
	{
		return;
	}
	const FString Clean = CleanStatus(InStatus);
	if (Screen.IsValid() && !Screen->IsBreaking())
	{
		if (bStartClosed)
		{
			Screen->SnapClosed();
		}
		else
		{
			// Si se estaba abriendo, se da la vuelta y se cierra desde donde esté.
			Screen->Close();
		}
		if (!Clean.IsEmpty())
		{
			Screen->SetStatus(TNLocText::Literal(Clean));
		}
		AddToViewport();
		return;
	}
	if (Screen.IsValid())
	{
		// Se estaba rompiendo y empieza otra carga: huevo nuevo.
		RemoveFromViewport();
		Screen.Reset();
	}
	Screen = SNew(STN_EggLoadingScreen)
		.StartClosed(bStartClosed)
		.Status(TNLocText::Literal(Clean));
	LoadDoneTime = -1.0;
	BreakAtTime = -1.0;
	FiredBreakCues = 0;
	FiredKnockSerial = -1;
	LastAutoStatus.Reset();
	AddToViewport();
}

bool UTN_LoadingScreenSubsystem::CloseForTravel(const FString& InStatus)
{
	if (IsRunningDedicatedServer() || !FSlateApplication::IsInitialized())
	{
		return false;
	}
	const UGameInstance* GameInstance = GetGameInstance();
	const UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (bInHardLoad || (World && World->IsInSeamlessTravel()))
	{
		// Ya se está viajando: el huevo ya está (o se pone) cerrado y solo cambia el texto.
		BeginLoading(bKeepLoadStatus ? FString() : InStatus, bInHardLoad);
	}
	else
	{
		CloseAndHold(TNEggLoading::EHold::Travel, InStatus);
	}
	return Screen.IsValid();
}

void UTN_LoadingScreenSubsystem::CloseAndHold(TNEggLoading::EHold Reason, const FString& InStatus)
{
	BeginLoading(InStatus);
	if (Screen.IsValid())
	{
		HoldReason = Reason;
		HoldStartTime = FPlatformTime::Seconds();
		LoadDoneTime = -1.0;
	}
}

void UTN_LoadingScreenSubsystem::CancelPendingClose()
{
	if (HoldReason == TNEggLoading::EHold::Travel)
	{
		OpenNow();
	}
}

void UTN_LoadingScreenSubsystem::RunWhenClosed(TFunction<void()> Action)
{
	if (!Action)
	{
		return;
	}
	const bool bWaiting = Screen.IsValid() && ScreenInViewport.IsValid() && !Screen->IsBreaking() && !Screen->IsOpening() && !Screen->IsSettled();
	if (!bWaiting)
	{
		Action();
		return;
	}
	if (PendingWhenClosed.Num() == 0)
	{
		PendingSince = FPlatformTime::Seconds();
	}
	PendingWhenClosed.Add(MoveTemp(Action));
}

void UTN_LoadingScreenSubsystem::FlushWhenClosed(bool bForce)
{
	if (PendingWhenClosed.Num() == 0)
	{
		return;
	}
	const bool bReady = bForce || !Screen.IsValid() || Screen->IsBreaking() || Screen->IsOpening() || Screen->IsSettled()
		|| FPlatformTime::Seconds() - PendingSince > TNLoadingTimes::MaxWaitForCloseSeconds;
	if (!bReady)
	{
		return;
	}
	TArray<TFunction<void()>> Ready = MoveTemp(PendingWhenClosed);
	PendingWhenClosed.Reset();
	for (TFunction<void()>& Pending : Ready)
	{
		if (Pending)
		{
			Pending();
		}
	}
}

void UTN_LoadingScreenSubsystem::SetStatus(const FString& InStatus)
{
	const FString Clean = CleanStatus(InStatus);
	if (Screen.IsValid() && !Clean.IsEmpty())
	{
		Screen->SetStatus(TNLocText::Literal(Clean));
	}
}

void UTN_LoadingScreenSubsystem::BreakNow(bool bWithGo)
{
	if (Screen.IsValid() && !Screen->IsBreaking())
	{
		if (!Screen->IsSettled())
		{
			Screen->SnapClosed();
		}
		HoldReason = TNEggLoading::EHold::None;
		BreakAtTime = -1.0;
		FiredBreakCues = 0;
		Screen->StartBreak(bWithGo);
		if (bWithGo)
		{
			// El «¡ADELANTE!» lo pone el huevo: nunca dos a la vez.
			RemoveGoBanner();
		}
		EnsureSynth();
		FlushWhenClosed(true);
	}
}

void UTN_LoadingScreenSubsystem::OpenNow()
{
	HoldReason = TNEggLoading::EHold::None;
	LobbyCancelSince = -1.0;
	bHold = false;
	BreakAtTime = -1.0;
	if (Screen.IsValid() && !Screen->IsBreaking() && !Screen->IsOpening())
	{
		Screen->Open();
		if (UTN_EggSynthComponent* EggSynth = EnsureSynth())
		{
			EggSynth->PlayWhoosh(0.45f);
		}
	}
	FlushWhenClosed(true);
}

void UTN_LoadingScreenSubsystem::BreakAfter(float Seconds, bool bWithGo)
{
	BreakAtTime = FPlatformTime::Seconds() + FMath::Max(0.f, Seconds);
	bBreakAtWithGo = bWithGo;
}

void UTN_LoadingScreenSubsystem::ShowGoBanner(float DelaySeconds)
{
	if (IsRunningDedicatedServer() || !FSlateApplication::IsInitialized())
	{
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	UGameViewportClient* Viewport = GameInstance ? GameInstance->GetGameViewportClient() : nullptr;
	if (!Viewport)
	{
		return;
	}
	RemoveGoBanner();
	GoBanner = SNew(STN_GoBanner).Delay(DelaySeconds);
	// Justo debajo del huevo, por encima del HUD y de los menús; no recibe clics.
	Viewport->AddViewportWidgetContent(GoBanner.ToSharedRef(), TNLoadingLayers::GoBannerZOrder);
	GoBannerViewport = Viewport;
}

void UTN_LoadingScreenSubsystem::RemoveGoBanner()
{
	UGameViewportClient* BannerViewport = GoBannerViewport.Get();
	if (GoBanner.IsValid() && BannerViewport)
	{
		BannerViewport->RemoveViewportWidgetContent(GoBanner.ToSharedRef());
	}
	GoBanner.Reset();
	GoBannerViewport = nullptr;
	bGoBannerCuePlayed = false;
}

void UTN_LoadingScreenSubsystem::TickGoBanner(double Now)
{
	if (!GoBanner.IsValid())
	{
		return;
	}
	if (!bGoBannerCuePlayed && Now >= GoBanner->GetShowTime())
	{
		// Sale con el mismo «¡pum!» y el «fiuu» de cuando revienta el huevo.
		bGoBannerCuePlayed = true;
		if (UTN_EggSynthComponent* EggSynth = EnsureSynth())
		{
			EggSynth->PlayPop();
			EggSynth->PlayWhoosh(0.8f);
		}
	}
	if (GoBanner->IsFinished())
	{
		RemoveGoBanner();
	}
}

void UTN_LoadingScreenSubsystem::TickWorldReady(UWorld* World, double Now)
{
	// El huevo se rompe en cuanto el mapa está listo en esta máquina (o se acaba su espera).
	const bool bLoadTimedOut = Now - LoadDoneTime > TNLoadingTimes::MaxAfterLoadSeconds;
	if (!bLoadTimedOut && !IsWorldReady(World, Now))
	{
		return;
	}
	BreakNow();
}

void UTN_LoadingScreenSubsystem::AddToViewport()
{
	UGameInstance* GameInstance = GetGameInstance();
	UGameViewportClient* Viewport = GameInstance ? GameInstance->GetGameViewportClient() : nullptr;
	if (!Viewport || !Screen.IsValid())
	{
		return;
	}
	if (ScreenInViewport.IsValid() && ViewportUsed.Get() == Viewport)
	{
		return;
	}
	RemoveFromViewport();
	// Por encima del HUD y de los menús; la pantalla de texto del GameInstance no sale mientras hay huevo.
	Viewport->AddViewportWidgetContent(Screen.ToSharedRef(), TNLoadingLayers::EggZOrder);
	ScreenInViewport = Screen;
	ViewportUsed = Viewport;
}

void UTN_LoadingScreenSubsystem::RemoveFromViewport()
{
	UGameViewportClient* Viewport = ViewportUsed.Get();
	if (ScreenInViewport.IsValid() && Viewport)
	{
		Viewport->RemoveViewportWidgetContent(ScreenInViewport.ToSharedRef());
	}
	ScreenInViewport.Reset();
	ViewportUsed = nullptr;
}

void UTN_LoadingScreenSubsystem::Hide()
{
	RemoveFromViewport();
	Screen.Reset();
	bHold = false;
	BreakAtTime = -1.0;
	LoadDoneTime = -1.0;
	HoldReason = TNEggLoading::EHold::None;
	LobbyCancelSince = -1.0;
	LastAutoStatus.Reset();
	FlushWhenClosed(true);
}

bool UTN_LoadingScreenSubsystem::IsWorldReady(UWorld* World, double Now) const
{
	if (!World || !World->HasBegunPlay())
	{
		return false;
	}
	const double SinceLoad = Now - LoadDoneTime;
	if (SinceLoad < TNLoadingTimes::MinAfterLoadSeconds)
	{
		return false;
	}
	const FString MapName = World->GetMapName();
	// En el menú no hay tortuga ni terreno que esperar.
	if (MapName.Contains(TEXT("Menu")))
	{
		return true;
	}
	// Paquetes que siguen cargando en segundo plano (subniveles, mallas): como mucho unos segundos.
	if (IsAsyncLoading() && SinceLoad < 6.0)
	{
		return false;
	}
	// La tortuga de cada jugador local (con la pantalla partida, la de todos), como mucho unos segundos.
	TArray<APlayerController*> LocalControllers;
	TNLocalViews::GetLocalControllers(World, LocalControllers);
	bool bAllPawns = LocalControllers.Num() > 0;
	for (const APlayerController* LocalPC : LocalControllers)
	{
		bAllPawns = bAllPawns && LocalPC->GetPawn() != nullptr;
	}
	if (!bAllPawns && SinceLoad < 6.0)
	{
		return false;
	}
	return true;
}

void UTN_LoadingScreenSubsystem::UpdateAutoClose(UWorld* World, double Now)
{
	if (!World || bInHardLoad)
	{
		return;
	}
	if (World->IsInSeamlessTravel())
	{
		// Por si el aviso del viaje sin cortes no llegó: el huevo se cierra igual.
		HoldReason = TNEggLoading::EHold::None;
		if (!Screen.IsValid() || Screen->IsBreaking() || Screen->IsOpening())
		{
			BeginLoading(LoadingMapName.IsEmpty() ? NSLOCTEXT("TNLoading", "StatusHatching", "Incubando la partida").ToString() : FriendlyStatusForMap(LoadingMapName));
		}
		return;
	}
	const ATN_CoopGameState* CoopState = World->GetGameState<ATN_CoopGameState>();
	if (!CoopState)
	{
		return;
	}

	if (IsLobbyWorld(World))
	{
		if (HoldReason != TNEggLoading::EHold::Lobby)
		{
			bLobbyTravelImminent = false;
		}
		// Todos listos (en los huevos o en la sala de la puerta doble): cuenta atrás (y luego la pausa antes de viajar). El huevo se cierra en todas las
		// pantallas y no se abre hasta que el mapa de la partida esté cargado y con su terreno.
		const bool bCountdown = CoopState->CountdownValue > 0
			|| CoopState->MatchFlowState == ETNMatchFlowState::Countdown
			|| CoopState->MatchFlowState == ETNMatchFlowState::Cinematic;
		if (bCountdown)
		{
			LobbyCancelSince = -1.0;
			bLobbyTravelImminent |= CoopState->MatchFlowState == ETNMatchFlowState::Cinematic;
			const bool bCounting = CoopState->CountdownValue > 0;
			const FString CountdownStatus = bCounting
				? FText::Format(NSLOCTEXT("TNLoading", "StatusAllReady", "¡Todos listos! Salimos en {0}"), TNLocText::Int(CoopState->CountdownValue)).ToString()
				: NSLOCTEXT("TNLoading", "StatusPreparingExpedition", "Preparando la expedición").ToString();
			if (HoldReason != TNEggLoading::EHold::Lobby || !Screen.IsValid() || Screen->IsBreaking() || Screen->IsOpening())
			{
				CloseAndHold(TNEggLoading::EHold::Lobby, CountdownStatus);
				LastAutoStatus.Reset();
			}
			if (Screen.IsValid() && CountdownStatus != LastAutoStatus)
			{
				Screen->SetStatus(TNLocText::Literal(CountdownStatus), !bCounting);
				LastAutoStatus = CountdownStatus;
			}
		}
		else if (HoldReason == TNEggLoading::EHold::Lobby && !bLobbyTravelImminent)
		{
			// Cuenta atrás cancelada (alguien ha salido de su huevo): se abre otra vez. Si ya se iba a viajar, no: sigue
			// cerrado hasta el mapa siguiente.
			if (LobbyCancelSince < 0.0)
			{
				LobbyCancelSince = Now;
			}
			else if (Now - LobbyCancelSince > TNLoadingTimes::LobbyCancelSeconds)
			{
				OpenNow();
			}
		}
		return;
	}

	// Mapas de partida: la cuenta de los resultados acaba con la vuelta al cuartel; en su último segundo se cierra.
	const bool bResults = CoopState->MatchFlowState == ETNMatchFlowState::Results;
	if (bResults && CoopState->CountdownValue == 1 && HoldReason == TNEggLoading::EHold::None
		&& (!Screen.IsValid() || Screen->IsBreaking() || Screen->IsOpening()))
	{
		CloseAndHold(TNEggLoading::EHold::Results, TEXT("Rumbo al cuartel"));
	}
	else if (!bResults && HoldReason == TNEggLoading::EHold::Results)
	{
		OpenNow();
	}
}

UTN_EggSynthComponent* UTN_LoadingScreenSubsystem::EnsureSynth()
{
	if (IsValid(Synth) && Synth->IsRegistered())
	{
		return Synth;
	}
	UGameInstance* GameInstance = GetGameInstance();
	APlayerController* PC = GameInstance ? GameInstance->GetFirstLocalPlayerController() : nullptr;
	if (!PC || !PC->GetWorld() || !FApp::CanEverRenderAudio())
	{
		return nullptr;
	}
	Synth = NewObject<UTN_EggSynthComponent>(PC, NAME_None, RF_Transient);
	if (USceneComponent* RootComp = PC->GetRootComponent())
	{
		Synth->SetupAttachment(RootComp);
	}
	Synth->RegisterComponent();
	PC->AddInstanceComponent(Synth);
	Synth->KeepAwake();
	return Synth;
}

void UTN_LoadingScreenSubsystem::TickCloseSound(double Now)
{
	if (!Screen.IsValid() || !Screen->IsClosed() || Screen->GetMoveSerial() == FiredKnockSerial)
	{
		return;
	}
	FiredKnockSerial = Screen->GetMoveSerial();
	// Solo al estamparse de verdad (no en los cierres en seco antes de un LoadMap).
	if (Screen->ClosesWithImpact() && Now - Screen->GetImpactTime() < 0.25)
	{
		if (UTN_EggSynthComponent* EggSynth = EnsureSynth())
		{
			EggSynth->PlayKnock(1.f);
		}
	}
}

void UTN_LoadingScreenSubsystem::TickBreakSounds()
{
	if (!Screen.IsValid())
	{
		return;
	}
	const float Elapsed = Screen->GetBreakElapsed();
	auto Fire = [this](int32 Cue, float At, float SinceBreak, TFunctionRef<void(UTN_EggSynthComponent&)> Play)
	{
		const uint32 Bit = 1u << Cue;
		if (SinceBreak >= At && !(FiredBreakCues & Bit))
		{
			FiredBreakCues |= Bit;
			if (UTN_EggSynthComponent* EggSynth = EnsureSynth())
			{
				Play(*EggSynth);
			}
		}
	};
	// Un crujido por grieta principal, cada vez más fuerte; el último, justo antes del «¡pum!» y del «fiuu».
	const TArray<float>& CrackTimes = Screen->GetCrackStartTimes();
	for (int32 i = 0; i < CrackTimes.Num() && i < 16; ++i)
	{
		const float Strength = 0.4f + 0.5f * FMath::Clamp(CrackTimes[i] / FTNEggTimeline::PopAt, 0.f, 1.f);
		Fire(i, CrackTimes[i], Elapsed, [Strength](UTN_EggSynthComponent& EggSynth) { EggSynth.PlayCrack(Strength); });
	}
	Fire(16, FTNEggTimeline::PopAt - 0.04f, Elapsed, [](UTN_EggSynthComponent& EggSynth) { EggSynth.PlayCrack(1.f); });
	Fire(17, FTNEggTimeline::PopAt, Elapsed, [](UTN_EggSynthComponent& EggSynth) { EggSynth.PlayPop(); });
	Fire(18, FTNEggTimeline::PopAt + 0.04f, Elapsed, [](UTN_EggSynthComponent& EggSynth) { EggSynth.PlayWhoosh(0.9f); });
}

void UTN_LoadingScreenSubsystem::Tick(float DeltaTime)
{
	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	const double Now = FPlatformTime::Seconds();

	FlushWhenClosed(false);
	UpdateAutoClose(World, Now);
	TickGoBanner(Now);
	if (!Screen.IsValid())
	{
		return;
	}

	AddToViewport();

	if (Screen->IsBreaking())
	{
		TickBreakSounds();
		if (Screen->IsBreakFinished())
		{
			Hide();
		}
		else if (Screen->IsShellGone() && Screen->GetVisibility() != EVisibility::HitTestInvisible)
		{
			// Solo queda «¡ADELANTE!» a la vista: los clics ya pasan a lo de debajo.
			Screen->SetVisibility(EVisibility::HitTestInvisible);
		}
		return;
	}
	if (Screen->IsOpening())
	{
		if (Screen->IsOpenFinished())
		{
			Hide();
		}
		return;
	}
	TickCloseSound(Now);

	// Cerrado a la espera de un viaje que aún no ha empezado: no se rompe; si el viaje no llega, se abre.
	if (HoldReason != TNEggLoading::EHold::None)
	{
		const double MaxHold = HoldReason == TNEggLoading::EHold::Travel ? TNLoadingTimes::HoldTravelSeconds
			: HoldReason == TNEggLoading::EHold::Lobby ? TNLoadingTimes::HoldLobbySeconds
			: TNLoadingTimes::HoldResultsSeconds;
		if (Now - HoldStartTime > MaxHold)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Carga] El huevo llevaba %.0f s cerrado esperando un viaje que no ha empezado: se abre."), Now - HoldStartTime);
			OpenNow();
		}
		return;
	}

	// Un LoadMap que no avisó de su final (fallo de conexión, por ejemplo) no deja el huevo puesto para siempre.
	if (bInHardLoad && Now - HardLoadStartTime > 60.0)
	{
		bInHardLoad = false;
	}
	const bool bTravelling = bInHardLoad || !World || World->IsInSeamlessTravel();
	if (bTravelling)
	{
		LoadDoneTime = -1.0;
		return;
	}
	if (LoadDoneTime < 0.0)
	{
		LoadDoneTime = Now;
	}

	// Primero que termine de cerrarse (y del golpe): nunca se rompe a medio cerrar.
	if (!Screen->IsSettled())
	{
		return;
	}

	// Pruebas: rotura programada o espera manual.
	if (BreakAtTime >= 0.0)
	{
		if (Now >= BreakAtTime)
		{
			BreakNow(bBreakAtWithGo);
		}
		return;
	}
	if (bHold)
	{
		return;
	}

	// Mapa listo (como mucho 45 s) y, en el mapa procedural, ronda empezada (como mucho 40 s más).
	TickWorldReady(World, Now);
}
