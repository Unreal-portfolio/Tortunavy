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
#include "EngineUtils.h"
#include "VR/TN_VRMode.h"

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

	// Con un micrófono elegido en el menú de pausa (y aún conectado), ese; si no, el predeterminado de siempre.
	if (IsLocallyOwned() && !OpenPreferredCaptureDevice())
	{
		AudioCaptureSynth = MakeUnique<Audio::FAudioCaptureSynth>();
		if (AudioCaptureSynth->OpenDefaultStream())
		{
			AudioCaptureSynth->StartCapturing();
			Audio::FCaptureDeviceInfo DeviceInfo;
			if (AudioCaptureSynth->GetDefaultCaptureDeviceInfo(DeviceInfo))
			{
				VoiceSampleRate = DeviceInfo.PreferredSampleRate;
				CaptureNumChannels = FMath::Max(1, DeviceInfo.InputChannels);
			}
		}
		else
		{
			AudioCaptureSynth.Reset();
		}

	}
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
		TNVR::AddToScreen(VoiceIndicatorWidgetInstance, 10);
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

	ProceduralSoundWave = NewObject<USoundWaveProcedural>(this);
	if (!ProceduralSoundWave)
	{
		return;
	}

	ProceduralSoundWave->SetSampleRate(ActualSampleRate);
	ProceduralSoundWave->NumChannels = VoiceNumChannels;
	ProceduralSoundWave->Duration = INDEFINITELY_LOOPING_DURATION;
	ProceduralSoundWave->SoundGroup = SOUNDGROUP_Voice;
	ProceduralSoundWave->bLooping = false;
	ProceduralSoundWave->bProcedural = true;
	ProceduralSoundWave->Volume = PlaybackVolume;

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
	PlaybackAudioComponent->AttenuationOverrides.bAttenuate = true;
	PlaybackAudioComponent->AttenuationOverrides.bSpatialize = true;
	PlaybackAudioComponent->AttenuationOverrides.FalloffDistance = FMath::Max(OuterRadius - InnerRadius, 100.f);
	PlaybackAudioComponent->AttenuationOverrides.AttenuationShape = EAttenuationShape::Sphere;
	PlaybackAudioComponent->AttenuationOverrides.AttenuationShapeExtents = FVector(InnerRadius);
	PlaybackAudioComponent->AttenuationOverrides.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;

	PlaybackAudioComponent->RegisterComponent();
	PlaybackAudioComponent->SetVolumeMultiplier(PlaybackVolume);
	PlaybackAudioComponent->SetSound(ProceduralSoundWave);
	PlaybackAudioComponent->Play();
}

void UProximityVoiceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

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

		// ── Downsampling con box filter (anti-aliasing) ───────────────────
		// Promedia DSFactor muestras antes de decimar → evita el efecto "lata"
		// que produce la decimación simple (nth-sample sin filtro pasa-bajos).
		// Factor=3 (de serie) → 48kHz a 16kHz: el paquete, a 1/3.
		const int32 DSFactor = FMath::Max(1, VoiceDownsampleFactor);
		if (DSFactor > 1 && MonoData.Num() > DSFactor)
		{
			TArray<float> Downsampled;
			Downsampled.Reserve(MonoData.Num() / DSFactor + 1);
			for (int32 i = 0; i < MonoData.Num(); i += DSFactor)
			{
				float Sum = 0.f;
				int32 Count = 0;
				const int32 End = FMath::Min(i + DSFactor, MonoData.Num());
				for (int32 k = i; k < End; ++k)
				{
					Sum += MonoData[k];
					++Count;
				}
				Downsampled.Add(Count > 0 ? Sum / Count : 0.f);
			}
			MonoData = MoveTemp(Downsampled);
		}

		FScopeLock Lock(&CaptureBufferLock);
		CaptureBuffer.Append(MonoData);

		// ── Cap buffer size ──────────────────────────────────────────────────
		// Si el SendInterval no se cumplió en mucho tiempo (lag spike, pawn
		// estaba pausado durante death/revive), CaptureBuffer crece sin control
		// y el RPC siguiente excede el límite UE5 de 65535 elementos por array
		// replicado (UE5 ensure crash en RepLayout::ValidateArraySize).
		// Cap a 8000 samples (~330ms a 24kHz) → siempre cabe en RPC tras compress.
		constexpr int32 MaxBufferedSamples = 8000;
		if (CaptureBuffer.Num() > MaxBufferedSamples)
		{
			const int32 Excess = CaptureBuffer.Num() - MaxBufferedSamples;
			CaptureBuffer.RemoveAt(0, Excess, EAllowShrinking::No);
			UE_LOG(LogTortunabo, Verbose, TEXT("[Voice] CaptureBuffer cap: dropped %d old samples"), Excess);
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

		if (SamplesToSend.Num() > 0 && bIsSpeaking)
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
				const int32 EffectiveSampleRate = FMath::Max(1, VoiceSampleRate / FMath::Max(1, VoiceDownsampleFactor));
				Server_SendVoiceData(Compressed, EffectiveSampleRate);
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
	// para no desconectar clientes legítimos; el _Implementation ya acota con precisión.
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
	// Acotar al rango humano de voz.
	SenderSampleRate = FMath::Clamp(SenderSampleRate, 8000, 96000);

	// Server-side rate limit: allow at most 25 Hz (min 40ms between packets).
	// The client enforces 80ms (12.5 Hz) via SendInterval, so 40ms gives 2× headroom for jitter.
	constexpr float MinVoicePacketInterval = 0.04f;
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f;
	if (Now >= 0.f && (Now - LastVoicePacketServerTime) < MinVoicePacketInterval)
	{
		return;
	}
	LastVoicePacketServerTime = Now;

	// Enviar solo a clientes dentro del OuterRadius — evita enviar audio a todos.
	AActor* SpeakerActor = GetOwner();
	if (!SpeakerActor || !GetWorld())
	{
		return;
	}

	const FVector SpeakerLoc = SpeakerActor->GetActorLocation();

	// Los oyentes dentro de OuterRadius y, si son más de MaxVoiceListeners, solo los más cercanos (los de lejos la oirían
	// muy baja de todos modos).
	TArray<TPair<double, AMP_GamePlayerController*>, TInlineAllocator<16>> Listeners;
	const double OuterSq = FMath::Square(static_cast<double>(OuterRadius));
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AMP_GamePlayerController* PC = Cast<AMP_GamePlayerController>(It->Get());
		if (!PC)
		{
			continue;
		}

		// Saltar al propio hablante
		APawn* ListenerPawn = PC->GetPawn();
		if (!ListenerPawn || ListenerPawn == SpeakerActor)
		{
			continue;
		}

		const double DistSq = FVector::DistSquared(ListenerPawn->GetActorLocation(), SpeakerLoc);
		if (DistSq <= OuterSq)
		{
			Listeners.Emplace(DistSq, PC);
		}
	}
	if (MaxVoiceListeners > 0 && Listeners.Num() > MaxVoiceListeners)
	{
		Listeners.Sort([](const TPair<double, AMP_GamePlayerController*>& A, const TPair<double, AMP_GamePlayerController*>& B) { return A.Key < B.Key; });
		Listeners.SetNum(MaxVoiceListeners);
	}
	for (const TPair<double, AMP_GamePlayerController*>& Listener : Listeners)
	{
		Listener.Value->ClientReceiveVoice(CompressedData, SenderSampleRate, SpeakerActor);
	}
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

	LastRemoteVoiceTime = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;

	// Defensa en el consumidor: acotar el sample rate recibido por red al rango
	// humano antes de configurar el playback.
	SenderSampleRate = FMath::Clamp(SenderSampleRate <= 0 ? 48000 : SenderSampleRate, 8000, 96000);

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

