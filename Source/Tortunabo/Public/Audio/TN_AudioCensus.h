#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TN_AudioCensus.generated.h"

/**
 * Recuento del sonido (#737): cuántos sonidos activos, voces del mezclador y componentes de audio hay vivos a lo largo de
 * una partida, para ver si algo crece sin parar o se queda sin voz (la música, el sonido de la propia tortuga).
 *
 * Lógica pura del veredicto aquí (probada en Tortunabo.Audio.Census.*); el recuento en sí, en UTN_AudioCensusSubsystem.
 */
namespace TNAudioCensus
{
	/** Veredicto de una serie de recuentos tomados a intervalos regulares. */
	struct FTrend
	{
		/** Muestras analizadas (tras el calentamiento). Con menos de MinSamples no hay veredicto. */
		int32 Samples = 0;
		/** Mediana del primer y del último tercio de las muestras analizadas. */
		float FirstMedian = 0.f;
		float LastMedian = 0.f;
		/** Máximo de toda la serie analizada. */
		int32 Peak = 0;
		/** La mediana del último tercio supera a la del primero en más de lo tolerado. */
		bool bGrowing = false;
		/** Hay muestras suficientes para decidir. */
		bool bDecided = false;
	};

	/** Muestras mínimas tras el calentamiento para dar un veredicto (dos por tercio). */
	constexpr int32 MinSamples = 6;

	/**
	 * Crece si la mediana del último tercio supera a la del primero en más de max(Tolerance, Relative * primera mediana).
	 * Las medianas de tercios no se dejan engañar por un pico suelto (una explosión, el podio) ni por el ir y venir de
	 * las rondas: solo una subida sostenida cuenta.
	 */
	TORTUNABO_API FTrend AnalyzeTrend(TConstArrayView<int32> Series, int32 WarmupSamples, float Tolerance, float Relative);

	/** Mediana de una serie (0 si está vacía). */
	TORTUNABO_API float Median(TConstArrayView<int32> Series);
}

/**
 * Recuento periódico del sonido de un mundo de juego (fuera de Shipping). En cada muestra:
 *  - del dispositivo de audio (hilo de audio): sonidos activos, ondas, voces en uso y sin voz, y su tope (MaxChannels);
 *  - del mundo (hilo de juego): componentes de audio vivos y sonando, sintetizadores vivos y activos por clase;
 *  - la música de la carrera y el sonido de la tortuga local: si están activos y si tienen voz.
 * Escribe una línea [AudioCensus] por muestra y, con -TNAudioCensusOut o «csv», un CSV; al parar, el veredicto
 * (TNAudioCensus::AnalyzeTrend) de cada serie.
 *
 * Uso: TN.Audio.Census (una muestra), TN.Audio.Census start [intervalo=10] [csv], TN.Audio.Census stop; o
 * -TNAudioCensus[=intervalo] [-TNAudioCensusOut=ruta.csv] [-TNAudioCensusSeconds=s] en la línea de órdenes.
 * Necesita un dispositivo de audio: con -nosound no hay ninguno (solo cuenta componentes, y ni eso se crea). Para medir
 * sin altavoces: -DeterministicAudio (mezclador sin salida) en lugar de -nosound. Ver Docs/Comandos_Prueba.md.
 */
UCLASS()
class TORTUNABO_API UTN_AudioCensusSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Empieza a muestrear cada InIntervalSeconds; con InCsvPath no vacío, también al CSV. InMaxSeconds > 0: para solo. */
	void StartCensus(float InIntervalSeconds, const FString& InCsvPath, float InMaxSeconds);
	/** Para y escribe el veredicto. */
	void StopCensus(const TCHAR* InReason);
	/** Una muestra suelta (se escribe cuando vuelve del hilo de audio). */
	void RequestSample();
	bool IsRunning() const { return bRunning; }

	/** Fila de una muestra (también la del CSV). */
	struct FRow
	{
		double Seconds = 0.0;
		FString Phase;
		int32 ActiveSounds = 0;
		int32 WaveInstances = 0;
		int32 Voices = 0;
		int32 MaxChannels = 0;
		/** Sonidos activos con ondas pero sin ninguna voz (los que el tope de voces ha dejado mudos). */
		int32 Voiceless = 0;
		/** Sonidos activos con bAlwaysPlay (prioridad máxima: ganan a todo lo demás). */
		int32 AlwaysPlay = 0;
		int32 AudioComponents = 0;
		int32 AudioComponentsPlaying = 0;
		int32 Synths = 0;
		int32 SynthsActive = 0;
		/** -1 = no hay; 0 = activa sin voz; 1 = activa con voz; 2 = parada (callada a propósito). */
		int32 MusicState = -1;
		int32 FoleyState = -1;
		FString Top;
	};

private:
	struct FAudioSnapshot;
	void CompleteSample(const FAudioSnapshot& InSnapshot);
	void WriteVerdict(const TCHAR* InReason);

	TArray<FRow> Rows;
	FString CsvPath;
	double StartSeconds = 0.0;
	double NextSampleSeconds = 0.0;
	double StopAtSeconds = 0.0;
	float IntervalSeconds = 10.f;
	bool bRunning = false;
	bool bSampleInFlight = false;
};
