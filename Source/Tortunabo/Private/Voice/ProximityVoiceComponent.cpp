#include "Voice/ProximityVoiceComponent.h"
#include "TN_VoiceDeviceCapture.h"
#include "Core/TN_Log.h"
#include "UI/Voice/VoiceIndicatorWidget.h"
#include "Player/MP_GamePlayerController.h"
#include "Settings/TN_GameSettingsSubsystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "Blueprint/UserWidget.h"
#include "Input/Events.h"
#include "Voice/TN_VoiceRouting.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "EngineUtils.h"
#include "UI/TN_ScreenHost.h"

UProximityVoiceComponent::UProximityVoiceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
	// VoiceIndicatorWidgetClass is assigned via EditDefaultsOnly in the owning Blueprint
	// (e.g. BP_GamePlayerController or the Character BP that hosts this component).
}

void UProximityVoiceComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UProximityVoiceComponent, bIsSpeaking);
	// VoiceSampleRate se configura una vez al iniciar captura y no cambia → InitialOnly
	DOREPLIFETIME_CONDITION(UProximityVoiceComponent, VoiceSampleRate, COND_InitialOnly);
}

bool UProximityVoiceComponent::IsLocallyOwned() const
{
	if (const AActor* Owner = GetOwner())
	{
		if (const APawn* Pawn = Cast<APawn>(Owner))
		{
			return Pawn->IsLocallyControlled();
		}
		return Owner->HasLocalNetOwner();
	}
	return false;
}

void UProximityVoiceComponent::BeginPlay()
{
	Super::BeginPlay();
	bIsShuttingDown = false;
	bRuntimeResourcesCleanedUp = false;
	if (IsLocallyOwned())
	{
		OpenCapture();
	}
}

void UProximityVoiceComponent::OpenCapture()
{
	bCaptureOpenAttempted = true;
	// Con un micrófono elegido en el menú de pausa (y aún conectado), ese; si no, el predeterminado de siempre.
	if (OpenPreferredCaptureDevice())
	{
		return;
	}
	AudioCaptureSynth = MakeUnique<Audio::FAudioCaptureSynth>();
	if (!AudioCaptureSynth->OpenDefaultStream())
	{
		AudioCaptureSynth.Reset();
		return;
	}
	AudioCaptureSynth->StartCapturing();
	Audio::FCaptureDeviceInfo DeviceInfo;
	if (AudioCaptureSynth->GetDefaultCaptureDeviceInfo(DeviceInfo))
	{
		VoiceSampleRate = DeviceInfo.PreferredSampleRate;
		CaptureNumChannels = FMath::Max(1, DeviceInfo.InputChannels);
	}
}

UProximityVoiceComponent* UProximityVoiceComponent::EnsureOn(APawn* Pawn)
{
	if (!Pawn || !Pawn->HasAuthority())
	{
		return nullptr;
	}
	if (UProximityVoiceComponent* Existing = Pawn->FindComponentByClass<UProximityVoiceComponent>())
	{
		return Existing;
	}
	UProximityVoiceComponent* VoiceComp = NewObject<UProximityVoiceComponent>(Pawn, TEXT("ProximityVoice"));
	if (VoiceComp)
	{
		VoiceComp->RegisterComponent();
	}
	return VoiceComp;
}

namespace TNVoiceDevices
{
	/** Micrófono elegido (id de Windows; vacío = el predeterminado). Uno por proceso: es un ajuste de la máquina. */
	FString& Preferred()
	{
		static FString DeviceId;
		return DeviceId;
	}

	/**
	 * Captura solo para enumerar micrófonos: nunca abre nada. Se crea una vez y no se destruye (por lo mismo que las
	 * capturas de voz: nada de WASAPI en el cierre del proceso).
	 */
	Audio::FAudioCapture& Enumerator()
	{
		static Audio::FAudioCapture* Capture = new Audio::FAudioCapture();
		return *Capture;
	}
}

void UProximityVoiceComponent::SetPreferredCaptureDevice(const FString& DeviceId)
{
	TNVoiceDevices::Preferred() = DeviceId;
}

FString UProximityVoiceComponent::GetPreferredCaptureDevice()
{
	return TNVoiceDevices::Preferred();
}

void UProximityVoiceComponent::GetCaptureDevices(TArray<TPair<FString, FString>>& OutDevices)
{
	OutDevices.Reset();
	TArray<Audio::FCaptureDeviceInfo> Devices;
	TNVoiceDevices::Enumerator().GetCaptureDevicesAvailable(Devices);
	for (const Audio::FCaptureDeviceInfo& Device : Devices)
	{
		OutDevices.Emplace(Device.DeviceId, Device.DeviceName);
	}
}

bool UProximityVoiceComponent::OpenPreferredCaptureDevice()
{
	const FString Wanted = GetPreferredCaptureDevice();
	if (Wanted.IsEmpty())
	{
		return false;
	}
	TArray<Audio::FCaptureDeviceInfo> Devices;
	TNVoiceDevices::Enumerator().GetCaptureDevicesAvailable(Devices);
	const int32 DeviceIndex = Devices.IndexOfByPredicate([&Wanted](const Audio::FCaptureDeviceInfo& Device) { return Device.DeviceId == Wanted; });
	if (DeviceIndex == INDEX_NONE)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Voice] El micrófono elegido ya no está conectado: se usa el predeterminado."));
		return false;
	}
	// Se crea y no se destruye nunca (ni si falla): ver CleanupRuntimeResources.
	FTNVoiceDeviceCapture* Capture = new FTNVoiceDeviceCapture();
	if (!Capture->Open(DeviceIndex))
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Voice] No se pudo abrir el micrófono «%s»: se usa el predeterminado."), *Devices[DeviceIndex].DeviceName);
		return false;
	}
	DeviceCapture = Capture;
	OpenCaptureDevice = Wanted;
	VoiceSampleRate = Capture->GetSampleRate() > 0 ? Capture->GetSampleRate() : Devices[DeviceIndex].PreferredSampleRate;
	// La captura propia ya entrega mono.
	CaptureNumChannels = 1;
	UE_LOG(LogTortunabo, Log, TEXT("[Voice] Micrófono: %s."), *Devices[DeviceIndex].DeviceName);
	return true;
}

void UProximityVoiceComponent::PrepareForLevelTransition()
{
	if (bRuntimeResourcesCleanedUp || bIsShuttingDown)
	{
		return;
	}

	UE_LOG(LogTortunabo, Log, TEXT("[Voice] PrepareForLevelTransition on %s — orphaning capture, cleaning playback/UI."),
		*GetNameSafe(GetOwner()));

	bIsShuttingDown = true;
	SetComponentTickEnabled(false);

	// bForceLeakAudio = false → playback component and UI are cleaned up properly
	// (world is still valid at this point). The capture synth is ALWAYS leaked regardless.
	CleanupRuntimeResources(/* bForceLeakAudio */ false);
}

void UProximityVoiceComponent::ShutdownAllCapture(const UWorld* World)
{
	if (!World)
	{
		return;
	}

	for (TObjectIterator<UProximityVoiceComponent> It; It; ++It)
	{
		UProximityVoiceComponent* Comp = *It;
		if (Comp && Comp->GetWorld() == World)
		{
			Comp->PrepareForLevelTransition();
		}
	}

	UE_LOG(LogTortunabo, Log, TEXT("[Voice] ShutdownAllCapture: all voice components in world cleaned up before travel."));
}

void UProximityVoiceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bIsShuttingDown = true;
	SetComponentTickEnabled(false);

	// If PrepareForLevelTransition already ran, bRuntimeResourcesCleanedUp is true
	// and CleanupRuntimeResources will early-out (no-op).
	// For EndPlay(Destroyed) during gameplay (e.g. player death), the capture synth
	// is still leaked (see comment in CleanupRuntimeResources), but playback/UI are
	// cleaned up normally.
	const bool bForceLeakPlayback = (EndPlayReason == EEndPlayReason::LevelTransition)
		|| IsEngineExitRequested();

	CleanupRuntimeResources(bForceLeakPlayback);
	Super::EndPlay(EndPlayReason);
}

void UProximityVoiceComponent::OnUnregister()
{
	bIsShuttingDown = true;
	SetComponentTickEnabled(false);
	CleanupRuntimeResources(IsEngineExitRequested());
	Super::OnUnregister();
}

void UProximityVoiceComponent::BeginDestroy()
{
	bIsShuttingDown = true;
	SetComponentTickEnabled(false);
	CleanupRuntimeResources(IsEngineExitRequested());
	Super::BeginDestroy();
}


void UProximityVoiceComponent::CleanupRuntimeResources(bool bForceLeakAudio)
{
	if (bRuntimeResourcesCleanedUp)
	{
		return;
	}

	bRuntimeResourcesCleanedUp = true;
	OnSpeakingChanged.Clear();

	// --- Audio Capture cleanup ---
	// ALWAYS orphan (Release) the FAudioCaptureSynth — NEVER call StopCapturing()
	// or Reset()/destructor. In UE 5.6, the WASAPI capture handle inside the synth
	// can become INVALID_HANDLE_VALUE at any time (the world's FAudioDevice may
	// invalidate it during teardown, or Windows may reclaim it). Both StopCapturing()
	// and the destructor attempt to use that handle and cause:
	//   - StopCapturing(): ACCESS_VIOLATION reading 0xFFFFFFFFFFFFFFFF
	//   - Destructor:      ACCESS_VIOLATION writing 0x0000000000000024
	//
	// Release() detaches our TUniquePtr without touching the synth internals.
	// The synth object leaks (~KB) — its capture callback continues harmlessly
	// writing to an internal buffer that nobody reads. The OS reclaims the memory
	// when the process exits.
	if (AudioCaptureSynth)
	{
		(void)AudioCaptureSynth.Release();
	}
	// La del micrófono elegido, igual: se suelta sin tocarla.
	DeviceCapture = nullptr;

	{
		FScopeLock Lock(&CaptureBufferLock);
		CaptureBuffer.Reset();
	}
	DecimationCarry.Reset();

	SendTimer = 0.f;
	bIsSpeaking = false;

	// --- Playback component cleanup ---
	// bForceLeakAudio controls whether we touch the playback component.
	// When called proactively (PrepareForLevelTransition, bForceLeakAudio=false),
	// the world is still valid and we can Stop/Deactivate/Destroy properly.
	// During EndPlay(LevelTransition) or engine exit, objects may already be
	// partially destroyed — skip interactive cleanup.
	if (UAudioComponent* AudioComponent = PlaybackAudioComponent.Get())
	{
		PlaybackAudioComponent = nullptr;

		const bool bSafeToTouch = !bForceLeakAudio && !AudioComponent->IsBeingDestroyed();

		if (bSafeToTouch)
		{
			AudioComponent->Stop();
			AudioComponent->Deactivate();
			AudioComponent->SetSound(nullptr);
		}

		if (bSafeToTouch && AudioComponent->IsRegistered())
		{
			AudioComponent->UnregisterComponent();
		}

		if (bSafeToTouch)
		{
			AudioComponent->DestroyComponent();
		}
	}

	ProceduralSoundWave = nullptr;

	if (!bForceLeakAudio && IsValid(VoiceIndicatorWidgetInstance))
	{
		VoiceIndicatorWidgetInstance->RemoveFromParent();
		VoiceIndicatorWidgetInstance = nullptr;
	}
	else
	{
		VoiceIndicatorWidgetInstance = nullptr;
	}
}

void UProximityVoiceComponent::CreateVoiceIndicatorHUD()
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn)
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController());
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	UClass* WidgetClass = VoiceIndicatorWidgetClass
		? VoiceIndicatorWidgetClass.Get()
		: UVoiceIndicatorWidget::StaticClass();

	VoiceIndicatorWidgetInstance = CreateWidget<UUserWidget>(PC, WidgetClass);
	if (VoiceIndicatorWidgetInstance)
	{
		TNScreen::AddToScreen(VoiceIndicatorWidgetInstance, 10);
	}
}

void UProximityVoiceComponent::SetupPlayback(int32 InSampleRate)
{
	if (bIsShuttingDown || (GetWorld() && GetWorld()->bIsTearingDown))
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	const int32 ActualSampleRate = (InSampleRate > 0) ? InSampleRate : VoiceSampleRate;

	ProceduralSoundWave = CreateVoiceWave(ActualSampleRate);
	if (!ProceduralSoundWave)
	{
		return;
	}

	PlaybackAudioComponent = NewObject<UAudioComponent>(Owner);
	if (!PlaybackAudioComponent)
	{
		return;
	}

	PlaybackAudioComponent->SetupAttachment(Owner->GetRootComponent());
	PlaybackAudioComponent->bAutoActivate = false;
	PlaybackAudioComponent->bAlwaysPlay = false; // Allow attenuation to cull distant voices

	// Configure spatial attenuation for proximity voice:
	// Full volume within InnerRadius (default 300cm = 3m),
	// fades to silence at OuterRadius (default 2500cm = 25m).
	PlaybackAudioComponent->bAllowSpatialization = true;
	PlaybackAudioComponent->bOverrideAttenuation = true;
	PlaybackAudioComponent->AttenuationOverrides = MakeAttenuation();

	PlaybackAudioComponent->RegisterComponent();
	PlaybackAudioComponent->SetVolumeMultiplier(PlaybackVolume);
	PlaybackAudioComponent->SetSound(ProceduralSoundWave);
	PlaybackAudioComponent->Play();
}

USoundWaveProcedural* UProximityVoiceComponent::CreateVoiceWave(int32 InSampleRate)
{
	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(this);
	if (!Wave)
	{
		return nullptr;
	}
	Wave->SetSampleRate(InSampleRate);
	Wave->NumChannels = VoiceNumChannels;
	Wave->Duration = INDEFINITELY_LOOPING_DURATION;
	Wave->SoundGroup = SOUNDGROUP_Voice;
	Wave->bLooping = false;
	Wave->bProcedural = true;
	Wave->Volume = PlaybackVolume;
	PlaybackSampleRate = InSampleRate;
	return Wave;
}

void UProximityVoiceComponent::ApplySendPlan(int32 CaptureRate)
{
	// Lo acumulado estaba reducido con el factor anterior: enviarlo con la frecuencia nueva lo reproduciría a otra velocidad.
	{
		FScopeLock Lock(&CaptureBufferLock);
		CaptureBuffer.Reset();
	}
	DecimationCarry.Reset();
	ActiveSendPlan = TNVoiceRate::MakeSendPlan(CaptureRate, VoiceTargetSampleRate);

	const bool bMeasured = CaptureRateMeter.Rate > 0;
	if (!ActiveSendPlan.IsValid())
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Voice] Captura a %d Hz: reducida por %d queda a %d Hz, fuera de %d-%d Hz: no se envía voz."),
			CaptureRate, ActiveSendPlan.Factor, ActiveSendPlan.SendRate, TNVoiceRate::MinVoiceRate, TNVoiceRate::MaxVoiceRate);
	}
	else if (!bMeasured)
	{
		// Dispositivo raro: la captura no cuadra con ninguna frecuencia estándar. Se usa la que dice el dispositivo.
		UE_LOG(LogTortunabo, Warning, TEXT("[Voice] No se pudo medir la frecuencia de la captura en %.0f s: se usa la del dispositivo, %d Hz (%d canales): se reduce por %d y se envía a %d Hz."),
			TNVoiceRate::FCaptureRateMeter::GiveUpSeconds, CaptureRate, CaptureNumChannels, ActiveSendPlan.Factor, ActiveSendPlan.SendRate);
	}
	else if (CaptureRate != VoiceSampleRate)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Voice] La captura llega a %d Hz en mono, no a los %d Hz del dispositivo (%d canales): se reduce por %d y se envía a %d Hz."),
			CaptureRate, VoiceSampleRate, CaptureNumChannels, ActiveSendPlan.Factor, ActiveSendPlan.SendRate);
	}
	else
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Voice] Captura a %d Hz (medida, %d canales): se reduce por %d y se envía a %d Hz."),
			CaptureRate, CaptureNumChannels, ActiveSendPlan.Factor, ActiveSendPlan.SendRate);
	}
}

void UProximityVoiceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// El peón pasa a ser de este jugador después de BeginPlay (el controlador que llega por red después que el componente):
	// se abre el micrófono entonces, una sola vez.
	if (!bCaptureOpenAttempted && !bIsShuttingDown && IsLocallyOwned())
	{
		OpenCapture();
	}

	if (bIsShuttingDown || (GetWorld() && GetWorld()->bIsTearingDown) || !IsLocallyOwned() || (!AudioCaptureSynth && !DeviceCapture))
	{
		return;
	}

	TArray<float> NewAudioData;
	const bool bGotAudio = DeviceCapture ? DeviceCapture->GetAudioData(NewAudioData) : AudioCaptureSynth->GetAudioData(NewAudioData);
	if (bGotAudio && NewAudioData.Num() > 0)
	{
		TArray<float> MonoData;
		if (CaptureNumChannels > 1)
		{
			const int32 NumFrames = NewAudioData.Num() / CaptureNumChannels;
			MonoData.SetNumUninitialized(NumFrames);
			for (int32 Frame = 0; Frame < NumFrames; ++Frame)
			{
				float Sum = 0.f;
				for (int32 Ch = 0; Ch < CaptureNumChannels; ++Ch)
				{
					Sum += NewAudioData[Frame * CaptureNumChannels + Ch];
				}
				MonoData[Frame] = Sum / CaptureNumChannels;
			}
		}
		else
		{
			MonoData = MoveTemp(NewAudioData);
		}

		// Frecuencia real: muestras mono que llegan por segundo (antes de reducirlas). Si no es la que dice el dispositivo
		// (cancelación de eco de Windows a 16 kHz, canales distintos), la voz se etiquetaba mal y se oía de ardilla. El factor
		// de reducción y la frecuencia de envío salen los dos del mismo plan (ActiveSendPlan), que se rehace, vaciando lo
		// acumulado, en cuanto cambia la frecuencia de la captura. Hasta la primera medida (~0,5 s de audio) no hay frecuencia
		// con la que etiquetar: GetCaptureSampleRate() da 0, no hay plan y no se envía nada (ver más abajo).
		CaptureRateMeter.Add(MonoData.Num(), FPlatformTime::Seconds());
		if (const int32 CaptureRate = GetCaptureSampleRate(); CaptureRate != ActiveSendPlan.CaptureRate)
		{
			ApplySendPlan(CaptureRate);
		}

		for (float& Sample : MonoData)
		{
			Sample = FMath::Clamp(Sample * VoiceGain, -1.0f, 1.0f);
		}

		// Nivel para el medidor del menú de pausa: RMS del bloque recién capturado, con la ganancia ya aplicada.
		{
			float BlockSquares = 0.f;
			for (const float Sample : MonoData)
			{
				BlockSquares += Sample * Sample;
			}
			MicLevel = MonoData.Num() > 0 ? FMath::Sqrt(BlockSquares / MonoData.Num()) : 0.f;
		}

		// Sin primera medida de la frecuencia no se sabe con cuál etiquetar lo capturado: se descarta (como mucho ~0,5 s la
		// primera vez) en vez de enviarlo con una frecuencia que puede no ser la suya.
		if (ActiveSendPlan.CaptureRate > 0)
		{
			// ── Reducción con box filter (anti-aliasing) ───────────────────────
			// Promedia Factor muestras → evita el efecto "lata" de la decimación simple (nth-sample sin filtro pasa-bajos).
			// El factor es el del plan de envío, max(1, captura / VoiceTargetSampleRate): 48 kHz → 3 (16 kHz), 44,1 kHz → 2
			// (22,05 kHz), 16 kHz → 1 (sin reducir). Lo que sobra de un bloque pasa al siguiente (Decimate): el plan es exacto.
			TArray<float> Reduced;
			TNVoiceRate::Decimate(MonoData, ActiveSendPlan.Factor, DecimationCarry, Reduced);

			FScopeLock Lock(&CaptureBufferLock);
			CaptureBuffer.Append(Reduced);

			// ── Cap buffer size ──────────────────────────────────────────────────
			// Si el SendInterval no se cumplió en mucho tiempo (lag spike, pawn
			// estaba pausado durante death/revive), CaptureBuffer crece sin control
			// y el RPC siguiente excede el límite UE5 de 65535 elementos por array
			// replicado (UE5 ensure crash en RepLayout::ValidateArraySize).
			// Cap a 8000 samples (0,5 s a 16 kHz, ≥ 83 ms a cualquier frecuencia aceptada) → siempre cabe en RPC tras compress.
			constexpr int32 MaxBufferedSamples = 8000;
			if (CaptureBuffer.Num() > MaxBufferedSamples)
			{
				const int32 Excess = CaptureBuffer.Num() - MaxBufferedSamples;
				CaptureBuffer.RemoveAt(0, Excess, EAllowShrinking::No);
				UE_LOG(LogTortunabo, Verbose, TEXT("[Voice] CaptureBuffer cap: dropped %d old samples"), Excess);
			}
		}
	}

	// Micrófono cerrado (silenciado o sin pulsar para hablar): la tortuga deja de hablar en el acto, sin la espera.
	if (!bTransmitEnabled && bIsSpeaking)
	{
		bIsSpeaking = false;
		SilenceHoldOffTimer = 0.f;
		OnSpeakingChanged.Broadcast(false);
	}

	// ── Speaking detection con silence hold-off ──────────────────────────
	{
		FScopeLock Lock(&CaptureBufferLock);
		if (CaptureBuffer.Num() > 0)
		{
			float SumSquares = 0.f;
			for (const float Sample : CaptureBuffer)
			{
				SumSquares += Sample * Sample;
			}
			const float RMS = FMath::Sqrt(SumSquares / CaptureBuffer.Num());
			const bool bAboveThreshold = bTransmitEnabled && RMS > SpeakingThreshold;

			if (bAboveThreshold)
			{
				SilenceHoldOffTimer = 0.f;
				if (!bIsSpeaking)
				{
					bIsSpeaking = true;
					OnSpeakingChanged.Broadcast(true);
				}
			}
			else if (bIsSpeaking)
			{
				// Debounce: esperar SilenceHoldOffSeconds antes de marcar silencio
				SilenceHoldOffTimer += DeltaTime;
				if (SilenceHoldOffTimer >= SilenceHoldOffSeconds)
				{
					bIsSpeaking = false;
					OnSpeakingChanged.Broadcast(false);
				}
			}
		}
		else if (bIsSpeaking)
		{
			SilenceHoldOffTimer += DeltaTime;
			if (SilenceHoldOffTimer >= SilenceHoldOffSeconds)
			{
				bIsSpeaking = false;
				OnSpeakingChanged.Broadcast(false);
			}
		}
	}

	SendTimer += DeltaTime;
	if (SendTimer >= SendInterval)
	{
		SendTimer = 0.f;

		TArray<float> SamplesToSend;
		{
			FScopeLock Lock(&CaptureBufferLock);
			if (CaptureBuffer.Num() > 0)
			{
				SamplesToSend = MoveTemp(CaptureBuffer);
				CaptureBuffer.Reset();
			}
		}

		// Las muestras de CaptureBuffer son siempre del plan vigente (cada cambio de plan lo vacía): se etiquetan con su
		// frecuencia y con ninguna otra. Un plan fuera de 8000-96000 Hz no se envía (el destino lo descartaría).
		if (SamplesToSend.Num() > 0 && bIsSpeaking && ActiveSendPlan.IsValid())
		{
			TArray<uint8> Compressed = CompressSamples(SamplesToSend);

			// CRITICAL: UE5 RepLayout::ValidateArraySize hace ensure si el array
			// supera 65535 elementos. MaxVoicePayloadBytes=8192 es el cap server.
			// Replicarlo client-side previene el ensure crash al serializar el
			// RPC (que el server iba a rechazar igual). Drop silencioso del
			// frame es preferible al crash del editor.
			constexpr int32 ClientPayloadCap = MaxVoicePayloadBytes;
			if (Compressed.Num() > 0 && Compressed.Num() <= ClientPayloadCap)
			{
				Server_SendVoiceData(Compressed, ActiveSendPlan.SendRate);
			}
			else if (Compressed.Num() > ClientPayloadCap)
			{
				UE_LOG(LogTortunabo, Warning, TEXT("[Voice] Drop oversized compressed payload (%d > %d) — frame skipped"),
					Compressed.Num(), ClientPayloadCap);
			}
		}
	}
}

bool UProximityVoiceComponent::Server_SendVoiceData_Validate(const TArray<uint8>& CompressedData, int32 SenderSampleRate)
{
	// Red de seguridad a nivel de engine: rechaza payloads absurdos o sample rates
	// fuera de todo rango humano (INT_MAX de un cliente manipulado). Cotas generosas
	// para no desconectar clientes legítimos; el _Implementation descarta con precisión lo que no esté en 8000-96000 Hz.
	return CompressedData.Num() <= MaxVoicePayloadBytes && SenderSampleRate > 0 && SenderSampleRate <= 192000;
}

void UProximityVoiceComponent::Server_SendVoiceData_Implementation(const TArray<uint8>& CompressedData, int32 SenderSampleRate)
{
	// Payload cap: ~8 KB covers 80ms at 48kHz stereo with headroom.
	// Larger packets indicate a malicious or bugged client.
	if (CompressedData.Num() > MaxVoicePayloadBytes)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Voice] Server_SendVoiceData: oversized payload (%d bytes) from %s — dropped"),
			CompressedData.Num(), *GetNameSafe(GetOwner()));
		return;
	}

	// Sanitizar el sample rate reportado por el cliente antes de reenviarlo: un valor
	// fuera de rango (p.ej. INT_MAX de un cliente manipulado) llega a
	// USoundWaveProcedural::SetSampleRate en los receptores y corrompe/crashea su audio.
	// Fuera del rango humano de voz se DESCARTA el paquete: acotarlo lo dejaría con una etiqueta que no es la de sus
	// muestras (sonaría acelerado o lento). Un cliente normal nunca lo manda así: su plan de envío (TNVoiceRate) lo evita.
	if (!TNVoiceRate::IsRateAccepted(SenderSampleRate))
	{
		UE_LOG(LogTortunabo, Verbose, TEXT("[Voice] Server_SendVoiceData: %d Hz fuera de %d-%d Hz de %s — descartado."),
			SenderSampleRate, TNVoiceRate::MinVoiceRate, TNVoiceRate::MaxVoiceRate, *GetNameSafe(GetOwner()));
		return;
	}

	// Server-side rate limit: allow at most 25 Hz (min 40ms between packets).
	// The client enforces 80ms (12.5 Hz) via SendInterval, so 40ms gives 2× headroom for jitter.
	constexpr float MinVoicePacketInterval = 0.04f;
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f;
	if (Now >= 0.f && (Now - LastVoicePacketServerTime) < MinVoicePacketInterval)
	{
		return;
	}
	LastVoicePacketServerTime = Now;

	RelayVoiceToListeners(CompressedData, SenderSampleRate);
}

void UProximityVoiceComponent::RelayVoiceToListeners(const TArray<uint8>& CompressedData, int32 SenderSampleRate)
{
	// Solo a quien la va a oír: dentro de OuterRadius y, como mucho, los MaxVoiceListeners más cercanos (TNVoiceRouting).
	AActor* Speaker = GetOwner();
	UWorld* World = GetWorld();
	if (!Speaker || !World)
	{
		return;
	}
	const APawn* SpeakerPawn = Cast<APawn>(Speaker);
	const APlayerState* SpeakerState = SpeakerPawn ? SpeakerPawn->GetPlayerState() : nullptr;
	const FVector SpeakerLoc = Speaker->GetActorLocation();
	TArray<ITN_VoiceListener*, TInlineAllocator<16>> Listeners;
	TArray<double, TInlineAllocator<16>> DistancesSquared;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		ITN_VoiceListener* Listener = Cast<ITN_VoiceListener>(PC);
		const APawn* ListenerPawn = PC ? PC->GetPawn() : nullptr;
		// Ni a quien no recibe voz ni al propio hablante.
		if (!Listener || !ListenerPawn || ListenerPawn == Speaker || (SpeakerState && PC->PlayerState == SpeakerState))
		{
			continue;
		}
		Listeners.Add(Listener);
		DistancesSquared.Add(FVector::DistSquared(ListenerPawn->GetActorLocation(), SpeakerLoc));
	}
	const TArray<bool> Selected = TNVoiceRouting::SelectListeners(DistancesSquared, OuterRadius, MaxVoiceListeners);
	for (int32 Index = 0; Index < Listeners.Num(); ++Index)
	{
		if (Selected[Index])
		{
			Listeners[Index]->SendVoiceToOwningClient(CompressedData, SenderSampleRate, Speaker);
		}
	}
}

FSoundAttenuationSettings UProximityVoiceComponent::MakeAttenuation() const
{
	FSoundAttenuationSettings Settings;
	Settings.bAttenuate = true;
	Settings.bSpatialize = true;
	Settings.FalloffDistance = FMath::Max(OuterRadius - InnerRadius, 100.f);
	Settings.AttenuationShape = EAttenuationShape::Sphere;
	Settings.AttenuationShapeExtents = FVector(InnerRadius);
	Settings.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
	return Settings;
}

bool UProximityVoiceComponent::IsHeardSpeaking() const
{
	if (IsLocallyOwned())
	{
		return bIsSpeaking;
	}
	const UWorld* World = GetWorld();
	return World && LastRemoteVoiceTime >= 0.0 && World->GetRealTimeSeconds() - LastRemoteVoiceTime < 0.35;
}

const APlayerState* UProximityVoiceComponent::GetSpeakerState() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (const APlayerState* Live = Pawn ? Pawn->GetPlayerState() : nullptr)
	{
		LastSpeakerState = Live;
		return Live;
	}
	return LastSpeakerState.Get();
}

bool UProximityVoiceComponent::IsMutedByListener() const
{
	const APlayerState* Speaker = GetSpeakerState();
	const UTN_GameSettingsSubsystem* Settings = Speaker ? UTN_GameSettingsSubsystem::Get(this) : nullptr;
	return Settings && Settings->IsPlayerMuted(UTN_GameSettingsSubsystem::PlayerKey(Speaker));
}

void UProximityVoiceComponent::PlayRemoteVoice(const TArray<uint8>& CompressedData, int32 SenderSampleRate)
{
	if (bIsShuttingDown || (GetWorld() && GetWorld()->bIsTearingDown) || IsLocallyOwned())
	{
		return;
	}

	// Defensa en el consumidor: un sample rate recibido por red fuera del rango humano (el mismo que acepta el servidor) se
	// descarta, no se acota ni se cambia por otro de serie: con una etiqueta que no es la de sus muestras sonaría distinto.
	if (!TNVoiceRate::IsRateAccepted(SenderSampleRate))
	{
		UE_LOG(LogTortunabo, Verbose, TEXT("[Voice] PlayRemoteVoice: %d Hz fuera de %d-%d Hz en %s — descartado."),
			SenderSampleRate, TNVoiceRate::MinVoiceRate, TNVoiceRate::MaxVoiceRate, *GetNameSafe(GetOwner()));
		return;
	}

	LastRemoteVoiceTime = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;

	// Si quien escucha lo tiene silenciado, ni se descodifica ni se reproduce. Bajarle el volumen a 0 cada fotograma
	// (UTN_GameSettingsSubsystem::UpdateSounds) no basta por sí solo: un componente de reproducción recién creado suena a
	// volumen pleno hasta el siguiente fotograma, y sin PlayerState en el peón no se sabría a quién silenciar (#248).
	if (IsMutedByListener())
	{
		return;
	}
	if (!bWarnedNoSpeakerState && !GetSpeakerState())
	{
		bWarnedNoSpeakerState = true;
		UE_LOG(LogTortunabo, Warning, TEXT("[Voice] Llega voz de %s sin saber de qué jugador es (peón sin PlayerState): no se puede aplicar su silencio."),
			*GetNameSafe(GetOwner()));
	}

	if (!ProceduralSoundWave || !PlaybackAudioComponent)
	{
		SetupPlayback(SenderSampleRate);
	}
	else if (PlaybackSampleRate != SenderSampleRate)
	{
		// El que habla ya ha medido su frecuencia real (TNVoiceRate) y es otra: onda nueva a esa frecuencia. Con la de
		// antes se oía más aguda y acelerada (o más grave y lenta).
		UE_LOG(LogTortunabo, Log, TEXT("[Voice] La voz de %s pasa de %d a %d Hz."), *GetNameSafe(GetOwner()), PlaybackSampleRate, SenderSampleRate);
		PlaybackAudioComponent->Stop();
		ProceduralSoundWave = CreateVoiceWave(SenderSampleRate);
		PlaybackAudioComponent->SetSound(ProceduralSoundWave);
	}

	if (!ProceduralSoundWave || !PlaybackAudioComponent)
	{
		return;
	}

	const TArray<float> Samples = DecompressSamples(CompressedData);
	if (Samples.Num() == 0)
	{
		return;
	}

	TArray<uint8> PCMData;
	PCMData.SetNumUninitialized(Samples.Num() * sizeof(int16));
	int16* OutPtr = reinterpret_cast<int16*>(PCMData.GetData());
	for (int32 i = 0; i < Samples.Num(); ++i)
	{
		OutPtr[i] = static_cast<int16>(FMath::Clamp(Samples[i], -1.f, 1.f) * 32767.f);
	}

	ProceduralSoundWave->QueueAudio(PCMData.GetData(), PCMData.Num());
	if (!PlaybackAudioComponent->IsPlaying())
	{
		PlaybackAudioComponent->Play();
	}
}

static const float MU_LAW_MU = 255.f;

TArray<uint8> UProximityVoiceComponent::CompressSamples(const TArray<float>& Samples)
{
	TArray<uint8> Compressed;
	Compressed.SetNumUninitialized(Samples.Num());

	for (int32 i = 0; i < Samples.Num(); ++i)
	{
		const float Sample = FMath::Clamp(Samples[i], -1.f, 1.f);
		const float Sign = Sample < 0.f ? -1.f : 1.f;
		const float Magnitude = FMath::Loge(1.f + MU_LAW_MU * FMath::Abs(Sample)) / FMath::Loge(1.f + MU_LAW_MU);
		const float Encoded = Sign * Magnitude;
		Compressed[i] = static_cast<uint8>(FMath::Clamp((Encoded + 1.f) * 0.5f * 255.f, 0.f, 255.f));
	}

	return Compressed;
}

TArray<float> UProximityVoiceComponent::DecompressSamples(const TArray<uint8>& Compressed)
{
	TArray<float> Samples;
	Samples.SetNumUninitialized(Compressed.Num());

	for (int32 i = 0; i < Compressed.Num(); ++i)
	{
		const float Encoded = (static_cast<float>(Compressed[i]) / 255.f) * 2.f - 1.f;
		const float Sign = Encoded < 0.f ? -1.f : 1.f;
		const float Decoded = Sign * (1.f / MU_LAW_MU) * (FMath::Pow(1.f + MU_LAW_MU, FMath::Abs(Encoded)) - 1.f);
		Samples[i] = FMath::Clamp(Decoded, -1.f, 1.f);
	}

	return Samples;
}

