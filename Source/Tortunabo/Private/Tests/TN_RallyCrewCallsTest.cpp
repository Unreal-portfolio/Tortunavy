// La artillera canta notas y avisos a la conductora (#330, Rally/TN_RallyCrewCalls.h): validación del servidor, la nota
// que llega por red y la señal que oye la conductora. Sin mundo. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.CrewCalls; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Rally/TN_RallyCrewCalls.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRallyCrewCallsTest
{
	using namespace TNRallyPaceNotes;

	FPaceNote Turn(ETurnDirection Direction, int32 Grade, double ArcCm = 0.0, double AngleDeg = 90.0, bool bDontCut = false)
	{
		FPaceNote Note;
		Note.Kind = ENoteKind::Turn;
		Note.Direction = Direction;
		Note.Grade = Grade;
		Note.ArcCm = ArcCm;
		Note.AngleDeg = AngleDeg;
		Note.bDontCut = bDontCut;
		return Note;
	}

	FNoteAhead Ahead(const FPaceNote& Note, double DistanceCm)
	{
		FNoteAhead Entry;
		Entry.Note = Note;
		Entry.DistanceCm = DistanceCm;
		return Entry;
	}

	bool SameSignal(const TNRallyCopilot::FCallSignal& A, const TNRallyCopilot::FCallSignal& B)
	{
		return A.Sound == B.Sound && A.Beeps == B.Beeps && FMath::IsNearlyEqual(A.Pan, B.Pan) && FMath::IsNearlyEqual(A.Pitch, B.Pitch);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCrewCallsValidateTest,
	"Tortunabo.Rally.CrewCalls.Validate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyCrewCallsValidateTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyCrewCalls;
	TestTrue(TEXT("nota a 120 m, sentada y tras la espera"), CanGunnerCall(true, 12000.0, 1.0));
	TestTrue(TEXT("justo a 600 m"), CanGunnerCall(true, MaxNoteAheadCm, 1.0));
	TestTrue(TEXT("justo al llegar a la nota"), CanGunnerCall(true, 0.0, 1.0));
	TestFalse(TEXT("a más de 600 m"), CanGunnerCall(true, MaxNoteAheadCm + 1.0, 1.0));
	TestFalse(TEXT("ya pasada"), CanGunnerCall(true, -10.0, 1.0));
	TestFalse(TEXT("sin sentarse"), CanGunnerCall(false, 12000.0, 1.0));
	TestFalse(TEXT("antes de 0,6 s"), CanGunnerCall(true, 12000.0, 0.59));
	TestTrue(TEXT("a los 0,6 s"), CanGunnerCall(true, 12000.0, MinSecondsBetweenCalls));
	TestTrue(TEXT("aviso rápido tras la espera"), CanGunnerQuickCall(true, 0.7));
	TestFalse(TEXT("aviso rápido en ráfaga"), CanGunnerQuickCall(true, 0.2));
	TestFalse(TEXT("aviso rápido sin sentarse"), CanGunnerQuickCall(false, 5.0));

	using TNRallyCrewCallsTest::Turn;
	using TNRallyCrewCallsTest::Ahead;
	using TNRallyPaceNotes::ETurnDirection;
	const TArray<TNRallyPaceNotes::FNoteAhead> List = { Ahead(Turn(ETurnDirection::Left, 3, 50000.0), 8000.0),
		Ahead(Turn(ETurnDirection::Right, 5, 61000.0), 19000.0) };
	TestEqual(TEXT("encuentra la primera por su arco"), FindCalledNote(List, 50000.0), 0);
	TestEqual(TEXT("y la segunda con un desfase pequeño"), FindCalledNote(List, 61000.0 + ArcMatchToleranceCm * 0.5), 1);
	TestEqual(TEXT("un arco que no es de ninguna nota se rechaza"), FindCalledNote(List, 55000.0), INDEX_NONE);
	TestEqual(TEXT("sin notas por delante, nada"), FindCalledNote({}, 50000.0), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCrewCallsSignalTest,
	"Tortunabo.Rally.CrewCalls.Signal",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyCrewCallsSignalTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyCrewCalls;
	using namespace TNRallyPaceNotes;
	using TNRallyCrewCallsTest::Turn;
	using TNRallyCrewCallsTest::SameSignal;

	// La nota que llega por red suena igual que la original: lado de la curva y tantos pitidos como el grado.
	for (const FPaceNote& Original : { Turn(ETurnDirection::Left, 2), Turn(ETurnDirection::Right, 5), Turn(ETurnDirection::Right, 1, 0.0, 170.0, true) })
	{
		const FPaceNote Received = NoteFromCall(MakeNoteCall(Original));
		const TNRallyCopilot::FCallSignal Signal = TNRallyCopilot::SignalFor(Received);
		TestTrue(TEXT("misma señal que la nota original"), SameSignal(Signal, TNRallyCopilot::SignalFor(Original)));
		TestEqual(TEXT("tantos pitidos como el grado"), Signal.Beeps, Original.Grade);
		TestTrue(TEXT("suena del lado de la curva"), Original.Direction == ETurnDirection::Left ? Signal.Pan < 0.f : Signal.Pan > 0.f);
		TestEqual(TEXT("misma placa"), TNRallyCopilot::PlateHeadline(Received).ToString(), TNRallyCopilot::PlateHeadline(Original).ToString());
		TestEqual(TEXT("mismo detalle («no cortes», horquilla)"), TNRallyCopilot::PlateDetail(Received).ToString(),
			TNRallyCopilot::PlateDetail(Original).ToString());
	}
	FPaceNote Crest;
	Crest.Kind = ENoteKind::Crest;
	TestTrue(TEXT("una cresta llega como cresta"), NoteFromCall(MakeNoteCall(Crest)).Kind == ENoteKind::Crest);

	FTNRallyCrewCall Corrupt;
	Corrupt.Kind = 200;
	Corrupt.Direction = 77;
	Corrupt.Grade = 99;
	const FPaceNote Safe = NoteFromCall(Corrupt);
	TestTrue(TEXT("un paquete corrupto no sale de rango"), Safe.Kind == ENoteKind::Turn && Safe.Direction == ETurnDirection::None
		&& Safe.Grade <= MaxGrade);

	const TNRallyCopilot::FCallSignal Boost = QuickSignal(ETNRallyQuickCall::Boost);
	const TNRallyCopilot::FCallSignal Brake = QuickSignal(ETNRallyQuickCall::Brake);
	TestEqual(TEXT("los avisos suenan centrados"), Boost.Pan + FMath::Abs(Brake.Pan), 0.f);
	TestTrue(TEXT("turbo y freno se distinguen de oído"), Boost.Beeps != Brake.Beeps && Boost.Pitch > Brake.Pitch);
	TestNotEqual(TEXT("y en la placa"), QuickHeadline(ETNRallyQuickCall::Boost).ToString(), QuickHeadline(ETNRallyQuickCall::Brake).ToString());

	ETNRallyQuickCall Quick = ETNRallyQuickCall::Brake;
	TestTrue(TEXT("1 o cruceta arriba: turbo"), QuickFromAxis(1.f, Quick) && Quick == ETNRallyQuickCall::Boost);
	TestTrue(TEXT("2 o B: freno"), QuickFromAxis(-1.f, Quick) && Quick == ETNRallyQuickCall::Brake);
	TestFalse(TEXT("sin pulsar: nada"), QuickFromAxis(0.f, Quick));
	return true;
}

#endif
