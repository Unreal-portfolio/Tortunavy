#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "Components/AudioComponent.h"
#include "AudioCaptureCore.h"
#include "Voice/TN_VoiceRate.h"
#include "ProximityVoiceComponent.generated.h"

class APlayerState;
class UUserWidget;
class FTNVoiceDeviceCapture;
class APawn;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSpeakingChanged, bool, bIsSpeaking);

/**
 * @brief Componente de voz por proximidad (VOIP) basado en WASAPI.
 *
 * Captura audio local con FAudioCaptureSynth, lo downsampea y comprime,
 * lo envía al servidor (Server_SendVoiceData) y éste lo multicastea a los
 * clientes cercanos. La atenuación se aplica en el cliente receptor según
 * la distancia entre el emisor y el listener (InnerRadius / OuterRadius).
 *
 * @warning Para detener la captura antes de un ServerTravel hay que llamar a
 *          ShutdownAllCapture(World) ANTES del travel. Hacerlo desde EndPlay
 *          tras teardown causa ACCESS_VIOLATION en WASAPI.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TORTUNABO_API UProximityVoiceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProximityVoiceComponent();

	/** @brief Inicia captura WASAPI si el componente es local-owned y crea el widget indicador. */
	virtual void BeginPlay() override;

	/** @brief Cierra captura y libera recursos de audio respetando el ciclo seguro de WASAPI. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** @brief Hook adicional para asegurar limpieza si el componente se desregistra antes de EndPlay. */
	virtual void OnUnregister() override;

	/** @brief Última red de seguridad: marca bIsShuttingDown antes de la destrucción del UObject. */
	virtual void BeginDestroy() override;

	/** @brief Bombea el buffer de captura, detecta speaking activity y comprime/envía paquetes al servidor. */
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * @brief Detiene la captura de audio de forma segura mientras WASAPI sigue vivo.
	 *        Debe llamarse ANTES de que ServerTravel inicie el teardown del mundo.
	 *        Después de esta llamada, EndPlay se convierte en un no-op para los recursos de audio.
	 */
	void PrepareForLevelTransition();

	/**
	 * @brief Encuentra todos los ProximityVoiceComponent del mundo y llama a PrepareForLevelTransition() en cada uno.
	 * @param World Mundo cuyos componentes se quieren detener.
	 * @note Llamar desde los GameModes justo ANTES de ServerTravel(). Ver @warning del UCLASS.
	 */
	static void ShutdownAllCapture(const UWorld* World);

	UPROPERTY(BlueprintAssignable, Category = "Voice")
	FOnSpeakingChanged OnSpeakingChanged;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Voice")
	bool bIsSpeaking = false;

	/**
	 * @brief Si esta tortuga está hablando vista desde esta máquina: la propia, por bIsSpeaking; la de otro jugador,
	 * si ha llegado audio suyo en las últimas 0,35 s (bIsSpeaking no llega a los demás). Lo usa el HUD en código.
	 */
	bool IsHeardSpeaking() const;

	/**
	 * @brief El jugador al que pertenece esta voz: el PlayerState de su peón y, si el peón ya no tiene (sin poseer, o aún sin
	 *        replicar), el último que se le vio. Con él se mira si quien escucha lo tiene silenciado (#248); sin él la voz
	 *        saldría sin silenciar, porque no se sabría de quién es.
	 */
	const APlayerState* GetSpeakerState() const;

	/** @brief true si quien escucha (esta máquina) tiene silenciado al dueño de esta voz (ajustes del menú de pausa). */
	bool IsMutedByListener() const;

	/**
	 * @brief Nivel del micrófono propio (RMS del último bloque capturado, ya con VoiceGain) para el medidor del menú de
	 *        pausa. 0 si esta tortuga no captura.
	 */
	float GetMicLevel() const { return MicLevel; }

	/** @brief true si esta tortuga (la local) tiene el micrófono abierto y capturando. */
	bool IsCapturing() const { return (AudioCaptureSynth.IsValid() || DeviceCapture != nullptr) && !bIsShuttingDown; }

	/**
	 * @brief Micrófono elegido por el jugador (id del dispositivo de Windows; vacío = el predeterminado). Lo pone
	 *        UTN_GameSettingsSubsystem y se usa al abrir la captura (BeginPlay): cambiarlo con la voz ya abierta surte
	 *        efecto al reaparecer o al cambiar de mapa, porque la captura WASAPI abierta no se puede cerrar sin riesgo.
	 */
	static void SetPreferredCaptureDevice(const FString& DeviceId);
	static FString GetPreferredCaptureDevice();

	/** @brief Micrófonos activos de Windows: id y nombre (en el orden de los índices de captura). */
	static void GetCaptureDevices(TArray<TPair<FString, FString>>& OutDevices);

	/** @brief Micrófono con el que se abrió la captura de esta tortuga (vacío = el predeterminado). */
	const FString& GetOpenCaptureDevice() const { return OpenCaptureDevice; }

	/**
	 * @brief Deja salir o no la voz propia (UTN_GameSettingsSubsystem: silenciarse y pulsar para hablar). Cerrada se sigue
	 *        capturando (el medidor sigue vivo), pero la tortuga deja de «hablar» en el acto y no se envía nada.
	 */
	void SetTransmitEnabled(bool bEnabled) { bTransmitEnabled = bEnabled; }
	bool IsTransmitEnabled() const { return bTransmitEnabled; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Attenuation")
	float InnerRadius = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Attenuation")
	float OuterRadius = 2500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Detection")
	float SpeakingThreshold = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Voice|Audio")
	int32 VoiceSampleRate = 48000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Audio")
	int32 VoiceNumChannels = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Network")
	float SendInterval = 0.08f;

	/**
	 * Frecuencia objetivo (Hz) a la que se envía la voz. El micrófono se reduce por el factor entero
	 * max(1, frecuencia REAL de la captura / esta) y se etiqueta con la frecuencia que resulta (TNVoiceRate::MakeSendPlan):
	 * 48 kHz → 16 kHz (voz de banda ancha, como un teléfono bueno: 16 KB/s por quien habla en vez de 24), 44,1 kHz → 22,05 kHz,
	 * 16 kHz → 16 kHz (sin reducir). Lo enviado queda entre esta frecuencia y el doble, nunca por debajo (el servidor y el
	 * receptor solo aceptan de 8000 a 96000 Hz). Con ocho jugadores hablando a la vez el anfitrión reenviaba más de 1 MB/s.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Network", meta = (ClampMin = "8000", ClampMax = "48000"))
	int32 VoiceTargetSampleRate = TNVoiceRate::DefaultTargetRate;

	/**
	 * Oyentes como mucho por paquete: el servidor reenvía la voz solo a los más cercanos dentro de OuterRadius (con ocho
	 * tortugas juntas en la salida, cada una iba a las otras siete). 0 = sin tope.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Network", meta = (ClampMin = "0", ClampMax = "15"))
	int32 MaxVoiceListeners = 4;

	/**
	 * @brief Reproduce datos de voz remotos recibidos en este componente.
	 * @param CompressedData Payload comprimido tal y como vino del servidor.
	 * @param SenderSampleRate SampleRate original del emisor (para resample si difiere).
	 * @note Lo llaman los PlayerController que reciben voz (ITN_VoiceListener) tras el filtro del servidor.
	 */
	void PlayRemoteVoice(const TArray<uint8>& CompressedData, int32 SenderSampleRate);

	/**
	 * @brief Servidor: añade la voz a Pawn si aún no la tiene (lo hacen los PlayerController en OnPossess). En la máquina
	 *        del jugador el micrófono se abre cuando el peón pasa a ser suyo.
	 * @return El componente del peón, o nullptr fuera del servidor.
	 */
	static UProximityVoiceComponent* EnsureOn(APawn* Pawn);

	/** Momento (tiempo real del mundo) en que llegó el último paquete de voz de esta tortuga a esta máquina. */
	double LastRemoteVoiceTime = -1.0;

	/**
	 * Segundos de silencio continuo antes de marcar bIsSpeaking = false.
	 * Evita que el flag rebote rápidamente generando tráfico de replicación.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Detection", meta = (ClampMin = "0.0"))
	float SilenceHoldOffSeconds = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Audio")
	float VoiceGain = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Audio")
	float PlaybackVolume = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|UI")
	TSubclassOf<UUserWidget> VoiceIndicatorWidgetClass;

protected:
	/**
	 * @brief RPC al servidor con un paquete de voz comprimido para que lo retransmita.
	 * @param CompressedData Buffer comprimido.
	 * @param SenderSampleRate SampleRate del cliente emisor.
	 */
	UFUNCTION(Server, Unreliable, WithValidation)
	void Server_SendVoiceData(const TArray<uint8>& CompressedData, int32 SenderSampleRate);

private:
	/** @brief Cap de payload de voz compartido por cliente (envío) y servidor (validación + límite). */
	static constexpr int32 MaxVoicePayloadBytes = 8192;

	TUniquePtr<Audio::FAudioCaptureSynth> AudioCaptureSynth;
	TArray<float> CaptureBuffer;
	FCriticalSection CaptureBufferLock;
	float SendTimer = 0.f;
	float SilenceHoldOffTimer = 0.f;
	int32 CaptureNumChannels = 1;

	/**
	 * Frecuencia real de lo que entrega la captura, medida con las muestras que llegan (#154): la del dispositivo
	 * (VoiceSampleRate) puede no ser la del flujo, y la voz etiquetada con ella se oía aguda y acelerada.
	 */
	TNVoiceRate::FCaptureRateMeter CaptureRateMeter;

	/**
	 * Frecuencia de la captura con la que se hace el plan de envío, o 0 si todavía no se puede enviar voz: la medida en cuanto
	 * hay primera medida (unos 0,5 s de audio); si en FCaptureRateMeter::GiveUpSeconds no la hay, la del dispositivo.
	 */
	int32 GetCaptureSampleRate() const { return TNVoiceRate::ResolveCaptureRate(CaptureRateMeter.Rate, CaptureRateMeter.bGaveUp, VoiceSampleRate); }

	/**
	 * Cómo se reduce y se etiqueta lo que se captura ahora (#154): ÚNICA fuente del factor con el que se reducen las muestras
	 * y de la frecuencia con la que salen. Solo cambia en ApplySendPlan, que vacía lo acumulado: un paquete nunca mezcla
	 * muestras reducidas con dos factores distintos ni sale con una frecuencia que no es la suya.
	 */
	TNVoiceRate::FSendPlan ActiveSendPlan;

	/** Muestras que sobraron en la última reducción (menos que el factor); se suman al bloque siguiente. */
	TArray<float> DecimationCarry;

	/**
	 * @brief Adopta el plan de envío de una captura a CaptureRate Hz: vacía CaptureBuffer y el sobrante de la reducción
	 *        (eran del plan anterior) y lo anota en el registro.
	 */
	void ApplySendPlan(int32 CaptureRate);

	/** Frecuencia de la onda con la que se reproduce la voz de esta tortuga en esta máquina (0 sin onda). */
	int32 PlaybackSampleRate = 0;

	/** La onda procedural de la voz a InSampleRate, mono y en bucle indefinido. */
	USoundWaveProcedural* CreateVoiceWave(int32 InSampleRate);

	UPROPERTY()
	TObjectPtr<UAudioComponent> PlaybackAudioComponent;

	UPROPERTY()
	TObjectPtr<USoundWaveProcedural> ProceduralSoundWave;

	/** @brief Abre el micrófono (el elegido o el predeterminado). Una sola vez: en BeginPlay o al pasar a ser local. */
	void OpenCapture();

	/** @brief Servidor: reenvía un paquete a los oyentes que tocan (TNVoiceRouting::SelectListeners). */
	void RelayVoiceToListeners(const TArray<uint8>& CompressedData, int32 SenderSampleRate);

	/** @brief Atenuación del playback por distancia (de InnerRadius a OuterRadius). */
	FSoundAttenuationSettings MakeAttenuation() const;

	/** Ya se ha intentado abrir el micrófono (con éxito o no): no se repite cada fotograma. */
	bool bCaptureOpenAttempted = false;

	/** @brief Inicializa la SoundWaveProcedural y el AudioComponent de playback con el sample rate dado. */
	void SetupPlayback(int32 InSampleRate = 0);

	/**
	 * @brief Libera AudioCaptureSynth + buffers en orden seguro respetando WASAPI.
	 * @param bForceLeakAudio Si true, no destruye AudioComponent (evita crash si el subsistema ya cerró).
	 */
	void CleanupRuntimeResources(bool bForceLeakAudio = false);

	/** @brief Comprime un array de samples float a uint8 (codec ligero in-house para VOIP). */
	static TArray<uint8> CompressSamples(const TArray<float>& Samples);

	/** @brief Decodifica un array de uint8 vuelta a samples float. */
	static TArray<float> DecompressSamples(const TArray<uint8>& Compressed);

	/** @brief Devuelve true si el componente pertenece al jugador local (autoridad de input/audio). */
	bool IsLocallyOwned() const;

	UPROPERTY()
	TObjectPtr<UUserWidget> VoiceIndicatorWidgetInstance;

	bool bIsShuttingDown = false;
	bool bRuntimeResourcesCleanedUp = false;

	/** Nivel RMS del último bloque capturado (GetMicLevel) y si la voz propia puede salir (SetTransmitEnabled). */
	float MicLevel = 0.f;
	bool bTransmitEnabled = true;

	/**
	 * Captura de un micrófono concreto (el elegido en el menú de pausa) en vez de AudioCaptureSynth. Como esa, nunca se
	 * para ni se destruye: al limpiar se suelta (puntero sin dueño a propósito).
	 */
	FTNVoiceDeviceCapture* DeviceCapture = nullptr;

	/** Micrófono elegido con el que se abrió DeviceCapture (vacío con el predeterminado). */
	FString OpenCaptureDevice;

	/** @brief Abre el micrófono elegido (SetPreferredCaptureDevice) si hay uno y sigue conectado. */
	bool OpenPreferredCaptureDevice();

	/** Rate limiting server-side para paquetes de voz (evita flooding). */
	float LastVoicePacketServerTime = -1.f;

	/** Último PlayerState que tuvo el peón dueño (GetSpeakerState) y si ya se avisó de que habla sin ninguno. */
	mutable TWeakObjectPtr<const APlayerState> LastSpeakerState;
	bool bWarnedNoSpeakerState = false;

	/** @brief Crea e inserta el widget VoiceIndicator en el HUD del jugador local. */
	void CreateVoiceIndicatorHUD();
};
