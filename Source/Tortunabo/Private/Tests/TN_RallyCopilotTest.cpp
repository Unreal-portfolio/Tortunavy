// Copiloto automático del Rally (#331): momento de cantar según la velocidad (unos 3 s antes, nunca a menos de 60 m), quién
// lo oye, la señal de cada nota (lado y pitidos según el grado), el orden de los cantos y la placa. Correr con
// "Automation RunTests Tortunabo.Rally.Copilot".

#include "Misc/AutomationTest.h"
#include "Rally/TN_RallyCopilotCalls.h"
#include "Rally/TN_RallyLogic.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRallyCopilotTest
{
	using namespace TNRallyPaceNotes;

	FPaceNote Turn(ETurnDirection Direction, int32 Grade, double ArcCm = 0.0)
	{
		FPaceNote Note;
		Note.Kind = ENoteKind::Turn;
		Note.Direction = Direction;
		Note.Grade = Grade;
		Note.ArcCm = ArcCm;
		return Note;
	}

	FPaceNote Other(ENoteKind Kind, double ArcCm = 0.0)
	{
		FPaceNote Note;
		Note.Kind = Kind;
		Note.ArcCm = ArcCm;
		return Note;
	}

	FNoteAhead Ahead(const FPaceNote& Note, double DistanceCm)
	{
		FNoteAhead Entry;
		Entry.Note = Note;
		Entry.DistanceCm = DistanceCm;
		return Entry;
	}

	double KmhToCms(double Kmh)
	{
		return Kmh * 100000.0 / 3600.0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCopilotMomentTest, "Tortunabo.Rally.Copilot.CallMoment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNRallyCopilotMomentTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyCopilot;
	using namespace TNRallyCopilotTest;
	// Parado o despacio: a 60 m (a 72 km/h, 3 s son justo 60 m).
	TestEqual(TEXT("Parado: 60 m"), CallDistanceCm(0.0), 6000.0);
	TestEqual(TEXT("Marcha atrás cuenta como parado"), CallDistanceCm(-1500.0), 6000.0);
	TestEqual(TEXT("A 40 km/h: 60 m"), CallDistanceCm(KmhToCms(40.0)), 6000.0);
	TestTrue(TEXT("A 72 km/h: 60 m"), FMath::IsNearlyEqual(CallDistanceCm(KmhToCms(72.0)), 6000.0, 0.01));
	// Más deprisa: lo que se recorre en 3 s (punta medida en I03R: 113,5 km/h → 94,6 m).
	TestTrue(TEXT("A 108 km/h: 90 m"), FMath::IsNearlyEqual(CallDistanceCm(KmhToCms(108.0)), 9000.0, 0.01));
	TestTrue(TEXT("A 113,5 km/h: 94,6 m"), FMath::IsNearlyEqual(CallDistanceCm(KmhToCms(113.5)), 9458.3, 0.1));

	// A cualquier velocidad por encima de 72 km/h la nota se canta a 3 s de llegar; por debajo, antes (60 m).
	for (const double Kmh : { 0.0, 30.0, 72.0, 90.0, 113.5 })
	{
		const double Speed = KmhToCms(Kmh);
		const double Distance = CallDistanceCm(Speed);
		TestTrue(FString::Printf(TEXT("A %.1f km/h toca a %.0f cm"), Kmh, Distance), IsTimeToCall(Distance, Speed));
		TestFalse(FString::Printf(TEXT("A %.1f km/h aún no toca a %.0f cm"), Kmh, Distance + 10.0), IsTimeToCall(Distance + 10.0, Speed));
		if (Speed > 0.0)
		{
			TestTrue(FString::Printf(TEXT("A %.1f km/h quedan al menos 3 s"), Kmh), Distance / Speed >= DefaultLeadSeconds - 1e-6);
		}
	}
	TestFalse(TEXT("Una nota ya pasada no se canta"), IsTimeToCall(-100.0, 0.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCopilotWhoTest, "Tortunabo.Rally.Copilot.OnlyWithoutHumanGunner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNRallyCopilotWhoTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyCopilot;
	TestTrue(TEXT("Conductora sola (o con bot): canta"), IsAutoCopilotActive(true, false));
	TestFalse(TEXT("Conductora con artillera humana: apagado"), IsAutoCopilotActive(true, true));
	TestFalse(TEXT("Artillera: no se lo canta a sí misma"), IsAutoCopilotActive(false, false));
	TestFalse(TEXT("Artillera con conductora humana: apagado"), IsAutoCopilotActive(false, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCopilotSignalTest, "Tortunabo.Rally.Copilot.Signal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNRallyCopilotSignalTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyCopilot;
	using namespace TNRallyCopilotTest;
	const FCallSignal Left3 = SignalFor(Turn(ETurnDirection::Left, 3));
	TestTrue(TEXT("Izquierda 3: pitido a la izquierda"), Left3.Sound == ECallSound::Beep && Left3.Pan < 0.f);
	TestEqual(TEXT("Izquierda 3: tres pitidos"), Left3.Beeps, 3);
	const FCallSignal Right6 = SignalFor(Turn(ETurnDirection::Right, 6));
	TestTrue(TEXT("Derecha 6: a la derecha, seis pitidos, tono base"), Right6.Pan > 0.f && Right6.Beeps == 6 && FMath::IsNearlyEqual(Right6.Pitch, 1.f));
	TestTrue(TEXT("Cuanto más cerrada, más aguda"), SignalFor(Turn(ETurnDirection::Right, 1)).Pitch > Left3.Pitch && Left3.Pitch > Right6.Pitch);
	TestEqual(TEXT("Grado fuera de rango: se recorta a 6"), SignalFor(Turn(ETurnDirection::Left, 9)).Beeps, 6);
	TestEqual(TEXT("Grado 0: se recorta a 1"), SignalFor(Turn(ETurnDirection::Left, 0)).Beeps, 1);
	// Seis pitidos caben antes de la separación mínima entre cantos.
	TestTrue(TEXT("Seis pitidos duran menos que la separación entre cantos"), 6 * BeepIntervalSeconds < MinSecondsBetweenCalls);
	const TPair<ENoteKind, ECallSound> Cases[] = { { ENoteKind::Crest, ECallSound::Crest }, { ENoteKind::Jump, ECallSound::Jump },
		{ ENoteKind::Water, ECallSound::Water } };
	for (const TPair<ENoteKind, ECallSound>& Case : Cases)
	{
		const FCallSignal Signal = SignalFor(Other(Case.Key));
		TestTrue(FString::Printf(TEXT("Nota %d: su sonido, una vez y al centro"), static_cast<int32>(Case.Key)),
			Signal.Sound == Case.Value && Signal.Beeps == 1 && Signal.Pan == 0.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCopilotPickTest, "Tortunabo.Rally.Copilot.PickOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNRallyCopilotPickTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyCopilot;
	using namespace TNRallyCopilotTest;
	const TArray<FNoteAhead> Notes = { Ahead(Turn(ETurnDirection::Left, 3, 10000.0), 5000.0),
		Ahead(Other(ENoteKind::Crest, 17000.0), 12000.0) };
	TestEqual(TEXT("Parado: la de 50 m"), PickNoteToCall(Notes, 0.0, {}, 10.0), 0);
	TestEqual(TEXT("Ya cantada la de 50 m, la de 120 m aún no toca parado"), PickNoteToCall(Notes, 0.0, { 10000.0 }, 10.0), INDEX_NONE);
	TestEqual(TEXT("A 144 km/h (120 m en 3 s) sí toca"), PickNoteToCall(Notes, KmhToCms(144.0), { 10000.0 }, 10.0), 1);
	TestEqual(TEXT("Recién cantada otra: espera"), PickNoteToCall(Notes, 0.0, {}, 0.3), INDEX_NONE);
	TestEqual(TEXT("Todas cantadas: nada"), PickNoteToCall(Notes, KmhToCms(144.0), { 10000.0, 17000.0 }, 10.0), INDEX_NONE);

	// Al pasar una nota sale de las de delante: se olvida y en la vuelta siguiente se vuelve a cantar.
	const TArray<FNoteAhead> AfterFirst = { Notes[1] };
	const TArray<double> Kept = KeepCalledAhead({ 10000.0, 17000.0 }, AfterFirst);
	TestTrue(TEXT("Se olvida la ya pasada y se recuerda la que sigue delante"), Kept.Num() == 1 && Kept[0] == 17000.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCopilotPlateTest, "Tortunabo.Rally.Copilot.Plate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNRallyCopilotPlateTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyCopilot;
	using namespace TNRallyCopilotTest;
	TestEqual(TEXT("Izquierda 3: el ángulo a la izquierda"), PlateHeadline(Turn(ETurnDirection::Left, 3)).ToString(), FString(TEXT("‹ 3")));
	TestEqual(TEXT("Derecha 5: el ángulo a la derecha"), PlateHeadline(Turn(ETurnDirection::Right, 5)).ToString(), FString(TEXT("5 ›")));
	const FPaceNote Curve = Turn(ETurnDirection::Right, 2);
	TestTrue(TEXT("Curva: debajo, la nota entera"), PlateDetail(Curve).EqualTo(NoteText(Curve)));
	const FPaceNote Crest = Other(ENoteKind::Crest);
	TestTrue(TEXT("Cresta: la nota en grande"), PlateHeadline(Crest).EqualTo(NoteText(Crest)));
	TestTrue(TEXT("Cresta: sin línea pequeña"), PlateDetail(Crest).IsEmpty());
	return true;
}

#endif
