// Circuitos por vueltas generados (#622): lectura de bank_deg y elements del manifest, puertas inclinadas con el peralte, notas
// del copiloto con los saltos y rasantes del manifest y frenada del piloto IA (TNRallyCircuit). Lógica pura; los de variantes
// construyen la pista de R01 y de todo el selector del Rally en un mundo vacío (solo editor: Scripts/ no se empaqueta). Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Circuit; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Lobby/TN_LobbyMission.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Rally/TN_RallyCircuit.h"
#include "Rally/TN_RallyGate.h"
#include "Rally/TN_RallyLogic.h"
#include "Rally/TN_RallyPaceNotes.h"
#include "Rally/TN_RallyTrack.h"
#include "Rally/TN_RallyTrackDressing.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRallyCircuitTest
{
	const TCHAR* const CircuitVariant = TEXT("R01_circuito_dunas");

	TSharedPtr<FJsonObject> ParseJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		FJsonSerializer::Deserialize(Reader, Root);
		return Root;
	}

	int32 CountKind(TConstArrayView<TNRallyCircuit::FFeatureArc> Features, TNRallyCircuit::EElementKind Kind)
	{
		int32 Count = 0;
		for (const TNRallyCircuit::FFeatureArc& Feature : Features)
		{
			Count += Feature.Kind == Kind ? 1 : 0;
		}
		return Count;
	}

	struct FScopedTestWorld
	{
		UWorld* World = nullptr;

		FScopedTestWorld()
		{
			if (!GEngine)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNRallyCircuitTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FScopedTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCircuitParseTest, "Tortunabo.Rally.Circuit.ParseFields",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyCircuitParseTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyCircuit;
	const TSharedPtr<FJsonObject> Root = TNRallyCircuitTest::ParseJson(TEXT(R"({
		"bank_deg": [0.0, 7.5, 15.0],
		"elements": [
			{"type": "salto", "id": "salto_1", "s_m": [10.0, 40.0], "lip_s_m": 20.0, "landing_s_m": [25.0, 35.0], "v_design_kmh": 90.0},
			{"type": "rasante", "id": "rasante_1", "s_m": [50.0, 80.0], "crest_s_m": 65.0},
			{"type": "horquilla", "id": "horquilla_1", "s_m": [90.0, 120.0], "radius_m": 20.0},
			{"type": "otra_cosa", "s_m": [1.0, 2.0]}
		]})"));
	TArray<double> Bank;
	TArray<FElement> Elements;
	FString Error;
	if (!TestTrue(TEXT("JSON de prueba"), Root.IsValid()) || !TestTrue(TEXT("lee los campos"), ReadCircuitFields(*Root, 3, Bank, Elements, Error)))
	{
		return false;
	}
	TestEqual(TEXT("un peralte por punto del eje"), Bank.Num(), 3);
	TestEqual(TEXT("peralte del último punto"), Bank.Last(), 15.0);
	if (!TestEqual(TEXT("cuatro elementos"), Elements.Num(), 4))
	{
		return false;
	}
	TestTrue(TEXT("salto"), Elements[0].Kind == EElementKind::Jump);
	TestEqual(TEXT("labio del salto"), Elements[0].KeyM, 20.0);
	TestEqual(TEXT("final del aterrizaje"), Elements[0].LandingEndM, 35.0);
	TestEqual(TEXT("velocidad de diseño"), Elements[0].DesignKmh, 90.0);
	TestTrue(TEXT("rasante"), Elements[1].Kind == EElementKind::Crest);
	TestEqual(TEXT("cima del rasante"), Elements[1].KeyM, 65.0);
	TestTrue(TEXT("horquilla"), Elements[2].Kind == EElementKind::Hairpin);
	TestEqual(TEXT("radio de la horquilla"), Elements[2].RadiusM, 20.0);
	TestTrue(TEXT("un tipo desconocido no rompe la lectura"), Elements[3].Kind == EElementKind::Other);
	TestTrue(TEXT("sin punto clave, KeyM negativo"), Elements[2].KeyM < 0.0);

	TestTrue(TEXT("bank_deg de otro largo que road_uu se ignora sin fallar"), ReadCircuitFields(*Root, 5, Bank, Elements, Error));
	TestEqual(TEXT("y queda sin peralte"), Bank.Num(), 0);

	const TSharedPtr<FJsonObject> Broken = TNRallyCircuitTest::ParseJson(TEXT(R"({"elements": [{"type": "salto"}]})"));
	TestFalse(TEXT("un elemento sin s_m es un manifest roto"), ReadCircuitFields(*Broken, 0, Bank, Elements, Error));

	const TSharedPtr<FJsonObject> Legacy = TNRallyCircuitTest::ParseJson(TEXT(R"({"road_uu": [[0, 0, 0], [100, 0, 0]]})"));
	TestTrue(TEXT("un manifest sin los campos nuevos (E01B, I03R) se lee"), ReadCircuitFields(*Legacy, 2, Bank, Elements, Error));
	TestTrue(TEXT("y no trae ni peralte ni elementos"), Bank.Num() == 0 && Elements.Num() == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCircuitGateBankTest, "Tortunabo.Rally.Circuit.GateFollowsBank",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyCircuitGateBankTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyCircuit;
	const FVector HalfExtent(200.0, 1200.0, 500.0);
	const double BankDeg = 15.0;
	const FQuat Banked = GateRotation(0.0, BankDeg).Quaternion();
	TestTrue(TEXT("peralte positivo: la derecha de la puerta queda más baja"), Banked.RotateVector(FVector::RightVector).Z < -0.2);
	TestTrue(TEXT("peralte negativo: la izquierda más baja"), GateRotation(0.0, -BankDeg).Quaternion().RotateVector(FVector::RightVector).Z > 0.2);

	// Buggy por el lado bajo de una curva peraltada de 15 grados: 5 m a la derecha del eje, con el origen 50 cm sobre la
	// calzada inclinada (1,3 m por debajo de la cota del eje). La puerta se construye sobre el eje, como ATN_RallyTrack.
	const FVector Axis(1000.0, 2000.0, 300.0);
	const double Lateral = 500.0;
	const double SurfaceDrop = Lateral * FMath::Tan(FMath::DegreesToRadians(BankDeg));
	const FVector Prev(Axis.X - 100.0, Axis.Y + Lateral, Axis.Z - SurfaceDrop + 50.0);
	const FVector Cur(Axis.X + 100.0, Axis.Y + Lateral, Axis.Z - SurfaceDrop + 50.0);
	auto CrossingOf = [&Axis, &HalfExtent](const FQuat& Rotation)
	{
		return FTransform(Rotation, Axis + Rotation.RotateVector(FVector(0.0, 0.0, HalfExtent.Z)));
	};
	double Alpha = 0.0;
	bool bForward = false;
	TestFalse(TEXT("con la puerta recta, el buggy del lado bajo pasa por debajo (el fallo de R01)"),
		TNRally::SegmentCrossesGate(Prev, Cur, CrossingOf(FQuat::Identity), HalfExtent, Alpha, bForward));
	TestTrue(TEXT("con la puerta inclinada, cuenta"), TNRally::SegmentCrossesGate(Prev, Cur, CrossingOf(Banked), HalfExtent, Alpha, bForward));
	TestTrue(TEXT("y hacia delante"), bForward);
	TestTrue(TEXT("sin peralte, la rotación es la de antes"), GateRotation(30.0, 0.0).Equals(FRotator(0.0, 30.0, 0.0)));

	// Puerta 2 de R01 en una vaguada: el origen del buggy pasa 8 cm por debajo de la cota del eje. El volumen de la puerta
	// baja ATN_RallyGate::BelowRoadCm bajo la calzada y lo cuenta; con el de antes (de 0 a 10 m) no contaba.
	const FVector Low = Axis + FVector(0.0, 64.0, -8.0);
	const FTransform Gate(FQuat::Identity, Axis + ATN_RallyGate::CrossingCenterOffset());
	TestTrue(TEXT("un buggy 8 cm por debajo del eje cruza la puerta"), TNRally::SegmentCrossesGate(Low - FVector(100.0, 0.0, 0.0),
		Low + FVector(100.0, 0.0, 0.0), Gate, ATN_RallyGate::CrossingHalfExtent(), Alpha, bForward));
	const FTransform OldGate(FQuat::Identity, Axis + FVector(0.0, 0.0, 0.5 * ATN_RallyGate::HeightCm));
	TestFalse(TEXT("con el volumen de antes, no"), TNRally::SegmentCrossesGate(Low - FVector(100.0, 0.0, 0.0), Low + FVector(100.0, 0.0, 0.0),
		OldGate, FVector(ATN_RallyGate::DepthCm, ATN_RallyGate::WidthCm, ATN_RallyGate::HeightCm) * 0.5, Alpha, bForward));
	const FVector Deep = Axis + FVector(0.0, 0.0, -ATN_RallyGate::BelowRoadCm - 100.0);
	TestFalse(TEXT("un tramo 4 m por debajo (otro nivel) no cruza"), TNRally::SegmentCrossesGate(Deep - FVector(100.0, 0.0, 0.0),
		Deep + FVector(100.0, 0.0, 0.0), Gate, ATN_RallyGate::CrossingHalfExtent(), Alpha, bForward));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCircuitArcsTest, "Tortunabo.Rally.Circuit.RoadArcsAndBank",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyCircuitArcsTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyCircuit;
	// Cuadrado de 100 m de lado (lazo de 400 m) con un punto por esquina.
	const TArray<FVector> Road = { FVector(0, 0, 0), FVector(10000, 0, 0), FVector(10000, 10000, 0), FVector(0, 10000, 0) };
	double RoadLength = 0.0;
	const TArray<double> Arcs = RoadPointArcs(Road, true, 41000.0, RoadLength);
	TestEqual(TEXT("el lazo cuenta el tramo que lo cierra"), RoadLength, 40000.0);
	TestEqual(TEXT("arcos escalados a la longitud de la spline"), Arcs[2], 20500.0, 0.01);
	TestEqual(TEXT("metros de road_uu a arco de la spline"), RoadMetersToArc(200.0, RoadLength, 41000.0, true), 20500.0, 0.01);
	TestEqual(TEXT("y envueltos en circuito"), RoadMetersToArc(410.0, RoadLength, 41000.0, true), 1025.0, 0.01);
	double OpenLength = 0.0;
	RoadPointArcs(Road, false, 30000.0, OpenLength);
	TestEqual(TEXT("en punto a punto no se cierra"), OpenLength, 30000.0);

	const TArray<double> Bank = { 0.0, 10.0, 10.0, -10.0 };
	TestEqual(TEXT("peralte interpolado"), BankAtArc(Arcs, Bank, 5125.0, 41000.0, true), 5.0, 0.01);
	TestEqual(TEXT("en un punto, el suyo"), BankAtArc(Arcs, Bank, 20500.0, 41000.0, true), 10.0, 0.01);
	TestEqual(TEXT("entre el último punto y el primero, el lazo se cierra"), BankAtArc(Arcs, Bank, 35875.0, 41000.0, true), -5.0, 0.01);
	TestEqual(TEXT("sin muestras, sin peralte"), BankAtArc(TArray<double>(), TArray<double>(), 100.0, 41000.0, true), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCircuitBrakeTest, "Tortunabo.Rally.Circuit.AIBrakesForJumpsAndHairpins",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyCircuitBrakeTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyCircuit;
	const double Length = 180000.0;
	const double Decel = 0.5 * 981.0;
	const double Probe = 9000.0;
	const float Max = 90.f;
	FBrakeTuning Tuning;

	TestEqual(TEXT("sin elementos (E01B, I03R) no cambia nada"), FeatureSpeedLimitKmh({}, 0.0, Length, true, Probe, Decel, Max, Tuning), Max);

	FFeatureArc Jump;
	Jump.Kind = EElementKind::Jump;
	Jump.StartCm = 60000.0;
	Jump.KeyCm = 63000.0;
	Jump.EndCm = 70000.0;
	Jump.DesignKmh = 94.0;
	const TArray<FFeatureArc> Jumps = { Jump };
	const float LipKmh = 94.f * Tuning.JumpLipSpeedFactor;
	TestEqual(TEXT("en el labio, la velocidad de diseño por el factor"), FeatureSpeedLimitKmh(Jumps, 63000.0, Length, true, Probe, Decel, Max, Tuning),
		LipKmh, 0.01f);
	// Desde 90 km/h, a 0,5 g, la bajada a 84,6 km/h cabe en los últimos 8 m: 5 m antes ya pide menos de 90.
	const float Before = FeatureSpeedLimitKmh(Jumps, 63000.0 - 500.0, Length, true, Probe, Decel, Max, Tuning);
	TestTrue(*FString::Printf(TEXT("5 m antes del labio ya frena (%.1f km/h)"), Before), Before > LipKmh && Before < Max);
	const float FastBefore = FeatureSpeedLimitKmh(Jumps, 63000.0 - 3000.0, Length, true, Probe, Decel, 130.f, Tuning);
	const float Expected = TNRally::ApproachSpeedKmh(LipKmh, 3000.0, Decel);
	TestEqual(*FString::Printf(TEXT("a más velocidad, 30 m antes pide %.1f km/h (frenada a tiempo)"), FastBefore), FastBefore, Expected, 0.01f);
	TestTrue(TEXT("y es menos que la punta"), FastBefore < 130.f);
	TestEqual(TEXT("más allá de la sonda no frena"), FeatureSpeedLimitKmh(Jumps, 63000.0 - 20000.0, Length, true, Probe, Decel, Max, Tuning), Max);
	TestEqual(TEXT("con el labio atrás (en la mesa) no frena"), FeatureSpeedLimitKmh(Jumps, 64000.0, Length, true, Probe, Decel, Max, Tuning), Max);

	FFeatureArc Hairpin;
	Hairpin.Kind = EElementKind::Hairpin;
	Hairpin.StartCm = 175000.0;
	Hairpin.EndCm = 3000.0;
	Hairpin.KeyCm = Hairpin.StartCm;
	Hairpin.RadiusM = 18.0;
	const TArray<FFeatureArc> Hairpins = { Hairpin };
	const float HairpinKmh = static_cast<float>(TNRally::CmsToKmh(FMath::Sqrt(981.0 * 1800.0 * Tuning.HairpinLateralG)));
	TestEqual(TEXT("dentro de la horquilla, la de su radio (también cruzando la salida)"),
		FeatureSpeedLimitKmh(Hairpins, 1000.0, Length, true, Probe, Decel, Max, Tuning), HairpinKmh, 0.01f);
	TestTrue(TEXT("40 m antes de la horquilla ya frena"), FeatureSpeedLimitKmh(Hairpins, 171000.0, Length, true, Probe, Decel, Max, Tuning) < Max);
	TestEqual(TEXT("pasada la horquilla, nada"), FeatureSpeedLimitKmh(Hairpins, 4000.0, Length, true, Probe, Decel, Max, Tuning), Max);

	FFeatureArc Crest;
	Crest.Kind = EElementKind::Crest;
	Crest.StartCm = 1000.0;
	Crest.KeyCm = 2000.0;
	Crest.EndCm = 3000.0;
	TestEqual(TEXT("los rasantes no frenan (no despegan ni con turbo)"), FeatureSpeedLimitKmh({ Crest }, 500.0, Length, true, Probe, Decel, Max, Tuning), Max);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCircuitNotesTest, "Tortunabo.Rally.Circuit.PaceNotesFromManifest",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyCircuitNotesTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyPaceNotes;
	TNRallyPaceNotes::FTrackNotes Track = Build({ FVector(0, 0, 0), FVector(100000, 0, 0) }, false, false, 0.0);
	FPaceNote Detected;
	Detected.Kind = ENoteKind::Jump;
	Detected.ArcCm = 30000.0;
	Track.Notes.Add(Detected);
	FPaceNote Turn;
	Turn.Kind = ENoteKind::Turn;
	Turn.ArcCm = 50000.0;
	Track.Notes.Add(Turn);

	TNRallyPaceNotes::FTrackNotes Untouched = Track;
	ApplyAuthoredVerticalNotes(Untouched, {});
	TestEqual(TEXT("sin elementos, las notas detectadas se quedan"), Untouched.Notes.Num(), 2);

	TNRallyCircuit::FFeatureArc Jump;
	Jump.Kind = TNRallyCircuit::EElementKind::Jump;
	Jump.KeyCm = 61000.0;
	TNRallyCircuit::FFeatureArc Crest;
	Crest.Kind = TNRallyCircuit::EElementKind::Crest;
	Crest.KeyCm = 20000.0;
	TNRallyCircuit::FFeatureArc Hairpin;
	Hairpin.Kind = TNRallyCircuit::EElementKind::Hairpin;
	Hairpin.KeyCm = 80000.0;
	ApplyAuthoredVerticalNotes(Track, { Jump, Crest, Hairpin });
	if (!TestEqual(TEXT("cresta, curva y salto"), Track.Notes.Num(), 3))
	{
		return false;
	}
	TestTrue(TEXT("primero la cresta del manifest"), Track.Notes[0].Kind == ENoteKind::Crest && Track.Notes[0].ArcCm == 20000.0);
	TestTrue(TEXT("la curva detectada se queda"), Track.Notes[1].Kind == ENoteKind::Turn);
	TestTrue(TEXT("el salto, en el labio del manifest (no en el detectado)"), Track.Notes[2].Kind == ENoteKind::Jump && Track.Notes[2].ArcCm == 61000.0);
	TestEqual(TEXT("se canta «salto»"), NoteText(Track.Notes[2]).ToString(), NSLOCTEXT("Rally", "PaceJump", "salto").ToString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCircuitR01Test, "Tortunabo.Rally.Circuit.R01BuildsAsLapCircuit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTNRallyCircuitR01Test::RunTest(const FString& Parameters)
{
	using namespace TNRallyCircuit;
	const FName Variant(TNRallyCircuitTest::CircuitVariant);
	FString ManifestText;
	if (!TestTrue(TEXT("manifest de R01"), FFileHelper::LoadFileToString(ManifestText, *TNRally::VariantManifestPath(Variant))))
	{
		return false;
	}
	TNRally::FTrackSource Source;
	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("manifest válido (%s)"), *Error), TNRally::ParseTrackManifest(ManifestText, Source, Error)))
	{
		return false;
	}
	TestTrue(TEXT("circuito cerrado"), TNRally::IsCircuit(Source));
	TestEqual(TEXT("un peralte por punto de road_uu"), Source.RoadBankDeg.Num(), Source.Road.Num());
	TestEqual(TEXT("un ancho por punto de road_uu (tramos variados)"), Source.RoadWidthsCm.Num(), Source.Road.Num());

	TNRallyCircuitTest::FScopedTestWorld Scoped;
	if (!TestNotNull(TEXT("mundo de prueba"), Scoped.World))
	{
		return false;
	}
	ATN_RallyTrack* Track = Scoped.World->SpawnActor<ATN_RallyTrack>();
	if (!TestNotNull(TEXT("pista"), Track) || !TestTrue(TEXT("la pista de R01 se construye"), Track->BuildFromVariant(Variant)))
	{
		return false;
	}
	TestTrue(TEXT("es un circuito por vueltas"), Track->IsCircuit());
	TestEqual(TEXT("vueltas del manifest"), Track->GetManifestLaps(), 3);
	TestEqual(TEXT("9 puertas"), Track->GetGateCount(), 9);
	TestTrue(TEXT("la puerta 0 (salida y meta) en la línea de salida"),
		FVector::Dist2D(Track->GetLocationAtArc(Track->GetGateArc(0)), Source.Start) < 100.0);
	TestTrue(TEXT("longitud de la vuelta (1830 m ± 1 %)"), FMath::IsNearlyEqual(Track->GetTrackLengthCm(), 183000.0, 1830.0));

	const TArray<FFeatureArc>& Features = Track->GetFeatures();
	TestEqual(TEXT("3 saltos"), TNRallyCircuitTest::CountKind(Features, EElementKind::Jump), 3);
	TestEqual(TEXT("2 rasantes"), TNRallyCircuitTest::CountKind(Features, EElementKind::Crest), 2);
	TestEqual(TEXT("2 horquillas"), TNRallyCircuitTest::CountKind(Features, EElementKind::Hairpin), 2);

	// Puerta 1, al final de la primera curva peraltada (14,8 grados): la puerta se inclina con la calzada.
	const double GateBank = Track->GetBankDegAtArc(Track->GetGateArc(1));
	TestTrue(*FString::Printf(TEXT("peralte en la puerta 1: %.1f grados"), GateBank), GateBank > 13.0);
	const FRotator GateRotation = Track->GetGateCrossingTransform(1).Rotator();
	TestEqual(TEXT("la puerta 1 lleva el peralte"), GateRotation.Roll, GateBank, 0.5);
	TestEqual(TEXT("la puerta 0 (recta) va recta"), Track->GetGateCrossingTransform(0).Rotator().Roll, 0.0, 0.1);

	// Notas: los saltos y rasantes salen del manifest, en el labio y la cima.
	const TNRallyPaceNotes::FTrackNotes Notes = TNRallyPaceNotes::BuildForTrack(*Track);
	int32 JumpNotes = 0;
	int32 CrestNotes = 0;
	for (const TNRallyPaceNotes::FPaceNote& Note : Notes.Notes)
	{
		JumpNotes += Note.Kind == TNRallyPaceNotes::ENoteKind::Jump ? 1 : 0;
		CrestNotes += Note.Kind == TNRallyPaceNotes::ENoteKind::Crest ? 1 : 0;
	}
	TestEqual(TEXT("una nota de salto por salto"), JumpNotes, 3);
	TestEqual(TEXT("una nota de cresta por rasante"), CrestNotes, 2);
	for (const FFeatureArc& Feature : Features)
	{
		if (Feature.Kind != EElementKind::Jump)
		{
			continue;
		}
		const FVector Lip = Track->GetLocationAtArc(Feature.KeyCm);
		TestTrue(TEXT("el labio está en lo alto de la rampa (más alto que 30 m antes)"), Lip.Z > Track->GetLocationAtArc(Feature.KeyCm - 3000.0).Z);
	}

	// Parrilla de 8 huecos detrás de la línea, sin solaparse.
	TArray<FVector> Slots;
	for (int32 Slot = 0; Slot < TNRally::MaxGridSlots; ++Slot)
	{
		Slots.Add(Track->GetGridSlotTransform(Slot, 0.0).GetLocation());
	}
	bool bSeparated = true;
	for (int32 A = 0; A < Slots.Num(); ++A)
	{
		for (int32 B = A + 1; B < Slots.Num(); ++B)
		{
			bSeparated &= FVector::Dist2D(Slots[A], Slots[B]) > 500.0;
		}
	}
	TestTrue(TEXT("los 8 huecos de la parrilla, a más de 5 m entre sí"), bSeparated);
	const FVector Forward = Track->GetDirectionAtArc(Track->GetGateArc(0));
	TestTrue(TEXT("la parrilla, detrás de la salida"), ((Slots[0] - Track->GetLocationAtArc(Track->GetGateArc(0))) | Forward) < 0.0);

	// Tramos variados: la calzada se estrecha y se ensancha por el lazo.
	TestTrue(TEXT("la pista trae ancho por tramos"), Track->HasRoadWidthsPerPoint());
	double MinWidth = TNumericLimits<double>::Max();
	double MaxWidth = 0.0;
	for (double Arc = 0.0; Arc < Track->GetTrackLengthCm(); Arc += 500.0)
	{
		MinWidth = FMath::Min(MinWidth, Track->GetRoadWidthAtArcCm(Arc));
		MaxWidth = FMath::Max(MaxWidth, Track->GetRoadWidthAtArcCm(Arc));
	}
	TestTrue(*FString::Printf(TEXT("hay un tramo estrecho (%.1f m)"), MinWidth / 100.0), MinWidth <= 1250.0);
	TestTrue(*FString::Printf(TEXT("hay un tramo ancho (%.1f m)"), MaxWidth / 100.0), MaxWidth >= 1650.0);
	TestEqual(TEXT("road_width_m es el máximo"), Track->GetRoadWidthCm(), MaxWidth, 30.0);
	for (const FFeatureArc& Feature : Features)
	{
		if (Feature.Kind == EElementKind::Hairpin)
		{
			const double Mid = Feature.StartCm + 0.5 * TNRally::ForwardArc(Feature.StartCm, Feature.EndCm, Track->GetTrackLengthCm(), true);
			TestTrue(TEXT("las horquillas se ensanchan"), Track->GetRoadWidthAtArcCm(Mid) >= 1650.0);
		}
	}

	// Barrera continua a los dos lados del lazo, pegada al borde de cada tramo.
	const TNRallyDressing::FTrackData Data = TNRallyDressing::SampleTrack(*Track, 400.0);
	const TNRallyDressing::FBarrierPlan Plan = TNRallyDressing::PlanBarriers(Data, TArray<uint8>(), TNRallyDressing::FBarrierParams());
	int32 OnRoad = 0;
	for (int32 Index = 0; Index < Data.Samples.Num(); ++Index)
	{
		for (int32 Side = TNRallyDressing::LeftSide; Side <= TNRallyDressing::RightSide; ++Side)
		{
			const double Offset = Plan.Sides[Side].OffsetCm.IsValidIndex(Index) ? Plan.Sides[Side].OffsetCm[Index] : 0.0;
			OnRoad += Offset > 0.0 && Offset < 0.5 * Data.Samples[Index].RoadWidthCm ? 1 : 0;
		}
	}
	TestEqual(TEXT("ninguna barrera dentro de su tramo de calzada"), OnRoad, 0);
	for (int32 Side = TNRallyDressing::LeftSide; Side <= TNRallyDressing::RightSide; ++Side)
	{
		int32 TooWide = 0;
		for (const double Gap : TNRallyDressing::BarrierGapsCm(Data, Plan, Side))
		{
			TooWide += Gap > TNRallyDressing::TurtleWidthCm ? 1 : 0;
		}
		TestEqual(*FString::Printf(TEXT("barrera %d sin huecos de más de una tortuga"), Side), TooWide, 0);
	}
	Track->ClearTrack();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCircuitVariantsTest, "Tortunabo.Rally.Circuit.CatalogBuilds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTNRallyCircuitVariantsTest::RunTest(const FString& Parameters)
{
	// Todos los circuitos del selector del Rally (#692: solo los del generador de vueltas) se construyen como circuito, con
	// sus puertas, sus elementos (saltos, baches...) y peralte en alguna puerta.
	const TArray<FName>& Circuits = TNLobbyMission::RallyMapOptions();
	TestTrue(TEXT("al menos seis circuitos en el selector (R01 a R06)"), Circuits.Num() >= 6);
	TNRallyCircuitTest::FScopedTestWorld Scoped;
	if (!TestNotNull(TEXT("mundo de prueba"), Scoped.World))
	{
		return false;
	}
	ATN_RallyTrack* Track = Scoped.World->SpawnActor<ATN_RallyTrack>();
	if (!TestNotNull(TEXT("pista"), Track))
	{
		return false;
	}
	for (const FName Circuit : Circuits)
	{
		const FString Name = Circuit.ToString();
		if (!TestTrue(*FString::Printf(TEXT("%s se construye desde su manifest"), *Name), Track->BuildFromVariant(Circuit)))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s: circuito por vueltas"), *Name), Track->IsCircuit());
		TestTrue(*FString::Printf(TEXT("%s: al menos 3 puertas"), *Name), Track->GetGateCount() >= 3);
		TestTrue(*FString::Printf(TEXT("%s: con los elementos del generador"), *Name), Track->GetFeatures().Num() >= 4);
	}
	Track->ClearTrack();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
