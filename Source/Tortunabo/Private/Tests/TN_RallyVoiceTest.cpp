// Voz del Rally (#329): regla de reparto de TNVoiceRouting. Las dos ocupantes de un buggy se oyen a volumen completo
// (interfono) estén donde estén; las de otros buggies, por proximidad y con el tope de oyentes; sin grupo, la voz por
// proximidad de siempre. Correr con "Automation RunTests Tortunabo.Rally.Voice".

#include "Misc/AutomationTest.h"
#include "Rally/TN_RallyPlayerState.h"
#include "Voice/TN_VoiceRouting.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRallyVoiceTest
{
	using TNVoiceRouting::ERoute;
	using TNVoiceRouting::FCandidate;

	constexpr double OuterRadiusCm = 2500.0;

	FCandidate At(int32 Group, double DistanceCm)
	{
		return FCandidate{ Group, DistanceCm * DistanceCm };
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyVoiceSameBuggyTest, "Tortunabo.Rally.Voice.SameBuggyFullVolume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNRallyVoiceSameBuggyTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyVoiceTest;
	TestTrue(TEXT("Mismo buggy: comparten interfono"), TNVoiceRouting::SharesIntercom(2, 2));
	TestEqual(TEXT("Mismo buggy, a 1 m: interfono"), TNVoiceRouting::Route(2, At(2, 100.0), OuterRadiusCm), ERoute::Intercom);
	// La cámara de persecución o una reaparición pueden separar mucho al oyente: sigue a volumen completo.
	TestEqual(TEXT("Mismo buggy, a 100 m: interfono"), TNVoiceRouting::Route(2, At(2, 10000.0), OuterRadiusCm), ERoute::Intercom);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyVoiceOtherBuggyTest, "Tortunabo.Rally.Voice.OtherBuggyProximity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNRallyVoiceOtherBuggyTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyVoiceTest;
	TestFalse(TEXT("Otro buggy: sin interfono"), TNVoiceRouting::SharesIntercom(2, 3));
	TestEqual(TEXT("Otro buggy a 10 m: proximidad"), TNVoiceRouting::Route(2, At(3, 1000.0), OuterRadiusCm), ERoute::Proximity);
	TestEqual(TEXT("Otro buggy justo en el radio: proximidad"), TNVoiceRouting::Route(2, At(3, OuterRadiusCm), OuterRadiusCm),
		ERoute::Proximity);
	TestEqual(TEXT("Otro buggy a 50 m: no se manda"), TNVoiceRouting::Route(2, At(3, 5000.0), OuterRadiusCm), ERoute::None);
	TestEqual(TEXT("Mirando la carrera (sin buggy) a 10 m: proximidad"),
		TNVoiceRouting::Route(2, At(INDEX_NONE, 1000.0), OuterRadiusCm), ERoute::Proximity);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyVoiceNoGroupTest, "Tortunabo.Rally.Voice.NoGroupIsPlainProximity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNRallyVoiceNoGroupTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyVoiceTest;
	// Fuera del Rally nadie tiene grupo: dos «sin grupo» no comparten interfono (la voz de siempre no cambia).
	TestFalse(TEXT("Sin grupo no es un grupo"), TNVoiceRouting::SharesIntercom(INDEX_NONE, INDEX_NONE));
	TestEqual(TEXT("Sin grupo, lejos: no se manda"), TNVoiceRouting::Route(INDEX_NONE, At(INDEX_NONE, 5000.0), OuterRadiusCm),
		ERoute::None);
	TestEqual(TEXT("PlayerState nulo: sin grupo"), TNVoiceRouting::IntercomGroupOf(nullptr), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("PlayerState del Rally sin plaza: sin grupo"),
		TNVoiceRouting::IntercomGroupOf(GetDefault<ATN_RallyPlayerState>()), static_cast<int32>(INDEX_NONE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyVoiceListenerCapTest, "Tortunabo.Rally.Voice.IntercomOutsideListenerCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNRallyVoiceListenerCapTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyVoiceTest;
	// Seis rivales cerca (tope de 4) y la compañera del buggy, la más lejana de todas.
	const TArray<FCandidate> Candidates = { At(5, 900.0), At(1, 300.0), At(4, 1200.0), At(3, 500.0), At(0, 6000.0),
		At(6, 700.0), At(7, 1100.0) };
	const TArray<ERoute> Routes = TNVoiceRouting::SelectListeners(0, Candidates, OuterRadiusCm, 4);
	if (!TestEqual(TEXT("Una ruta por candidato"), Routes.Num(), Candidates.Num()))
	{
		return false;
	}
	TestEqual(TEXT("La compañera, aunque lejos y fuera del tope: interfono"), Routes[4], ERoute::Intercom);
	int32 Proximity = 0;
	for (const ERoute Route : Routes)
	{
		Proximity += Route == ERoute::Proximity ? 1 : 0;
	}
	TestEqual(TEXT("Solo 4 rivales por proximidad"), Proximity, 4);
	TestEqual(TEXT("El más lejano de los rivales se queda fuera"), Routes[2], ERoute::None);
	TestEqual(TEXT("El segundo más lejano también"), Routes[6], ERoute::None);
	TestEqual(TEXT("El más cercano entra"), Routes[1], ERoute::Proximity);
	const TArray<ERoute> NoCap = TNVoiceRouting::SelectListeners(0, Candidates, OuterRadiusCm, 0);
	TestEqual(TEXT("Sin tope entran los seis rivales"), NoCap.FilterByPredicate([](ERoute R) { return R == ERoute::Proximity; }).Num(), 6);
	return true;
}

#endif
