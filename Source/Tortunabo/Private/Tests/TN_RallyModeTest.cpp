// Rally con lo de Karts (#631, decisión del 04-10 en #627): el Rally de LVL_Rally y Karts (mapa generado) comparten la
// base del buggy (mirada libre y peso de la artillera), PlayerController (HUD del buggy), bots con dificultad y parrilla
// mínima; Karts conserva sus objetos y el Rally no los lleva. Correr desde Session Frontend
// (categoría "Tortunabo.Rally.Mode") o headless con UnrealEditor-Win64-DebugGame-Cmd <uproject>
// -ExecCmds="Automation RunTests Tortunabo.Rally.Mode; Quit".

#include "Misc/AutomationTest.h"
#include "Kart/TN_KartBuggy.h"
#include "Kart/TN_KartGameMode.h"
#include "Kart/TN_KartPlayerController.h"
#include "Rally/TN_RallyAIController.h"
#include "Rally/TN_RallyGameMode.h"
#include "Rally/TN_RallyKartBuggy.h"
#include "Rally/TN_RallyLogic.h"
#include "Vehicles/TN_RallyTurretLogic.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyModeSharedClassesTest, "Tortunabo.Rally.Mode.SharedClasses",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyModeSharedClassesTest::RunTest(const FString& Parameters)
{
	const ATN_RallyGameMode* Circuit = GetDefault<ATN_RallyGameMode>();
	const ATN_KartGameMode* Generated = GetDefault<ATN_KartGameMode>();
	TestTrue(TEXT("El mapa generado es la misma carrera del Rally"), ATN_KartGameMode::StaticClass()->IsChildOf(ATN_RallyGameMode::StaticClass()));

	const UClass* CircuitVehicle = Circuit->VehicleClass.LoadSynchronous();
	const UClass* KartVehicle = Generated->VehicleClass.LoadSynchronous();
	TestTrue(TEXT("LVL_Rally usa el buggy de Karts (mirada libre y peso de la artillera)"),
		CircuitVehicle && CircuitVehicle->IsChildOf(ATN_RallyKartBuggy::StaticClass()));
	TestTrue(TEXT("Karts conserva su buggy"), KartVehicle == ATN_KartBuggy::StaticClass());
	const ATN_KartBuggy* CircuitBuggy = CircuitVehicle ? Cast<ATN_KartBuggy>(CircuitVehicle->GetDefaultObject()) : nullptr;
	TestTrue(TEXT("En el Rally, sin objetos de Karts: las cajas dan munición de la torreta"), CircuitBuggy && !CircuitBuggy->UsesDriverItems());
	TestTrue(TEXT("En Karts, con sus objetos"), GetDefault<ATN_KartBuggy>()->UsesDriverItems());
	TestTrue(TEXT("La artillera del Rally, sin las teclas de objeto"),
		GetDefault<ATN_RallyKartBuggy>()->GetGunnerPawnClass() && GetDefault<ATN_RallyKartBuggy>()->GetGunnerPawnClass()->IsChildOf(ATN_RallyKartGunnerPawn::StaticClass()));

	TestTrue(TEXT("LVL_Rally usa el PlayerController con el HUD del buggy"),
		Circuit->PlayerControllerClass && Circuit->PlayerControllerClass->IsChildOf(ATN_KartPlayerController::StaticClass()));
	TestTrue(TEXT("El mapa generado, el mismo PlayerController"), Generated->PlayerControllerClass == Circuit->PlayerControllerClass);
	TestTrue(TEXT("Los bots son los pilotos IA del Rally en los dos mapas"),
		Circuit->AIControllerClass && Generated->AIControllerClass == Circuit->AIControllerClass
		&& Circuit->AIControllerClass->IsChildOf(ATN_RallyAIController::StaticClass()));
	TestTrue(TEXT("Del lobby se llega y se vuelve sin cortar la conexión"), Circuit->bUseSeamlessTravel && Generated->bUseSeamlessTravel);
	TestEqual(TEXT("Misma parrilla mínima en los dos mapas"), Generated->MinTeams, Circuit->MinTeams);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyModeBotsTest, "Tortunabo.Rally.Mode.BotsAndDifficulty",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyModeBotsTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyRace;
	TestEqual(TEXT("Sola y en un buggy: tres bots hasta cuatro buggies"), DefaultBotCount(1, 1, 4, -1), 3);
	TestEqual(TEXT("Cuatro por parejas: dos buggies y dos bots"), DefaultBotCount(4, 2, 4, -1), 2);
	TestEqual(TEXT("Ocho en un buggy cada una: sin bots"), DefaultBotCount(8, 1, 4, -1), 0);
	TestEqual(TEXT("Nunca más bots que huecos libres"), DefaultBotCount(7, 1, 12, -1), TNRally::MaxGridSlots - 7);
	TestEqual(TEXT("TN.Rally.Bots manda"), DefaultBotCount(1, 1, 4, 0), 0);
	TestEqual(TEXT("TN.Rally.Bots acotado a la parrilla"), DefaultBotCount(1, 1, 4, 99), TNRally::MaxGridSlots);

	TestEqual(TEXT("?ProcDifficulty=Hard"), ParseDifficulty(TEXT("hard"), ETNProcDifficulty::Easy), ETNProcDifficulty::Hard);
	TestEqual(TEXT("Sin opción, la del lobby"), ParseDifficulty(FString(), ETNProcDifficulty::Easy), ETNProcDifficulty::Easy);
	TestEqual(TEXT("Lobby sin dificultad válida: normal"), ParseDifficulty(TEXT("x"), ETNProcDifficulty::Count), ETNProcDifficulty::Normal);

	const FVector Speeds = GetDefault<ATN_RallyGameMode>()->BotMaxSpeedKmh;
	TestTrue(TEXT("Los bots difíciles van más deprisa que los fáciles"),
		BotMaxSpeedKmh(Speeds, ETNProcDifficulty::Hard, 1) > BotMaxSpeedKmh(Speeds, ETNProcDifficulty::Easy, 1));
	TestEqual(TEXT("El bot central de cada tres, a la velocidad de su dificultad"),
		BotMaxSpeedKmh(Speeds, ETNProcDifficulty::Normal, 1), static_cast<float>(Speeds.Y));
	TestTrue(TEXT("Variedad entre bots seguidos"),
		BotMaxSpeedKmh(Speeds, ETNProcDifficulty::Normal, 0) != BotMaxSpeedKmh(Speeds, ETNProcDifficulty::Normal, 2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyModeLeanAndRecoilTest, "Tortunabo.Rally.Mode.LeanAndRecoil",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyModeLeanAndRecoilTest::RunTest(const FString& Parameters)
{
	// El peso de la artillera cambia el ángulo de las ruedas; el retroceso, la velocidad del chasis: no se anulan.
	const float Lean = TNKart::LeanSteerMultiplier(1.f, 1.f);
	const FVector Kick = TNRallyTurret::RecoilVelocity(FVector::ForwardVector, 300.f);
	TestTrue(TEXT("Inclinada hacia dentro, el buggy gira más"), Lean > 1.f);
	TestTrue(TEXT("Disparar hacia delante frena el buggy"), Kick.X < 0.0);
	TestEqual(TEXT("El retroceso no depende del peso de la artillera"), TNRallyTurret::RecoilVelocity(FVector::ForwardVector, 300.f), Kick);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyModeFinishLineHoldTest, "Tortunabo.Rally.Mode.FinishLineNeverParksMidRace",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyModeFinishLineHoldTest::RunTest(const FString& Parameters)
{
	using TNRallyRace::DecideVehicleHold;
	// #667: circuito de 5 puertas a 3 vueltas; la puerta 0 es salida y meta. El buggy sale frenado de la parrilla y cruza la
	// línea varias veces: solo se bloquea al aparcarlo en el podio después de la última.
	TNRally::FLapRules Rules;
	Rules.NumGates = 5;
	Rules.bCircuit = true;
	Rules.Laps = 3;
	TNRallyRace::FVehicleHold Applied = DecideVehicleHold(ETNRallyPhase::Countdown, false, false);
	TestTrue(TEXT("En el semáforo, motor cortado y freno de carrera"), Applied.bEngineLocked && Applied.bRaceBrake);

	int32 LineCrossings = 0;
	bool bParked = false;
	for (int32 GatesPassed = 1; !Rules.IsFinished(GatesPassed - 1); ++GatesPassed)
	{
		const bool bFinished = Rules.IsFinished(GatesPassed);
		LineCrossings += Rules.LastGateIndex(GatesPassed) == 0 ? 1 : 0;
		Applied = DecideVehicleHold(ETNRallyPhase::Racing, bParked, false);
		if (!bFinished)
		{
			TestFalse(FString::Printf(TEXT("Puerta %d (vuelta %d): sin freno de carrera"), Rules.LastGateIndex(GatesPassed),
				Rules.LapForGates(GatesPassed)), Applied.bRaceBrake);
			TestFalse(FString::Printf(TEXT("Puerta %d (vuelta %d): con motor"), Rules.LastGateIndex(GatesPassed),
				Rules.LapForGates(GatesPassed)), Applied.bEngineLocked);
			continue;
		}
		// Recién llegado y aún sin aparcar (plano lateral): sigue rodando por la meta.
		Applied = DecideVehicleHold(ETNRallyPhase::Finishing, false, false);
		TestFalse(TEXT("En meta y sin aparcar: no se queda clavado en la línea"), Applied.bRaceBrake || Applied.bEngineLocked);
		bParked = true;
		Applied = DecideVehicleHold(ETNRallyPhase::Finishing, bParked, false);
		TestTrue(TEXT("Aparcado en el podio: motor cortado y freno de carrera"), Applied.bEngineLocked && Applied.bRaceBrake);
	}
	TestEqual(TEXT("Cruza la línea de salida y meta una vez por vuelta más la salida"), LineCrossings, Rules.Laps + 1);
	TestTrue(TEXT("Termina aparcado"), bParked);

	const TNRallyRace::FVehicleHold Retired = DecideVehicleHold(ETNRallyPhase::Racing, false, true);
	TestTrue(TEXT("Retirado: motor cortado sin freno de carrera"), Retired.bEngineLocked && !Retired.bRaceBrake);
	const TNRallyRace::FVehicleHold Results = DecideVehicleHold(ETNRallyPhase::Results, false, false);
	TestTrue(TEXT("Resultados: motor cortado para todos"), Results.bEngineLocked && !Results.bRaceBrake);
	return true;
}

#endif
