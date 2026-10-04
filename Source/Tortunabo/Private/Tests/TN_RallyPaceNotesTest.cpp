// Notas de copiloto del Rally (Rally/TN_RallyPaceNotes.h) con trazados sintéticos: recta, horquilla, S, cresta, salto,
// vadeo, «no cortes» y circuito. Correr desde Session Frontend (categoría "Tortunabo.Rally.PaceNotes") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.PaceNotes; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Rally/TN_RallyPaceNotes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNPaceNotesTestHelpers
{
	/** Traza un eje a pasos de 1 m o 1°: rectas con pendiente y arcos (ángulo positivo = derecha, como el yaw de UE). */
	struct FPathBuilder
	{
		TArray<FVector> Points;
		FVector Position = FVector::ZeroVector;
		double YawDeg = 0.0;

		FPathBuilder() { Points.Add(Position); }

		FPathBuilder& Straight(double LengthCm, double Slope = 0.0)
		{
			const int32 Steps = FMath::Max(1, FMath::CeilToInt32(LengthCm / 100.0));
			const double Step = LengthCm / Steps;
			const FVector Direction = FRotator(0.0, YawDeg, 0.0).Vector();
			for (int32 Index = 0; Index < Steps; ++Index)
			{
				Position += Direction * Step + FVector(0.0, 0.0, Slope * Step);
				Points.Add(Position);
			}
			return *this;
		}

		FPathBuilder& Arc(double RadiusCm, double AngleDeg)
		{
			const int32 Steps = FMath::Max(4, FMath::CeilToInt32(FMath::Abs(AngleDeg)));
			const double StepYaw = AngleDeg / Steps;
			const double StepLength = RadiusCm * FMath::DegreesToRadians(FMath::Abs(AngleDeg)) / Steps;
			for (int32 Index = 0; Index < Steps; ++Index)
			{
				// Cuerda con el rumbo medio del paso: el polígono queda inscrito en el arco.
				YawDeg += 0.5 * StepYaw;
				Position += FRotator(0.0, YawDeg, 0.0).Vector() * StepLength;
				YawDeg += 0.5 * StepYaw;
				Points.Add(Position);
			}
			return *this;
		}
	};

	int32 CountKind(const TNRallyPaceNotes::FTrackNotes& Track, TNRallyPaceNotes::ENoteKind Kind)
	{
		int32 Count = 0;
		for (const TNRallyPaceNotes::FPaceNote& Note : Track.Notes)
		{
			if (Note.Kind == Kind) { ++Count; }
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPaceNotesStraightTest, "Tortunabo.Rally.PaceNotes.Straight",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPaceNotesStraightTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyPaceNotes;
	TNPaceNotesTestHelpers::FPathBuilder Path;
	Path.Straight(50000.0);
	const FTrackNotes Track = Build(Path.Points, false, true, -500.0);
	TestTrue(TEXT("El eje remuestreado es válido"), Track.IsValid());
	TestNearlyEqual(TEXT("Longitud de 500 m"), Track.LengthCm, 50000.0, 1.0);
	TestEqual(TEXT("Una recta seca y llana no lleva notas"), Track.Notes.Num(), 0);
	TestEqual(TEXT("Grado de un radio enorme"), GradeFor(1.0e6, 90.0), MaxGrade);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPaceNotesHairpinTest, "Tortunabo.Rally.PaceNotes.Hairpin",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPaceNotesHairpinTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyPaceNotes;
	TNPaceNotesTestHelpers::FPathBuilder Path;
	Path.Straight(10000.0).Arc(1500.0, -180.0).Straight(10000.0);
	const FTrackNotes Track = Build(Path.Points, false, false, 0.0);
	if (!TestEqual(TEXT("Una sola nota"), Track.Notes.Num(), 1))
	{
		return false;
	}
	const FPaceNote& Note = Track.Notes[0];
	TestTrue(TEXT("Es una curva"), Note.Kind == ENoteKind::Turn);
	TestTrue(TEXT("A la izquierda (yaw decreciente)"), Note.Direction == ETurnDirection::Left);
	TestEqual(TEXT("Grado 1"), Note.Grade, 1);
	TestNearlyEqual(TEXT("Gira unos 180°"), Note.AngleDeg, 180.0, 10.0);
	TestNearlyEqual(TEXT("Radio de unos 15 m"), Note.RadiusCm, 1500.0, 250.0);
	TestFalse(TEXT("Una horquilla cerrada se puede pisar por dentro"), Note.bDontCut);
	TestTrue(TEXT("Empieza al final de la recta de 100 m"), FMath::Abs(Note.ArcCm - 10000.0) <= 1500.0);
	TestEqual(TEXT("Se canta «horquilla izquierda»"), NoteText(Note).BuildSourceString(), FString(TEXT("horquilla izquierda")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPaceNotesEssTest, "Tortunabo.Rally.PaceNotes.Ess",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPaceNotesEssTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyPaceNotes;
	TNPaceNotesTestHelpers::FPathBuilder Path;
	Path.Straight(10000.0).Arc(6000.0, -90.0).Arc(6000.0, 90.0).Straight(10000.0);
	const FTrackNotes Track = Build(Path.Points, false, false, 0.0);
	if (!TestEqual(TEXT("Dos curvas"), Track.Notes.Num(), 2))
	{
		return false;
	}
	TestTrue(TEXT("Primero izquierda"), Track.Notes[0].Direction == ETurnDirection::Left);
	TestTrue(TEXT("Luego derecha"), Track.Notes[1].Direction == ETurnDirection::Right);
	TestEqual(TEXT("Izquierda de 60 m de radio y 90°: grado 4"), Track.Notes[0].Grade, 4);
	TestEqual(TEXT("Derecha igual: grado 4"), Track.Notes[1].Grade, 4);
	TestEqual(TEXT("Se canta «izquierda 4»"), NoteText(Track.Notes[0]).BuildSourceString(), FString(TEXT("izquierda 4")));

	const TArray<FNoteAhead> FromStart = NotesAhead(Track, 0.0, 40000.0);
	TestEqual(TEXT("Desde la salida se ven las dos"), FromStart.Num(), 2);
	const TArray<FNoteAhead> Inside = NotesAhead(Track, Track.Notes[0].ArcCm + 100.0, 40000.0);
	TestEqual(TEXT("Pasada la primera queda una"), Inside.Num(), 1);
	TestTrue(TEXT("Y es la derecha"), Inside.Num() == 1 && Inside[0].Note.Direction == ETurnDirection::Right);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPaceNotesCrestTest, "Tortunabo.Rally.PaceNotes.Crest",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPaceNotesCrestTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyPaceNotes;
	using TNPaceNotesTestHelpers::CountKind;
	TNPaceNotesTestHelpers::FPathBuilder Path;
	Path.Straight(10000.0).Straight(10000.0, 0.1).Straight(10000.0, -0.1).Straight(10000.0);
	const FTrackNotes Track = Build(Path.Points, false, false, 0.0);
	TestEqual(TEXT("Una cresta"), CountKind(Track, ENoteKind::Crest), 1);
	TestEqual(TEXT("Sin saltos"), CountKind(Track, ENoteKind::Jump), 0);
	TestEqual(TEXT("Sin curvas"), CountKind(Track, ENoteKind::Turn), 0);
	if (Track.Notes.Num() == 1)
	{
		TestNearlyEqual(TEXT("En lo alto (a 200 m)"), Track.Notes[0].ArcCm, 20000.0, 1500.0);
		TestEqual(TEXT("Se canta «cresta»"), NoteText(Track.Notes[0]).BuildSourceString(), FString(TEXT("cresta")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPaceNotesJumpWaterTest, "Tortunabo.Rally.PaceNotes.JumpAndWater",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPaceNotesJumpWaterTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyPaceNotes;
	using TNPaceNotesTestHelpers::CountKind;
	TNPaceNotesTestHelpers::FPathBuilder Ramp;
	Ramp.Straight(10000.0).Straight(2000.0, 0.3).Straight(10000.0);
	const FTrackNotes Jump = Build(Ramp.Points, false, false, 0.0);
	TestEqual(TEXT("La rampa acaba en salto"), CountKind(Jump, ENoteKind::Jump), 1);
	TestEqual(TEXT("Y no en cresta"), CountKind(Jump, ENoteKind::Crest), 0);

	TNPaceNotesTestHelpers::FPathBuilder Ford;
	Ford.Straight(10000.0).Straight(2000.0, -0.05).Straight(5000.0).Straight(2000.0, 0.05).Straight(10000.0);
	const FTrackNotes Water = Build(Ford.Points, false, true, -50.0);
	if (TestEqual(TEXT("Un vadeo y nada más"), Water.Notes.Num(), 1))
	{
		TestTrue(TEXT("Es agua"), Water.Notes[0].Kind == ENoteKind::Water);
		TestTrue(TEXT("De 50 a 90 m"), Water.Notes[0].LengthCm >= 5000.0 && Water.Notes[0].LengthCm <= 9000.0);
	}
	const FTrackNotes Dry = Build(Ford.Points, false, false, -50.0);
	TestEqual(TEXT("Sin nivel del agua no hay vadeo"), CountKind(Dry, ENoteKind::Water), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPaceNotesDontCutTest, "Tortunabo.Rally.PaceNotes.DontCut",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPaceNotesDontCutTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyPaceNotes;
	TNPaceNotesTestHelpers::FPathBuilder Path;
	Path.Straight(10000.0).Arc(9000.0, 135.0).Straight(10000.0);
	const FTrackNotes Track = Build(Path.Points, false, false, 0.0);
	if (!TestEqual(TEXT("Una curva"), Track.Notes.Num(), 1))
	{
		return false;
	}
	const FPaceNote& Note = Track.Notes[0];
	TestTrue(TEXT("Derecha"), Note.Direction == ETurnDirection::Right);
	TestEqual(TEXT("90 m de radio (5) y 135° (uno menos): grado 4"), Note.Grade, 4);
	TestTrue(TEXT("La cuerda se aleja más de 25 m: no cortes"), Note.bDontCut);
	TestEqual(TEXT("Se canta «derecha 4, no cortes»"), NoteText(Note).BuildSourceString(), FString(TEXT("derecha 4, no cortes")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPaceNotesCircuitTest, "Tortunabo.Rally.PaceNotes.Circuit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPaceNotesCircuitTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyPaceNotes;
	TNPaceNotesTestHelpers::FPathBuilder Path;
	Path.Straight(10000.0);
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		Path.Arc(3000.0, 90.0).Straight(Corner < 3 ? 20000.0 : 10000.0);
	}
	const FTrackNotes Track = Build(Path.Points, true, false, 0.0);
	if (!TestEqual(TEXT("Cuatro esquinas"), Track.Notes.Num(), 4))
	{
		return false;
	}
	for (const FPaceNote& Note : Track.Notes)
	{
		TestTrue(TEXT("Todas a la derecha"), Note.Direction == ETurnDirection::Right);
		TestEqual(TEXT("30 m de radio y 90°: grado 2"), Note.Grade, 2);
	}
	// A 10 m de cerrar la vuelta, la próxima nota es la primera esquina de la vuelta siguiente.
	const TArray<FNoteAhead> Ahead = NotesAhead(Track, Track.LengthCm - 1000.0, 40000.0);
	if (TestTrue(TEXT("Se ve la esquina de la vuelta siguiente"), Ahead.Num() >= 1))
	{
		TestNearlyEqual(TEXT("Distancia dando la vuelta"), Ahead[0].DistanceCm, Track.Notes[0].ArcCm + 1000.0, 1.0);
	}
	TestTrue(TEXT("El punto del arco da la vuelta"), LocationAtArc(Track, Track.LengthCm + 1.0).Equals(Track.Points[0], 5.0));
	return true;
}

#endif
