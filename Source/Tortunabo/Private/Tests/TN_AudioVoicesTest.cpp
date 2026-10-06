// Voces del mezclador (#737): veredicto del recuento del sonido (TNAudioCensus::AnalyzeTrend) y rangos de voz
// (TNAudioVoices). Lógica pura, sin dispositivo de audio. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Audio; Quit" -nullrhi -nosound -unattended

#include "Audio/TN_AudioCensus.h"
#include "Audio/TN_AudioVoices.h"
#include "Components/AudioComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/TN_DizzyBirdsComponent.h"
#include "UI/Loading/TN_LoadingScreenSubsystem.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNAudioCensusTrendTest,
	"Tortunabo.Audio.Census.Trend",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNAudioCensusTrendTest::RunTest(const FString& Parameters)
{
	using namespace TNAudioCensus;

	// Una carrera sana: sube y baja con lo que hay cerca, sin tendencia.
	const TArray<int32> Healthy = { 4, 12, 13, 15, 18, 17, 23, 21, 16, 19, 24, 22, 17, 20, 23, 18, 21, 19, 22, 17, 20, 18 };
	const FTrend HealthyTrend = AnalyzeTrend(Healthy, 3, 4.f, 0.25f);
	TestTrue(TEXT("sana: hay veredicto"), HealthyTrend.bDecided);
	TestFalse(TEXT("sana: no crece"), HealthyTrend.bGrowing);
	TestEqual(TEXT("sana: el calentamiento no cuenta"), HealthyTrend.Samples, Healthy.Num() - 3);
	TestEqual(TEXT("sana: máximo"), HealthyTrend.Peak, 24);

	// La fuga de #737: los sintetizadores del mareo se quedaban en marcha y se sumaban sin parar.
	TArray<int32> Leak;
	for (int32 i = 0; i < 30; ++i)
	{
		Leak.Add(12 + i + (i % 3));
	}
	const FTrend LeakTrend = AnalyzeTrend(Leak, 3, 4.f, 0.25f);
	TestTrue(TEXT("fuga: hay veredicto"), LeakTrend.bDecided);
	TestTrue(TEXT("fuga: crece"), LeakTrend.bGrowing);
	TestTrue(TEXT("fuga: el último tercio por encima del primero"), LeakTrend.LastMedian > LeakTrend.FirstMedian + 10.f);

	// Un pico suelto al final (una explosión, el podio) no es una fuga: cuentan las medianas de los tercios.
	TArray<int32> Spike = { 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 40 };
	TestFalse(TEXT("pico suelto: no crece"), AnalyzeTrend(Spike, 0, 4.f, 0.25f).bGrowing);

	// La tolerancia es la mayor de la absoluta y la relativa.
	const TArray<int32> Small = { 2, 2, 2, 2, 5, 5, 5, 5, 5 };
	TestFalse(TEXT("+3 con tolerancia 4: no crece"), AnalyzeTrend(Small, 0, 4.f, 0.25f).bGrowing);
	TestTrue(TEXT("+3 con tolerancia 2: crece"), AnalyzeTrend(Small, 0, 2.f, 0.25f).bGrowing);
	const TArray<int32> Big = { 100, 100, 100, 110, 110, 110, 120, 120, 120 };
	TestFalse(TEXT("+20 % con tolerancia del 25 %: no crece"), AnalyzeTrend(Big, 0, 4.f, 0.25f).bGrowing);

	// Sin muestras suficientes no se decide nada.
	const TArray<int32> Short = { 1, 2, 3, 4, 5, 6, 7 };
	const FTrend ShortTrend = AnalyzeTrend(Short, 3, 4.f, 0.25f);
	TestFalse(TEXT("corta: sin veredicto"), ShortTrend.bDecided);
	TestFalse(TEXT("corta: no se da por fuga"), ShortTrend.bGrowing);
	TestFalse(TEXT("vacía: sin veredicto"), AnalyzeTrend(TArray<int32>(), 0, 4.f, 0.25f).bDecided);
	TestFalse(TEXT("calentamiento mayor que la serie: sin veredicto"), AnalyzeTrend(Healthy, 100, 4.f, 0.25f).bDecided);

	TestEqual(TEXT("mediana impar"), Median(TArray<int32>{ 5, 1, 3 }), 3.f);
	TestEqual(TEXT("mediana par"), Median(TArray<int32>{ 4, 1, 3, 2 }), 2.5f);
	TestEqual(TEXT("mediana vacía"), Median(TArray<int32>()), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNAudioVoicesRankTest,
	"Tortunabo.Audio.Voices.Ranks",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNAudioVoicesRankTest::RunTest(const FString& Parameters)
{
	using namespace TNAudioVoices;

	// Solo la voz reservada (música, interfaz, la tortuga propia) va con bAlwaysPlay: nada se la puede quitar.
	TestTrue(TEXT("reservada: siempre suena"), IsAlwaysPlay(ERank::Reserved));
	TestFalse(TEXT("mundo: compite"), IsAlwaysPlay(ERank::World));
	TestFalse(TEXT("fondo: compite"), IsAlwaysPlay(ERank::Background));
	// El mundo compite con los sonidos de archivo (prioridad 1 de serie); el fondo cede antes.
	TestEqual(TEXT("mundo: prioridad de los sonidos de archivo"), PriorityFor(ERank::World), 1.f);
	TestTrue(TEXT("fondo por debajo del mundo"), PriorityFor(ERank::Background) < PriorityFor(ERank::World));
	TestTrue(TEXT("reservada por encima del mundo"), PriorityFor(ERank::Reserved) > PriorityFor(ERank::World));
	TestTrue(TEXT("sin dueño: mundo"), RankForOwner(nullptr) == ERank::World);

	// Aplicar el rango toca el sintetizador y su componente de audio (el motor lee el del componente al empezar a sonar).
	UAudioComponent* Audio = NewObject<UAudioComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	Apply(*Audio, ERank::Background);
	TestFalse(TEXT("componente de fondo: sin bAlwaysPlay"), static_cast<bool>(Audio->bAlwaysPlay));
	TestTrue(TEXT("componente de fondo: prioridad forzada"), static_cast<bool>(Audio->bOverridePriority));
	TestEqual(TEXT("componente de fondo: prioridad 0,5"), Audio->Priority, 0.5f);
	Apply(*Audio, ERank::World);
	TestFalse(TEXT("componente del mundo: sin bAlwaysPlay"), static_cast<bool>(Audio->bAlwaysPlay));
	TestFalse(TEXT("componente del mundo: la prioridad del propio sonido"), static_cast<bool>(Audio->bOverridePriority));
	Apply(*Audio, ERank::Reserved);
	TestTrue(TEXT("componente reservado: bAlwaysPlay"), static_cast<bool>(Audio->bAlwaysPlay));

	// USynthComponent nace con bAlwaysPlay: esa era la raíz del reparto a ciegas de #737. Aplicar el rango lo quita.
	UTN_DizzySynthComponent* Dizzy = NewObject<UTN_DizzySynthComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	TestTrue(TEXT("sintetizador de serie: bAlwaysPlay (lo que había)"), static_cast<bool>(Dizzy->bAlwaysPlay));
	Apply(*Dizzy, ERank::World);
	TestFalse(TEXT("sintetizador del mundo: sin bAlwaysPlay"), static_cast<bool>(Dizzy->bAlwaysPlay));
	Apply(*Dizzy, ERank::Reserved);
	TestTrue(TEXT("sintetizador reservado: bAlwaysPlay"), static_cast<bool>(Dizzy->bAlwaysPlay));

	// La cola del mareo es corta: el sintetizador suelta su voz poco después de acabar (antes, nunca).
	TestTrue(TEXT("cola del mareo de 0,6 a 3 s"), UTN_DizzyBirdsComponent::SoundTailSeconds >= 0.6f && UTN_DizzyBirdsComponent::SoundTailSeconds <= 3.f);
	TestTrue(TEXT("el huevo se para tras unos segundos callado"), UTN_EggSynthComponent::IdleStopSeconds > 1.f && UTN_EggSynthComponent::IdleStopSeconds <= 5.f);
	return true;
}

#endif
