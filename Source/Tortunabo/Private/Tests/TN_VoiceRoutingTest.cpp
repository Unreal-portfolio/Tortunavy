// Reparto de la voz por proximidad (TNVoiceRouting): a quién reenvía el servidor cada paquete. Dentro del radio exterior y,
// con tope, solo los oyentes más cercanos. Correr con "Automation RunTests Tortunabo.Voice.Routing".

#include "Misc/AutomationTest.h"
#include "Voice/TN_VoiceRouting.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNVoiceRoutingTest
{
	constexpr double OuterRadiusCm = 2500.0;

	double Squared(double DistanceCm)
	{
		return DistanceCm * DistanceCm;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVoiceRoutingRangeTest, "Tortunabo.Voice.Routing.Range",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNVoiceRoutingRangeTest::RunTest(const FString& Parameters)
{
	using namespace TNVoiceRoutingTest;
	TestTrue(TEXT("A 10 m: se manda"), TNVoiceRouting::IsInRange(Squared(1000.0), OuterRadiusCm));
	TestTrue(TEXT("Justo en el radio: se manda"), TNVoiceRouting::IsInRange(Squared(OuterRadiusCm), OuterRadiusCm));
	TestFalse(TEXT("A 50 m: no se manda"), TNVoiceRouting::IsInRange(Squared(5000.0), OuterRadiusCm));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVoiceRoutingListenerCapTest, "Tortunabo.Voice.Routing.ListenerCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNVoiceRoutingListenerCapTest::RunTest(const FString& Parameters)
{
	using namespace TNVoiceRoutingTest;
	// Seis oyentes dentro del radio (tope de 4) y uno fuera.
	const TArray<double> Distances = { Squared(900.0), Squared(300.0), Squared(1200.0), Squared(500.0), Squared(6000.0),
		Squared(700.0), Squared(1100.0) };
	const TArray<bool> Selected = TNVoiceRouting::SelectListeners(Distances, OuterRadiusCm, 4);
	if (!TestEqual(TEXT("Una decisión por oyente"), Selected.Num(), Distances.Num()))
	{
		return false;
	}
	int32 Sent = 0;
	for (const bool bSent : Selected)
	{
		Sent += bSent ? 1 : 0;
	}
	TestEqual(TEXT("Solo 4 oyentes"), Sent, 4);
	TestFalse(TEXT("El que está fuera del radio no entra"), Selected[4]);
	TestFalse(TEXT("El más lejano de los de dentro se queda fuera"), Selected[2]);
	TestFalse(TEXT("El segundo más lejano también"), Selected[6]);
	TestTrue(TEXT("El más cercano entra"), Selected[1]);
	const TArray<bool> NoCap = TNVoiceRouting::SelectListeners(Distances, OuterRadiusCm, 0);
	TestEqual(TEXT("Sin tope entran los seis de dentro"), NoCap.FilterByPredicate([](bool bSent) { return bSent; }).Num(), 6);
	return true;
}

#endif
