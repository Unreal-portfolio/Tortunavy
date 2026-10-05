#include "Audio/TN_AudioCensus.h"

#include "ActiveSound.h"
#include "Audio.h"
#include "Audio/TN_RaceMusicComponent.h"
#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "AudioThread.h"
#include "Components/AudioComponent.h"
#include "Components/SynthComponent.h"
#include "Core/TN_Log.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Game/TN_BeachRaceGameState.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/TN_TurtleFoleyComponent.h"
#include "Sound/SoundBase.h"
#include "Testing/TN_TestReport.h"
#include "UObject/UObjectIterator.h"

// ─────────────────────────────────────────────────────────────────────────────
// Veredicto (lógica pura)
// ─────────────────────────────────────────────────────────────────────────────

float TNAudioCensus::Median(TConstArrayView<int32> Series)
{
	if (Series.Num() == 0)
	{
		return 0.f;
	}
	TArray<int32> Sorted(Series.GetData(), Series.Num());
	Sorted.Sort();
	const int32 Mid = Sorted.Num() / 2;
	return (Sorted.Num() % 2 == 1) ? static_cast<float>(Sorted[Mid]) : 0.5f * static_cast<float>(Sorted[Mid - 1] + Sorted[Mid]);
}

TNAudioCensus::FTrend TNAudioCensus::AnalyzeTrend(TConstArrayView<int32> Series, int32 WarmupSamples, float Tolerance, float Relative)
{
	FTrend Out;
	const int32 Skip = FMath::Clamp(WarmupSamples, 0, Series.Num());
	const TConstArrayView<int32> Used = Series.Slice(Skip, Series.Num() - Skip);
	Out.Samples = Used.Num();
	for (const int32 Value : Used)
	{
		Out.Peak = FMath::Max(Out.Peak, Value);
	}
	if (Out.Samples < MinSamples)
	{
		return Out;
	}
	const int32 Third = Out.Samples / 3;
	Out.FirstMedian = Median(Used.Slice(0, Third));
	Out.LastMedian = Median(Used.Slice(Out.Samples - Third, Third));
	const float Allowed = FMath::Max(FMath::Max(0.f, Tolerance), FMath::Max(0.f, Relative) * Out.FirstMedian);
	Out.bGrowing = Out.LastMedian - Out.FirstMedian > Allowed;
	Out.bDecided = true;
	return Out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Recuento en el motor
// ─────────────────────────────────────────────────────────────────────────────

/** Lo que se lee en el hilo de audio: un registro por sonido activo. */
struct UTN_AudioCensusSubsystem::FAudioSnapshot
{
	struct FSound
	{
		uint64 ComponentId = 0;
		FName SoundName;
		int32 Waves = 0;
		bool bVoice = false;
		bool bAlwaysPlay = false;
	};
	TArray<FSound> Sounds;
	int32 WaveInstances = 0;
	int32 Voices = 0;
	int32 MaxChannels = 0;
	bool bDevice = false;
};

namespace TNAudioCensusLocal
{
	/** Muestras de calentamiento que no cuentan para el veredicto (la ronda aún se está montando). */
	constexpr int32 WarmupSamples = 3;

	UTN_AudioCensusSubsystem* FromWorld(UWorld* World)
	{
		return World ? World->GetSubsystem<UTN_AudioCensusSubsystem>() : nullptr;
	}

	/** Nombre corto de una clase de sintetizador: sin «TN_» delante ni «Component» detrás. */
	FString ShortClassName(const UClass* InClass)
	{
		FString Name = InClass ? InClass->GetName() : FString(TEXT("?"));
		Name.RemoveFromStart(TEXT("TN_"));
		Name.RemoveFromEnd(TEXT("Component"));
		return Name;
	}

	/** Etiqueta de un componente de audio: la clase del sintetizador al que pertenece o el nombre del sonido. */
	FString LabelFor(const UAudioComponent* Comp, FName SoundName)
	{
		if (Comp)
		{
			if (const USynthComponent* Synth = Cast<USynthComponent>(Comp->GetOuter()))
			{
				return ShortClassName(Synth->GetClass());
			}
			if (Comp->Sound)
			{
				return Comp->Sound->GetName();
			}
		}
		return SoundName.IsNone() ? FString(TEXT("(sin componente)")) : SoundName.ToString();
	}

	const TCHAR* StateText(int32 State)
	{
		switch (State)
		{
		case 0: return TEXT("SIN VOZ");
		case 1: return TEXT("con voz");
		case 2: return TEXT("parada");
		default: return TEXT("no hay");
		}
	}

	void HandleCommand(const TArray<FString>& Args, UWorld* World)
	{
		UTN_AudioCensusSubsystem* Census = FromWorld(World);
		if (!Census)
		{
			UE_LOG(LogTortunabo, Display, TEXT("[AudioCensus] Solo en un mundo de juego (y fuera de Shipping)."));
			return;
		}
		const FString Verb = Args.Num() > 0 ? Args[0].ToLower() : FString();
		if (Verb == TEXT("stop"))
		{
			Census->StopCensus(TEXT("parado a mano"));
			return;
		}
		if (Verb == TEXT("start"))
		{
			const float Interval = Args.IsValidIndex(1) ? FCString::Atof(*Args[1]) : 10.f;
			const bool bCsv = Args.IsValidIndex(2) && Args[2].Equals(TEXT("csv"), ESearchCase::IgnoreCase);
			Census->StartCensus(Interval, bCsv ? TNTestReport::DefaultPath(TEXT("AudioCensus")).Replace(TEXT(".json"), TEXT(".csv")) : FString(), 0.f);
			return;
		}
		Census->RequestSample();
	}

	FAutoConsoleCommandWithWorldAndArgs CensusCommand(
		TEXT("TN.Audio.Census"),
		TEXT("Recuento del sonido (#737): sonidos activos, voces en uso y sin voz, componentes de audio y sintetizadores vivos, y si la música y la tortuga local tienen voz. ")
		TEXT("TN.Audio.Census (una muestra) | TN.Audio.Census start [intervalo=10] [csv] | TN.Audio.Census stop (veredicto)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&HandleCommand));
}

bool UTN_AudioCensusSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_DedicatedServer && Super::ShouldCreateSubsystem(Outer);
#endif
}

void UTN_AudioCensusSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	// -TNAudioCensus[=intervalo] [-TNAudioCensusOut=ruta.csv] [-TNAudioCensusSeconds=s]
	FString Interval;
	const bool bWanted = FParse::Value(FCommandLine::Get(), TEXT("-TNAudioCensus="), Interval) || FParse::Param(FCommandLine::Get(), TEXT("TNAudioCensus"));
	if (!bWanted || InWorld.GetMapName().Contains(TEXT("Menu")))
	{
		return;
	}
	FString Out = TNTestReport::CommandLineValue(TEXT("-TNAudioCensusOut"));
	if (Out.IsEmpty())
	{
		Out = FPaths::ProjectSavedDir() / TEXT("AudioCensus") / FString::Printf(TEXT("%s_%s.csv"), *TNTestReport::NetModeName(&InWorld),
			*FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H-%M-%S")));
	}
	const FString Seconds = TNTestReport::CommandLineValue(TEXT("-TNAudioCensusSeconds"));
	StartCensus(Interval.IsEmpty() ? 10.f : FCString::Atof(*Interval), Out, Seconds.IsEmpty() ? 0.f : FCString::Atof(*Seconds));
}

void UTN_AudioCensusSubsystem::Deinitialize()
{
	if (bRunning)
	{
		StopCensus(TEXT("el mundo se ha cerrado"));
	}
	Super::Deinitialize();
}

TStatId UTN_AudioCensusSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTN_AudioCensusSubsystem, STATGROUP_Tickables);
}

void UTN_AudioCensusSubsystem::StartCensus(float InIntervalSeconds, const FString& InCsvPath, float InMaxSeconds)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	IntervalSeconds = FMath::Clamp(InIntervalSeconds, 0.5f, 600.f);
	CsvPath = InCsvPath;
	Rows.Reset();
	StartSeconds = World->GetRealTimeSeconds();
	NextSampleSeconds = StartSeconds;
	StopAtSeconds = InMaxSeconds > 0.f ? StartSeconds + InMaxSeconds : 0.0;
	bRunning = true;
	if (!CsvPath.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(CsvPath), true);
		FFileHelper::SaveStringToFile(
			TEXT("segundos,fase,sonidos_activos,ondas,voces,tope_voces,sin_voz,siempre_suena,componentes_audio,componentes_sonando,sintes,sintes_activos,musica,tortuga,top\n"),
			*CsvPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	UE_LOG(LogTortunabo, Display, TEXT("[AudioCensus] En marcha en %s (%s): una muestra cada %.1f s%s%s."), *World->GetMapName(),
		*TNTestReport::NetModeName(World), IntervalSeconds, CsvPath.IsEmpty() ? TEXT("") : TEXT(", CSV en "), *CsvPath);
}

void UTN_AudioCensusSubsystem::StopCensus(const TCHAR* InReason)
{
	if (!bRunning)
	{
		return;
	}
	bRunning = false;
	WriteVerdict(InReason);
}

void UTN_AudioCensusSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const UWorld* World = GetWorld();
	if (!bRunning || !World || World->bIsTearingDown)
	{
		return;
	}
	const double Now = World->GetRealTimeSeconds();
	if (StopAtSeconds > 0.0 && Now >= StopAtSeconds)
	{
		StopCensus(TEXT("tiempo cumplido"));
		return;
	}
	if (Now >= NextSampleSeconds && !bSampleInFlight)
	{
		NextSampleSeconds = Now + IntervalSeconds;
		RequestSample();
	}
}

void UTN_AudioCensusSubsystem::RequestSample()
{
	UWorld* World = GetWorld();
	FAudioDevice* Device = World ? World->GetAudioDeviceRaw() : nullptr;
	TSharedRef<FAudioSnapshot, ESPMode::ThreadSafe> Snapshot = MakeShared<FAudioSnapshot, ESPMode::ThreadSafe>();
	if (!Device)
	{
		CompleteSample(*Snapshot);
		return;
	}
	bSampleInFlight = true;
	const Audio::FDeviceId DeviceId = Device->DeviceID;
	TWeakObjectPtr<UTN_AudioCensusSubsystem> WeakThis(this);
	FAudioThread::RunCommandOnAudioThread([WeakThis, DeviceId, Snapshot]()
	{
		FAudioDeviceManager* Manager = FAudioDeviceManager::Get();
		if (FAudioDevice* AudioDevice = Manager ? Manager->GetAudioDeviceRaw(DeviceId) : nullptr)
		{
			Snapshot->bDevice = true;
			Snapshot->MaxChannels = AudioDevice->GetMaxChannels();
			const TMap<FWaveInstance*, FSoundSource*>& SourceMap = AudioDevice->GetWaveInstanceSourceMap();
			Snapshot->Voices = SourceMap.Num();
			for (const FActiveSound* Active : AudioDevice->GetActiveSounds())
			{
				if (!Active)
				{
					continue;
				}
				FAudioSnapshot::FSound& Row = Snapshot->Sounds.AddDefaulted_GetRef();
				Row.ComponentId = Active->GetAudioComponentID();
				Row.SoundName = Active->GetSound() ? Active->GetSound()->GetFName() : NAME_None;
				Row.bAlwaysPlay = Active->GetAlwaysPlay();
				for (const TPair<UPTRINT, FWaveInstance*>& Pair : Active->GetWaveInstances())
				{
					++Row.Waves;
					Row.bVoice |= Pair.Value && SourceMap.Contains(Pair.Value);
				}
				Snapshot->WaveInstances += Row.Waves;
			}
		}
		FAudioThread::RunCommandOnGameThread([WeakThis, Snapshot]()
		{
			if (UTN_AudioCensusSubsystem* Self = WeakThis.Get())
			{
				Self->bSampleInFlight = false;
				Self->CompleteSample(*Snapshot);
			}
		});
	});
}

void UTN_AudioCensusSubsystem::CompleteSample(const FAudioSnapshot& InSnapshot)
{
	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown)
	{
		return;
	}
	using namespace TNAudioCensusLocal;
	FRow Row;
	Row.Seconds = World->GetRealTimeSeconds() - StartSeconds;
	if (const ATN_BeachRaceGameState* Race = World->GetGameState<ATN_BeachRaceGameState>())
	{
		Row.Phase = FString::Printf(TEXT("R%d %s"), Race->CurrentRound, *StaticEnum<ETNBeachRacePhase>()->GetNameStringByValue(static_cast<int64>(Race->RacePhase)));
	}
	else
	{
		Row.Phase = World->GetMapName();
	}

	// Dispositivo: sonidos activos, con y sin voz, por etiqueta.
	TMap<uint64, const FAudioSnapshot::FSound*> ById;
	struct FCount { int32 Active = 0; int32 Voiceless = 0; };
	TMap<FString, FCount> ByLabel;
	Row.ActiveSounds = InSnapshot.Sounds.Num();
	Row.WaveInstances = InSnapshot.WaveInstances;
	Row.Voices = InSnapshot.Voices;
	Row.MaxChannels = InSnapshot.MaxChannels;
	for (const FAudioSnapshot::FSound& Sound : InSnapshot.Sounds)
	{
		if (Sound.ComponentId != 0)
		{
			ById.Add(Sound.ComponentId, &Sound);
		}
		const bool bMute = Sound.Waves > 0 && !Sound.bVoice;
		Row.Voiceless += bMute ? 1 : 0;
		Row.AlwaysPlay += Sound.bAlwaysPlay ? 1 : 0;
		FCount& Count = ByLabel.FindOrAdd(LabelFor(Sound.ComponentId ? UAudioComponent::GetAudioComponentFromID(Sound.ComponentId) : nullptr, Sound.SoundName));
		++Count.Active;
		Count.Voiceless += bMute ? 1 : 0;
	}

	// Mundo: componentes de audio y sintetizadores vivos.
	for (TObjectIterator<UAudioComponent> It; It; ++It)
	{
		const UAudioComponent* Comp = *It;
		if (IsValid(Comp) && Comp->GetWorld() == World)
		{
			++Row.AudioComponents;
			Row.AudioComponentsPlaying += Comp->IsPlaying() ? 1 : 0;
		}
	}
	for (TObjectIterator<USynthComponent> It; It; ++It)
	{
		const USynthComponent* Synth = *It;
		if (IsValid(Synth) && Synth->GetWorld() == World)
		{
			++Row.Synths;
			Row.SynthsActive += Synth->IsActive() ? 1 : 0;
		}
	}

	// La música de la carrera y el sonido de la tortuga local: activos y con voz.
	auto StateOf = [&ById](USynthComponent* Synth) -> int32
	{
		if (!Synth)
		{
			return -1;
		}
		if (!Synth->IsActive())
		{
			return 2;
		}
		const UAudioComponent* Audio = Synth->GetAudioComponent();
		const FAudioSnapshot::FSound* const* Found = Audio ? ById.Find(Audio->GetAudioComponentID()) : nullptr;
		return (Found && (*Found)->bVoice) ? 1 : 0;
	};
	APlayerController* LocalPC = World->GetFirstPlayerController();
	if (LocalPC && LocalPC->IsLocalController())
	{
		Row.MusicState = StateOf(LocalPC->FindComponentByClass<UTN_RaceMusicComponent>());
		const APawn* Pawn = LocalPC->GetPawn();
		Row.FoleyState = StateOf(Pawn ? Pawn->FindComponentByClass<UTN_TurtleFoleyComponent>() : nullptr);
	}

	// Lo más numeroso (activos/sin voz), para ver de un vistazo qué ocupa las voces.
	ByLabel.ValueSort([](const FCount& A, const FCount& B) { return A.Active > B.Active; });
	int32 Listed = 0;
	for (const TPair<FString, FCount>& Pair : ByLabel)
	{
		if (Listed++ >= 8)
		{
			break;
		}
		Row.Top += FString::Printf(TEXT("%s%s %d/%d"), Row.Top.IsEmpty() ? TEXT("") : TEXT(" · "), *Pair.Key, Pair.Value.Active, Pair.Value.Voiceless);
	}

	UE_LOG(LogTortunabo, Display,
		TEXT("[AudioCensus] %5.0f s %s | sonidos %d (siempre suenan %d), voces %d/%d, sin voz %d | componentes %d (sonando %d), sintes %d (activos %d) | música %s, tortuga %s | %s%s"),
		Row.Seconds, *Row.Phase, Row.ActiveSounds, Row.AlwaysPlay, Row.Voices, Row.MaxChannels, Row.Voiceless, Row.AudioComponents,
		Row.AudioComponentsPlaying, Row.Synths, Row.SynthsActive, StateText(Row.MusicState), StateText(Row.FoleyState), *Row.Top,
		InSnapshot.bDevice ? TEXT("") : TEXT(" (sin dispositivo de audio: -nosound)"));

	if (!bRunning)
	{
		return;
	}
	if (!CsvPath.IsEmpty())
	{
		const FString Line = FString::Printf(TEXT("%.1f,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,\"%s\"\n"), Row.Seconds, *Row.Phase, Row.ActiveSounds,
			Row.WaveInstances, Row.Voices, Row.MaxChannels, Row.Voiceless, Row.AlwaysPlay, Row.AudioComponents, Row.AudioComponentsPlaying, Row.Synths,
			Row.SynthsActive, Row.MusicState, Row.FoleyState, *Row.Top);
		FFileHelper::SaveStringToFile(Line, *CsvPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
	}
	Rows.Add(MoveTemp(Row));
}

void UTN_AudioCensusSubsystem::WriteVerdict(const TCHAR* InReason)
{
	using namespace TNAudioCensus;
	auto Series = [this](int32 FRow::*Field)
	{
		TArray<int32> Out;
		Out.Reserve(Rows.Num());
		for (const FRow& Row : Rows) { Out.Add(Row.*Field); }
		return Out;
	};
	auto Report = [](const TCHAR* Name, const FTrend& Trend) -> bool
	{
		UE_LOG(LogTortunabo, Display, TEXT("[AudioCensus]   %-22s primer tercio %5.1f, último %5.1f, máximo %4d: %s"), Name, Trend.FirstMedian, Trend.LastMedian, Trend.Peak,
			!Trend.bDecided ? TEXT("sin muestras suficientes") : (Trend.bGrowing ? TEXT("CRECE") : TEXT("estable")));
		return Trend.bDecided && !Trend.bGrowing;
	};
	UE_LOG(LogTortunabo, Display, TEXT("[AudioCensus] Fin (%s): %d muestras."), InReason, Rows.Num());
	// Tolerancia: 4 sonidos o un 25 % (el reparto de cada ronda cambia cuántos bichos hay cerca).
	bool bStable = true;
	bStable &= Report(TEXT("sonidos activos"), AnalyzeTrend(Series(&FRow::ActiveSounds), TNAudioCensusLocal::WarmupSamples, 4.f, 0.25f));
	bStable &= Report(TEXT("sin voz"), AnalyzeTrend(Series(&FRow::Voiceless), TNAudioCensusLocal::WarmupSamples, 2.f, 0.25f));
	bStable &= Report(TEXT("sintes activos"), AnalyzeTrend(Series(&FRow::SynthsActive), TNAudioCensusLocal::WarmupSamples, 4.f, 0.25f));
	bStable &= Report(TEXT("componentes sonando"), AnalyzeTrend(Series(&FRow::AudioComponentsPlaying), TNAudioCensusLocal::WarmupSamples, 4.f, 0.25f));
	// Los componentes vivos (sonando o no) son informativos: en un cliente crecen a lo largo de la ronda con los actores de
	// la playa que le van llegando por red y se liberan al rehacerla; lo que cuenta es lo que suena.
	Report(TEXT("componentes vivos (info)"), AnalyzeTrend(Series(&FRow::AudioComponents), TNAudioCensusLocal::WarmupSamples, 8.f, 0.25f));
	int32 MusicSamples = 0;
	int32 MusicMute = 0;
	int32 FoleySamples = 0;
	int32 FoleyMute = 0;
	for (const FRow& Row : Rows)
	{
		MusicSamples += (Row.MusicState == 0 || Row.MusicState == 1) ? 1 : 0;
		MusicMute += Row.MusicState == 0 ? 1 : 0;
		FoleySamples += (Row.FoleyState == 0 || Row.FoleyState == 1) ? 1 : 0;
		FoleyMute += Row.FoleyState == 0 ? 1 : 0;
	}
	UE_LOG(LogTortunabo, Display, TEXT("[AudioCensus]   música activa en %d muestras, sin voz en %d; tortuga activa en %d, sin voz en %d."), MusicSamples, MusicMute,
		FoleySamples, FoleyMute);
	const bool bPassed = bStable && MusicMute == 0 && FoleyMute == 0;
	UE_LOG(LogTortunabo, Display, TEXT("[AudioCensus] Veredicto: %s"), bPassed ? TEXT("ESTABLE (nada crece y la música y la tortuga siempre tienen voz)")
		: TEXT("FALLA (algo crece o la música o la tortuga se han quedado sin voz)"));
}
